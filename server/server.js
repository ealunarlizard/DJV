#!/usr/bin/env node
// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

// The DJV sync server: shares the markers of a review between the people
// who have it open, live, along with the media file they are about.
//
// A session is a set of markers and, optionally, the media they belong to.
// A client reads the session's state, then follows its event stream; it
// sends each change it makes, and the server numbers the changes as they
// arrive and passes them to everyone. The last change the server receives to
// a marker is the one that stands, and every client applies the changes in
// the server's order, so they all end up with the same markers. Deleted
// markers are remembered, so a client that was away learns of the deletion
// rather than sending the marker back.
//
// The sessions are kept in one SQLite file. Every change is written there
// before the server answers it, so a change a client was told was taken
// survives the server stopping however it stops, and the latest changes are
// kept too, so that a client that was away while the server restarted can
// still catch up rather than read everything again.
//
// On a studio network the server is also where DJV learns its place: the
// people who review sign in with the name and password the admin gave them
// (see users.js and the admin page), and the server tells their DJV where
// the media share is and how to sign in to it, so that DJV can map it to a
// drive.
//
// Nothing but Node: a server for a handful of people reviewing together
// should start with "node server.js". Node has SQLite built in from 22.13.
// See README.md for the protocol.

import { createServer as createHttpServer } from 'node:http';
import {
  existsSync, mkdirSync, readFileSync, readdirSync, renameSync, rmSync, statSync,
} from 'node:fs';
import { link, mkdir, open, rename, rm, unlink } from 'node:fs/promises';
import { randomBytes, randomInt } from 'node:crypto';
import { tmpdir } from 'node:os';
import { DatabaseSync } from 'node:sqlite';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

import { ConfigError, ServerConfig } from './config.js';
import {
  UPLOAD_DIR, freeSpace, isVideoPath, listMedia, pathProblem, resolveMedia, sharePath,
} from './media.js';
import {
  ROOT_USER, hashPassword, hashPasswordSync, isValidUsername, newToken,
  passwordProblem, tokenHash, verifyPassword, verifyPasswordSync,
} from './users.js';

const SERVER_DIR = path.dirname(fileURLToPath(import.meta.url));
const SERVER_VERSION = JSON.parse(readFileSync(path.join(SERVER_DIR, 'package.json'), 'utf8')).version;

// How long a session token lasts without being used. Each use pushes it on,
// so a DJV used every day stays signed in, and one left for a month signs
// in again with the password it keeps.
const TOKEN_DAYS = 30;
const DAY_MS = 24 * 60 * 60 * 1000;

// How often the use of a token is written down, at most: every request would
// be a write for nothing.
const TOKEN_TOUCH_MS = 60 * 60 * 1000;

// Signing in is refused for a while after this many failures from one
// address, so that passwords cannot be guessed at the speed of the network.
const LOGIN_FAILURES = 10;
const LOGIN_WINDOW_MS = 5 * 60 * 1000;

// A session name goes into a URL, so it is kept to what is safe there and in
// a file name, which is where sessions were kept before the database. DJV
// checks the same (models::isValidSyncSession()).
const SESSION_PATTERN = /^[A-Za-z0-9._-]{1,128}$/;
const MARKER_ID_PATTERN = /^[A-Za-z0-9._-]{1,128}$/;

// What a client can ask for in one request, and how many changes are kept
// for a client catching up. One that is further behind reads the state
// again.
const MAX_BODY = 1024 * 1024;
const MAX_LOG = 1000;

// How a video is uploaded: in pieces of this size, each one its own
// request, so that a dropped connection costs one piece rather than the
// whole file, and an upload can carry on after the page or the server was
// restarted. Unfinished uploads are dropped after a week.
const UPLOAD_CHUNK = 32 * 1024 * 1024;
const UPLOAD_KEEP_MS = 7 * 24 * 60 * 60 * 1000;
const MAX_UPLOAD = 4 * 1024 ** 4;

// What a session made on the portal says about itself.
const MAX_DESCRIPTION = 2000;
const DATE_PATTERN = /^\d{4}-\d{2}-\d{2}$/;

// The longest path the media of a session can have. Paths on a network
// share are long, but not this long; anything longer is not a path.
const MAX_MEDIA_PATH = 4096;

// The stream is kept open with a comment this often, which also tells a
// client that a silent stream has gone. DJV waits three times as long.
const HEARTBEAT_MS = 15000;

// The layout of the database, numbered so that a later server can tell an
// older file and bring it up to date.
// Version 2 adds the session tokens; version 3 who each session is for
// (session_info) and the unfinished uploads. Both only add tables, which
// "IF NOT EXISTS" brings into an older file.
const SCHEMA_VERSION = 3;

class HttpError extends Error {
  // What else the client is told, beside the message: a code it can act
  // on, for one.
  constructor(status, message, extra = {}) {
    super(message);
    this.status = status;
    this.extra = extra;
  }
}

// Which PC is asking, as DJV calls it, and with which DJV. Both come in
// headers, so that every request carries them, the event stream included.
// They say nothing about who may do what -- the user's token does that --
// and are shown on the admin page.
function identify(req) {
  const header = (name) => {
    const value = req.headers[name];
    return typeof value === 'string' ? value.trim() : '';
  };
  return {
    id: header('x-djv-client'),
    version: header('x-djv-version').slice(0, 64),
    ip: req.socket.remoteAddress ?? '',
  };
}

function isValidSession(name) {
  return SESSION_PATTERN.test(name) && name !== '.' && name !== '..';
}

// One file of a session's media, checked: the paths and the name.
function checkMediaFile(file, what) {
  const out = { ...file };
  for (const key of ['path', 'pathAbsolute', 'name']) {
    const value = file[key] ?? '';
    if (typeof value !== 'string' || value.length > MAX_MEDIA_PATH) {
      throw new HttpError(400, `The ${what}'s "${key}" must be text of up to ${MAX_MEDIA_PATH} characters.`);
    }
    out[key] = value;
  }
  if (!out.path && !out.pathAbsolute) {
    throw new HttpError(400, `The ${what} has no path.`);
  }
  return out;
}

// The media of a session as a client sent it, checked. The paths are only
// passed on: the server never opens them, and need not be able to reach the
// network storage they are on. Fields the server does not know are kept, so
// a newer DJV can say more about the media without the server's help.
//
// "compare" is the second video of a session that has two, which DJV shows
// as B beside the first.
function checkMedia(media) {
  if (!media || typeof media !== 'object' || Array.isArray(media)) {
    throw new HttpError(400, 'The request has no media.');
  }
  const out = checkMediaFile(media, 'media');
  const sharedBy = media.sharedBy ?? '';
  if (typeof sharedBy !== 'string' || sharedBy.length > MAX_MEDIA_PATH) {
    throw new HttpError(400, `The media's "sharedBy" must be text of up to ${MAX_MEDIA_PATH} characters.`);
  }
  out.sharedBy = sharedBy;
  if (media.compare === undefined || media.compare === null) {
    delete out.compare;
  } else if (typeof media.compare !== 'object' || Array.isArray(media.compare)) {
    throw new HttpError(400, 'The media\'s "compare" must be a file.');
  } else {
    out.compare = checkMediaFile(media.compare, 'compare media');
  }
  return out;
}

// The text of a request's field, checked, or the default when it is not
// given.
function textField(body, key, max, fallback) {
  const value = body[key];
  if (value === undefined) {
    return fallback;
  }
  if (typeof value !== 'string' || value.length > max) {
    throw new HttpError(400, `"${key}" must be text of up to ${max} characters.`);
  }
  return value.trim();
}

function checkDate(value) {
  if (!DATE_PATTERN.test(value) || Number.isNaN(Date.parse(`${value}T00:00:00Z`))) {
    throw new HttpError(400, 'The date must be written as YYYY-MM-DD.');
  }
  return value;
}

function today() {
  return new Date().toISOString().slice(0, 10);
}

// A session name made from what it is about, so that it says something in
// DJV and in a review file: the date, a few words of the description, and
// enough randomness that two made the same day about the same thing differ.
const NAME_ALPHABET = 'abcdefghjkmnpqrstvwxyz23456789';

function sessionName(date, description) {
  const slug = description
    .normalize('NFKD').replace(/[̀-ͯ]/g, '')
    .toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-+|-+$/g, '')
    .slice(0, 40).replace(/-+$/, '');
  let tail = '';
  for (let i = 0; i < 4; ++i) {
    tail += NAME_ALPHABET[randomInt(NAME_ALPHABET.length)];
  }
  return [date.replace(/-/g, ''), slug, tail].filter(Boolean).join('-');
}


// The sessions on disk. Each change is one transaction: the session's
// revision, the marker or media it changed, and the change itself for the
// clients catching up, all written together or not at all.
class Store {
  constructor(file, { exclusive = false } = {}) {
    this.db = new DatabaseSync(file);
    // Write-ahead logging lets the snapshot a client reads not wait on a
    // change being written, and a commit costs one write rather than two.
    // With it, NORMAL is still safe against the server crashing; only the
    // machine losing power could lose the very last changes.
    //
    // Exclusive locking is for a file system that cannot share memory
    // between processes, such as a folder of the host mounted into a
    // container: WAL then keeps its index in the server's own memory
    // rather than in a "-shm" file beside the database. Only this server
    // can open the file while it runs; copies are made with backup().
    if (exclusive) {
      this.db.exec('PRAGMA locking_mode = EXCLUSIVE;');
    }
    this.db.exec(`
      PRAGMA journal_mode = WAL;
      PRAGMA synchronous = NORMAL;
      PRAGMA busy_timeout = 5000;
      CREATE TABLE IF NOT EXISTS sessions (
        name    TEXT PRIMARY KEY,
        rev     INTEGER NOT NULL,
        media   TEXT,
        created TEXT NOT NULL,
        updated TEXT NOT NULL
      );
      CREATE TABLE IF NOT EXISTS markers (
        session TEXT NOT NULL,
        id      TEXT NOT NULL,
        data    TEXT NOT NULL,
        PRIMARY KEY (session, id)
      );
      CREATE TABLE IF NOT EXISTS deleted (
        session TEXT NOT NULL,
        id      TEXT NOT NULL,
        rev     INTEGER NOT NULL,
        PRIMARY KEY (session, id)
      );
      CREATE TABLE IF NOT EXISTS events (
        session TEXT NOT NULL,
        rev     INTEGER NOT NULL,
        data    TEXT NOT NULL,
        PRIMARY KEY (session, rev)
      );
      CREATE TABLE IF NOT EXISTS tokens (
        hash     TEXT PRIMARY KEY,
        username TEXT NOT NULL,
        client   TEXT NOT NULL,
        created  TEXT NOT NULL,
        used     TEXT NOT NULL,
        expires  TEXT NOT NULL
      );
      -- Who a session is for. A session with no row here was made before
      -- the server knew, and is open to every user until the admin says
      -- otherwise; every session made since has one from the start.
      CREATE TABLE IF NOT EXISTS session_info (
        name        TEXT PRIMARY KEY,
        description TEXT NOT NULL,
        date        TEXT NOT NULL,
        createdBy   TEXT NOT NULL,
        members     TEXT NOT NULL,
        created     TEXT NOT NULL,
        updated     TEXT NOT NULL
      );
      CREATE TABLE IF NOT EXISTS uploads (
        id       TEXT PRIMARY KEY,
        username TEXT NOT NULL,
        path     TEXT NOT NULL,
        size     INTEGER NOT NULL,
        received INTEGER NOT NULL,
        created  TEXT NOT NULL,
        updated  TEXT NOT NULL
      );
      PRAGMA user_version = ${SCHEMA_VERSION};
    `);
    const db = this.db;
    this.statements = {
      session: db.prepare('SELECT rev, media FROM sessions WHERE name = ?'),
      sessionNames: db.prepare('SELECT name FROM sessions UNION SELECT name FROM session_info ORDER BY name'),
      sessionMedia: db.prepare('SELECT name, media FROM sessions WHERE media IS NOT NULL'),
      info: db.prepare('SELECT description, date, createdBy, members, created, updated FROM session_info WHERE name = ?'),
      putInfo: db.prepare(`
        INSERT INTO session_info (name, description, date, createdBy, members, created, updated) VALUES (?, ?, ?, ?, ?, ?, ?)
        ON CONFLICT (name) DO UPDATE SET description = excluded.description, date = excluded.date,
          createdBy = excluded.createdBy, members = excluded.members, updated = excluded.updated`),
      removeSession: db.prepare('DELETE FROM sessions WHERE name = ?'),
      removeSessionInfo: db.prepare('DELETE FROM session_info WHERE name = ?'),
      removeSessionMarkers: db.prepare('DELETE FROM markers WHERE session = ?'),
      removeSessionDeleted: db.prepare('DELETE FROM deleted WHERE session = ?'),
      removeSessionEvents: db.prepare('DELETE FROM events WHERE session = ?'),
      upload: db.prepare('SELECT id, username, path, size, received, created, updated FROM uploads WHERE id = ?'),
      uploads: db.prepare('SELECT id, username, path, size, received, created, updated FROM uploads ORDER BY created'),
      uploadFor: db.prepare('SELECT id, username, path, size, received, created, updated FROM uploads WHERE path = ?'),
      putUpload: db.prepare('INSERT INTO uploads (id, username, path, size, received, created, updated) VALUES (?, ?, ?, ?, 0, ?, ?)'),
      setReceived: db.prepare('UPDATE uploads SET received = ?, updated = ? WHERE id = ?'),
      removeUpload: db.prepare('DELETE FROM uploads WHERE id = ?'),
      markers: db.prepare('SELECT data FROM markers WHERE session = ? ORDER BY rowid'),
      deleted: db.prepare('SELECT id, rev FROM deleted WHERE session = ?'),
      events: db.prepare('SELECT data FROM events WHERE session = ? ORDER BY rev'),
      putSession: db.prepare(`
        INSERT INTO sessions (name, rev, media, created, updated) VALUES (?, ?, ?, ?, ?)
        ON CONFLICT (name) DO UPDATE SET rev = excluded.rev, media = excluded.media, updated = excluded.updated`),
      // An update in place rather than a replacement, so a marker keeps its
      // row and the markers come back in the order they were first made.
      putMarker: db.prepare(`
        INSERT INTO markers (session, id, data) VALUES (?, ?, ?)
        ON CONFLICT (session, id) DO UPDATE SET data = excluded.data`),
      removeMarker: db.prepare('DELETE FROM markers WHERE session = ? AND id = ?'),
      putDeleted: db.prepare(`
        INSERT INTO deleted (session, id, rev) VALUES (?, ?, ?)
        ON CONFLICT (session, id) DO UPDATE SET rev = excluded.rev`),
      removeDeleted: db.prepare('DELETE FROM deleted WHERE session = ? AND id = ?'),
      putEvent: db.prepare('INSERT INTO events (session, rev, data) VALUES (?, ?, ?)'),
      trimEvents: db.prepare('DELETE FROM events WHERE session = ? AND rev <= ?'),
      putToken: db.prepare('INSERT INTO tokens (hash, username, client, created, used, expires) VALUES (?, ?, ?, ?, ?, ?)'),
      token: db.prepare('SELECT username, client, used, expires FROM tokens WHERE hash = ?'),
      touchToken: db.prepare('UPDATE tokens SET used = ?, expires = ? WHERE hash = ?'),
      removeToken: db.prepare('DELETE FROM tokens WHERE hash = ?'),
      removeUserTokens: db.prepare('DELETE FROM tokens WHERE username = ?'),
      removeExpiredTokens: db.prepare('DELETE FROM tokens WHERE expires < ?'),
      lastSignIn: db.prepare('SELECT username, MAX(created) AS created FROM tokens GROUP BY username'),
    };
  }

  addToken(hash, username, client, expires) {
    const now = new Date().toISOString();
    this.statements.putToken.run(hash, username, client, now, now, expires);
  }

  token(hash) {
    return this.statements.token.get(hash) ?? null;
  }

  touchToken(hash, used, expires) {
    this.statements.touchToken.run(used, expires, hash);
  }

  removeToken(hash) {
    this.statements.removeToken.run(hash);
  }

  removeUserTokens(username) {
    this.statements.removeUserTokens.run(username);
  }

  removeExpiredTokens(now) {
    this.statements.removeExpiredTokens.run(now);
  }

  // When each user last signed in, of the sign-ins still valid.
  lastSignIns() {
    return new Map(this.statements.lastSignIn.all().map((row) => [row.username, row.created]));
  }

  // A consistent copy of the whole database, made while it is in use.
  backup(file) {
    this.db.prepare('VACUUM INTO ?').run(file);
  }

  // Run the function as one transaction, undone if it throws.
  transaction(fn) {
    this.db.exec('BEGIN IMMEDIATE');
    try {
      const out = fn();
      this.db.exec('COMMIT');
      return out;
    } catch (error) {
      this.db.exec('ROLLBACK');
      throw error;
    }
  }

  has(name) {
    return this.statements.session.get(name) !== undefined ||
      this.statements.info.get(name) !== undefined;
  }

  // Who a session is for, or null for one made before the server knew.
  info(name) {
    const row = this.statements.info.get(name);
    return row ? { ...row, members: JSON.parse(row.members) } : null;
  }

  putInfo(name, info) {
    const now = new Date().toISOString();
    this.statements.putInfo.run(
      name, info.description, info.date, info.createdBy, JSON.stringify(info.members),
      info.created ?? now, now);
  }

  // The media of every session that has some, by session.
  allMedia() {
    return this.statements.sessionMedia.all().map(({ name, media }) => ({ name, media: JSON.parse(media) }));
  }

  // Forget a session and everything in it.
  removeSession(name) {
    const s = this.statements;
    this.transaction(() => {
      s.removeSession.run(name);
      s.removeSessionInfo.run(name);
      s.removeSessionMarkers.run(name);
      s.removeSessionDeleted.run(name);
      s.removeSessionEvents.run(name);
    });
  }

  upload(id) {
    return this.statements.upload.get(id) ?? null;
  }

  uploads() {
    return this.statements.uploads.all();
  }

  uploadFor(relative) {
    return this.statements.uploadFor.get(relative) ?? null;
  }

  addUpload(id, username, relative, size) {
    const now = new Date().toISOString();
    this.statements.putUpload.run(id, username, relative, size, now, now);
  }

  setReceived(id, received) {
    this.statements.setReceived.run(received, new Date().toISOString(), id);
  }

  removeUpload(id) {
    this.statements.removeUpload.run(id);
  }

  names() {
    return this.statements.sessionNames.all().map((row) => row.name);
  }

  // The session as it was last written, or an empty one when it never was.
  load(name) {
    const session = new Session(name);
    session.info = this.info(name);
    const row = this.statements.session.get(name);
    if (!row) {
      return session;
    }
    session.rev = row.rev;
    session.media = row.media ? JSON.parse(row.media) : null;
    for (const { data } of this.statements.markers.all(name)) {
      const marker = JSON.parse(data);
      session.markers.set(marker.id, marker);
    }
    for (const { id, rev } of this.statements.deleted.all(name)) {
      session.deleted.set(id, rev);
    }
    session.log = this.statements.events.all(name).map(({ data }) => JSON.parse(data));
    return session;
  }

  // Write one numbered change to a session, whose media is as given after
  // the change.
  record(name, event, media) {
    const s = this.statements;
    const now = new Date().toISOString();
    this.transaction(() => {
      s.putSession.run(name, event.rev, media ? JSON.stringify(media) : null, now, now);
      if (event.type === 'upsert') {
        s.putMarker.run(name, event.marker.id, JSON.stringify(event.marker));
        s.removeDeleted.run(name, event.marker.id);
      } else if (event.type === 'delete') {
        s.removeMarker.run(name, event.id);
        s.putDeleted.run(name, event.id, event.rev);
      }
      s.putEvent.run(name, event.rev, JSON.stringify(event));
      s.trimEvents.run(name, event.rev - MAX_LOG);
    });
  }

  // Bring in a session written by an earlier server, one JSON file each.
  // Its changes were never kept, so a client that was following it reads
  // the state again, as it did after any restart then.
  import(name, json) {
    const s = this.statements;
    const now = new Date().toISOString();
    this.transaction(() => {
      s.putSession.run(name, Number.isInteger(json.rev) ? json.rev : 0, null, now, now);
      for (const marker of Array.isArray(json.markers) ? json.markers : []) {
        if (marker && typeof marker.id === 'string') {
          s.putMarker.run(name, marker.id, JSON.stringify(marker));
        }
      }
      for (const [id, rev] of Object.entries(json.deleted ?? {})) {
        s.putDeleted.run(name, id, Number.isInteger(rev) ? rev : 0);
      }
    });
  }

  close() {
    if (!this.closed) {
      this.closed = true;
      this.db.close();
    }
  }
}

class Session {
  constructor(name) {
    this.name = name;
    this.rev = 0;
    // The media the markers are about, as the client that set it described
    // it, or null.
    this.media = null;
    // By identifier.
    this.markers = new Map();
    // The revision each marker was deleted at, by identifier.
    this.deleted = new Map();
    // The latest changes, oldest first, for a client catching up.
    this.log = [];
    this.clients = new Set();
    // Who the session is for (Store.info()), or null for one made before
    // the server knew, which every user can open.
    this.info = null;
  }

  snapshot() {
    return {
      session: this.name,
      rev: this.rev,
      media: this.media,
      markers: [...this.markers.values()],
      deleted: [...this.deleted.keys()],
    };
  }

  // The changes after the given revision, or null when some of them are no
  // longer kept.
  since(rev) {
    if (rev >= this.rev) {
      return [];
    }
    const first = this.log.length > 0 ? this.log[0].rev : this.rev + 1;
    if (rev < first - 1) {
      return null;
    }
    return this.log.filter((event) => event.rev > rev);
  }

  // Number a change, write it, and pass it to everyone following. It is
  // written before anything else is touched: a change the disk would not
  // take is refused, and the session stays as it was.
  apply(event, store) {
    const out = { ...event, rev: this.rev + 1 };
    const media = out.type === 'media' ? out.media : this.media;
    store.record(this.name, out, media);

    this.rev = out.rev;
    if (out.type === 'upsert') {
      this.markers.set(out.marker.id, out.marker);
      this.deleted.delete(out.marker.id);
    } else if (out.type === 'delete') {
      this.markers.delete(out.id);
      this.deleted.set(out.id, this.rev);
    } else if (out.type === 'media') {
      this.media = out.media;
    }
    this.log.push(out);
    if (this.log.length > MAX_LOG) {
      this.log.splice(0, this.log.length - MAX_LOG);
    }
    for (const client of this.clients) {
      writeEvent(client, out);
    }
    return out;
  }
}

function writeEvent(res, event) {
  res.write(`id: ${event.rev}\ndata: ${JSON.stringify(event)}\n\n`);
}

function sendJson(res, status, body) {
  const text = JSON.stringify(body);
  res.writeHead(status, {
    'Content-Type': 'application/json; charset=utf-8',
    'Content-Length': Buffer.byteLength(text),
  });
  res.end(text);
}

// A request's body, which must be a JSON object.
async function readObject(req) {
  const body = await readJson(req);
  if (!body || typeof body !== 'object' || Array.isArray(body)) {
    throw new HttpError(400, 'The request must be a JSON object.');
  }
  return body;
}

async function readJson(req) {
  const chunks = [];
  let size = 0;
  for await (const chunk of req) {
    size += chunk.length;
    if (size > MAX_BODY) {
      throw new HttpError(413, 'The request is too large.');
    }
    chunks.push(chunk);
  }
  try {
    return JSON.parse(Buffer.concat(chunks).toString('utf8'));
  } catch {
    throw new HttpError(400, 'The request is not JSON.');
  }
}

// Bring the sessions an earlier server kept as JSON files into the database,
// once: each file is renamed when it is in, so it is not read again, and is
// left beside the database in case it is wanted back.
function importJsonSessions(store, dataDir, log) {
  let files;
  try {
    files = readdirSync(dataDir);
  } catch (error) {
    if (error.code === 'ENOENT') {
      return;
    }
    throw error;
  }
  for (const file of files) {
    if (!file.endsWith('.json')) {
      continue;
    }
    const name = file.slice(0, -5);
    const filePath = path.join(dataDir, file);
    if (!isValidSession(name)) {
      continue;
    }
    if (store.has(name)) {
      log(`Session "${name}" is already in the database; ${file} is not imported.`);
      continue;
    }
    try {
      const json = JSON.parse(readFileSync(filePath, 'utf8'));
      // Only what an earlier server wrote, which always has the session's
      // name and its markers: the data folder may hold other JSON, the
      // server's own configuration among them.
      if (json?.session !== name || !Array.isArray(json.markers)) {
        continue;
      }
      store.import(name, json);
      renameSync(filePath, `${filePath}.imported`);
      log(`Imported session "${name}" from ${file}`);
    } catch (error) {
      log(`Cannot import session "${name}" from ${file}: ${error.message}`);
    }
  }
}

// Make sure the administrator is there, with the password from the server's
// file: that file is where the password is set, so that whoever runs the
// server can always get back in, and changing it there and restarting is
// how it is changed. Tells whether it was changed.
function ensureRoot(config, rootPassword, log) {
  if (!rootPassword) {
    if (!config.user(ROOT_USER)) {
      throw new ConfigError(`There is no "${ROOT_USER}" user, and no password to make one with: set ROOT_PASSWORD_FILE (or ROOT_PASSWORD).`);
    }
    return false;
  }
  const problem = passwordProblem(rootPassword);
  if (problem) {
    throw new ConfigError(`The "${ROOT_USER}" password: ${problem}`);
  }
  const current = config.user(ROOT_USER);
  if (current && verifyPasswordSync(rootPassword, current.passwordHash)) {
    return false;
  }
  config.setUser(ROOT_USER, {
    name: current?.name || 'Administrator',
    passwordHash: hashPasswordSync(rootPassword),
  });
  log(current ? `The "${ROOT_USER}" password was changed` : `Made the "${ROOT_USER}" user`);
  return true;
}

function isoTime(ms) {
  return new Date(ms).toISOString();
}

export function createSyncServer(options = {}) {
  const dataDir = options.dataDir ?? path.join(process.cwd(), 'data');
  const dbFile = options.dbFile ?? path.join(dataDir, 'djv-sync.db');
  const log = options.log ?? (() => {});
  // The configuration as given, or read from its file, or none at all.
  const config = options.config ??
    (options.configFile ? ServerConfig.load(options.configFile) : new ServerConfig());
  // The password of the share's account, which signed-in DJVs map the
  // share with. It is only held here, never written to the configuration.
  const sharePassword = options.sharePassword ?? '';
  const rootChanged = ensureRoot(config, options.rootPassword ?? '', log);
  // The folder the media share shares, mounted into the server so that the
  // portal can list the videos and upload new ones. Without it, the portal
  // can still show and make sessions of what DJV shares, but not list or
  // upload videos.
  const mediaDir = options.mediaDir ?? '';
  // The size of an upload's pieces; smaller in the tests.
  const uploadChunk = options.uploadChunk ?? UPLOAD_CHUNK;

  mkdirSync(path.dirname(dbFile), { recursive: true });
  const store = new Store(dbFile, { exclusive: options.exclusive ?? false });
  importJsonSessions(store, dataDir, log);
  if (rootChanged) {
    store.removeUserTokens(ROOT_USER);
  }
  store.removeExpiredTokens(isoTime(Date.now()));
  if (mediaDir) {
    sweepUploads(store, mediaDir, log);
  }

  // Checked against when the user name is not known, so that a wrong name
  // takes as long as a wrong password and does not tell which names exist.
  const nobodyHash = hashPasswordSync(newToken());

  // Who has been heard from, by user: from which PC, where, and with which
  // DJV, for the admin page. Not kept across a restart.
  const seen = new Map();
  // The event streams open now, with who opened them, for the admin page.
  const streams = new Map();
  // The failed sign-ins, by address.
  const loginFailures = new Map();

  function noteUser(user, identity) {
    seen.set(user.username, {
      pc: identity.id,
      ip: identity.ip,
      version: identity.version,
      lastSeen: new Date().toISOString(),
    });
  }

  function checkLoginAllowed(ip) {
    const entry = loginFailures.get(ip);
    if (!entry) {
      return;
    }
    if (Date.now() - entry.first > LOGIN_WINDOW_MS) {
      loginFailures.delete(ip);
      return;
    }
    if (entry.count >= LOGIN_FAILURES) {
      throw new HttpError(429, 'Too many failed sign-ins from this address; wait a few minutes.', { code: 'login-throttled' });
    }
  }

  function noteLoginFailure(ip) {
    const entry = loginFailures.get(ip);
    if (entry && Date.now() - entry.first <= LOGIN_WINDOW_MS) {
      entry.count += 1;
    } else {
      loginFailures.set(ip, { count: 1, first: Date.now() });
    }
  }

  function bearer(req) {
    const header = req.headers.authorization ?? '';
    return header.startsWith('Bearer ') ? header.slice(7).trim() : '';
  }

  // The user the request's session token belongs to, or null.
  function authenticate(req) {
    const token = bearer(req);
    if (!token) {
      return null;
    }
    const hash = tokenHash(token);
    const row = store.token(hash);
    if (!row) {
      return null;
    }
    const now = Date.now();
    if (Date.parse(row.expires) < now) {
      store.removeToken(hash);
      return null;
    }
    const user = config.userView(row.username);
    if (!user) {
      store.removeUserTokens(row.username);
      return null;
    }
    if (now - Date.parse(row.used) > TOKEN_TOUCH_MS) {
      store.touchToken(hash, isoTime(now), isoTime(now + TOKEN_DAYS * DAY_MS));
    }
    return { ...user, tokenHash: hash };
  }

  function requireUser(user) {
    if (!user) {
      throw new HttpError(401, 'Sign in first: the request has no session token, or one that has expired.', { code: 'signed-out' });
    }
    return user;
  }

  function requireAdmin(user) {
    requireUser(user);
    if (!user.admin) {
      throw new HttpError(403, `Only the administrator (${ROOT_USER}) can do this.`, { code: 'not-admin' });
    }
  }

  // A session is for the people it was made for: whoever made it, and
  // whoever they asked in. The administrator sees them all. One made before
  // the server kept this has no one, and is open to every user.
  function canSee(user, session) {
    const info = session.info;
    return user.admin || info === null ||
      info.createdBy === user.username || info.members.includes(user.username);
  }

  // Only whoever made a session, and the administrator, change it.
  function canEdit(user, session) {
    return user.admin || (session.info !== null && session.info.createdBy === user.username);
  }

  function requireMember(user, session) {
    if (!canSee(user, session)) {
      throw new HttpError(403, `The session "${session.name}" is not one you were asked to; ask whoever made it to add you.`, { code: 'not-member' });
    }
  }

  function requireEditor(user, session) {
    if (!canEdit(user, session)) {
      throw new HttpError(403, `Only whoever made the session "${session.name}", or ${ROOT_USER}, can change it.`, { code: 'not-owner' });
    }
  }

  // A name nobody has used yet is the user's who first opens it, as DJV
  // does when someone types a new one: the session is theirs, for them
  // alone until they ask others in on the portal. A name in use is never
  // taken over this way.
  function claim(session, user) {
    if (session.info !== null || store.has(session.name)) {
      return;
    }
    store.putInfo(session.name, {
      description: '',
      date: today(),
      createdBy: user.username,
      members: [user.username],
    });
    session.info = store.info(session.name);
    log(`Session "${session.name}" started by ${user.username}`);
  }

  function requireMediaDir() {
    if (!mediaDir) {
      throw new HttpError(503, 'The server has no media folder (MEDIA_DIR), so it cannot list or take videos.', { code: 'no-media-folder' });
    }
  }

  // Sign a user out everywhere: their tokens go, and their streams are
  // closed, so that their DJVs find out at once rather than at the next
  // change they send.
  function signOutEverywhere(username) {
    store.removeUserTokens(username);
    for (const [res, stream] of streams) {
      if (stream.user === username) {
        res.end();
      }
    }
  }

  async function login(req, res, identity) {
    checkLoginAllowed(identity.ip);
    const body = await readJson(req);
    const username = typeof body?.username === 'string' ? body.username.trim() : '';
    const password = typeof body?.password === 'string' ? body.password : '';
    const user = isValidUsername(username) ? config.user(username) : null;
    const ok = await verifyPassword(password, user?.passwordHash ?? nobodyHash);
    if (!user || !ok) {
      noteLoginFailure(identity.ip);
      log(`Failed sign-in as "${username.slice(0, 64)}" from ${identity.id || '?'} (${identity.ip})`);
      throw new HttpError(401, 'The user name or the password is not right.', { code: 'login-failed' });
    }
    loginFailures.delete(identity.ip);
    const token = newToken();
    const now = Date.now();
    const expires = isoTime(now + TOKEN_DAYS * DAY_MS);
    store.removeExpiredTokens(isoTime(now));
    store.addToken(tokenHash(token), username, identity.id, expires);
    const view = config.userView(username);
    noteUser(view, identity);
    log(`${username} signed in from ${identity.id || '?'} (${identity.ip})`);
    sendJson(res, 200, { token, expires, user: view });
  }

  function configView() {
    return {
      ...config.view(),
      serverVersion: SERVER_VERSION,
      sharePasswordSet: sharePassword !== '',
    };
  }

  // What a DJV is told about the server. Where the media is, and how to
  // sign in to it, only once its user has signed in.
  function hello(user) {
    const out = {
      name: config.name,
      serverVersion: SERVER_VERSION,
      djvVersion: config.djvVersion,
      authRequired: true,
      signedIn: user !== null,
    };
    if (user) {
      out.user = { username: user.username, name: user.name, admin: user.admin };
      out.storage = { unc: config.unc, user: config.shareUser, password: sharePassword };
    }
    return out;
  }

  function adminUsers() {
    const signIns = store.lastSignIns();
    const connected = [...streams.values()];
    return {
      users: config.users.map((user) => {
        const last = seen.get(user.username);
        return {
          ...user,
          lastSignIn: signIns.get(user.username) ?? '',
          lastSeen: last?.lastSeen ?? '',
          pc: last?.pc ?? '',
          ip: last?.ip ?? '',
          version: last?.version ?? '',
          connected: connected.filter((stream) => stream.user === user.username).length,
        };
      }),
    };
  }

  async function setUser(req, res, username) {
    const body = await readJson(req);
    if (!body || typeof body !== 'object' || Array.isArray(body)) {
      throw new HttpError(400, 'The request must be a JSON object.');
    }
    const change = {};
    if (body.name !== undefined) {
      if (typeof body.name !== 'string') {
        throw new HttpError(400, 'The name must be text.');
      }
      change.name = body.name;
    }
    if (body.password !== undefined && body.password !== '') {
      if (username === ROOT_USER) {
        throw new HttpError(400, `The "${ROOT_USER}" password is set in its file on the server (ROOT_PASSWORD_FILE), and the server restarted.`);
      }
      const problem = passwordProblem(body.password);
      if (problem) {
        throw new HttpError(400, problem);
      }
      change.passwordHash = await hashPassword(body.password);
    }
    const isNew = config.user(username) === null;
    const user = config.setUser(username, change);
    if (change.passwordHash && !isNew) {
      // A new password is often because the old one got out.
      signOutEverywhere(username);
    }
    log(isNew ? `User "${username}" added` : `User "${username}" changed${change.passwordHash ? ', with a new password' : ''}`);
    sendJson(res, 200, user);
  }

  function backup(res) {
    const file = path.join(tmpdir(), `djv-sync-backup-${process.pid}-${Date.now()}.db`);
    try {
      store.backup(file);
      const data = readFileSync(file);
      const stamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19);
      res.writeHead(200, {
        'Content-Type': 'application/vnd.sqlite3',
        'Content-Length': data.length,
        'Content-Disposition': `attachment; filename="djv-sync-${stamp}.db"`,
        'Cache-Control': 'no-store',
      });
      res.end(data);
    } finally {
      rmSync(file, { force: true });
    }
  }

  async function handleAdmin(req, res, url, parts, user) {
    requireAdmin(user);
    if (parts.length === 3 && parts[2] === 'config') {
      if (req.method === 'GET') {
        sendJson(res, 200, configView());
        return;
      }
      if (req.method === 'PUT') {
        config.update(await readJson(req));
        log(`Configuration changed by ${user.username}`);
        sendJson(res, 200, configView());
        return;
      }
    }
    if (parts.length === 3 && req.method === 'GET') {
      if (parts[2] === 'users') {
        sendJson(res, 200, adminUsers());
        return;
      }
      if (parts[2] === 'connected') {
        sendJson(res, 200, { connected: [...streams.values()] });
        return;
      }
      if (parts[2] === 'sessions') {
        sendJson(res, 200, { sessions: listSessions(user) });
        return;
      }
      if (parts[2] === 'backup') {
        backup(res);
        return;
      }
    }
    if (parts.length === 4 && parts[2] === 'users') {
      const username = parts[3];
      if (!isValidUsername(username)) {
        throw new HttpError(400, 'A user name can only have letters, digits, ".", "_" and "-", up to 64 of them.');
      }
      if (req.method === 'PUT') {
        await setUser(req, res, username);
        return;
      }
      if (req.method === 'DELETE') {
        if (username === ROOT_USER) {
          throw new HttpError(400, `The "${ROOT_USER}" user cannot be removed.`);
        }
        if (!config.removeUser(username)) {
          throw new HttpError(404, `There is no user "${username}".`);
        }
        signOutEverywhere(username);
        log(`User "${username}" removed`);
        sendJson(res, 200, { ok: true });
        return;
      }
    }
    throw new HttpError(404, 'Not found.');
  }

  // The sessions in use, read from the database the first time they are
  // asked for. They are small, and holding them saves reading every marker
  // again for each client that joins.
  const sessions = new Map();

  function getSession(name) {
    if (!isValidSession(name)) {
      throw new HttpError(400, 'The session name can only have letters, digits, ".", "_" and "-".');
    }
    let session = sessions.get(name);
    if (!session) {
      session = store.load(name);
      sessions.set(name, session);
    }
    return session;
  }

  // What the portal and DJV are told about a session. "media" is the name
  // of its first video, as servers before the portal said.
  function sessionView(session, user) {
    const info = session.info;
    const media = session.media;
    const videos = media ? [media, media.compare].filter(Boolean).map((file) => ({
      path: file.path ?? '',
      pathAbsolute: file.pathAbsolute ?? '',
      name: file.name || path.posix.basename((file.path || file.pathAbsolute || '').replace(/\\/g, '/')),
    })) : [];
    const connected = new Set();
    for (const client of session.clients) {
      const stream = streams.get(client);
      if (stream) {
        connected.add(stream.user);
      }
    }
    return {
      session: session.name,
      rev: session.rev,
      markers: session.markers.size,
      clients: session.clients.size,
      media: media?.name ?? null,
      description: info?.description ?? '',
      date: info?.date ?? '',
      createdBy: info?.createdBy ?? '',
      members: info?.members ?? null,
      open: info === null,
      videos,
      connected: [...connected].sort(),
      canEdit: canEdit(user, session),
      created: info?.created ?? '',
      updated: info?.updated ?? '',
    };
  }

  // The sessions the user can see, the newest first.
  function listSessions(user) {
    const names = new Set([...sessions.keys(), ...store.names()]);
    return [...names].filter(isValidSession)
      .map((name) => getSession(name))
      // A name only looked at, never written to or claimed, is no session.
      .filter((session) => session.info !== null || session.rev > 0)
      .filter((session) => canSee(user, session))
      .map((session) => sessionView(session, user))
      .sort((a, b) => (b.date || '').localeCompare(a.date || '') ||
        (b.created || '').localeCompare(a.created || '') || a.session.localeCompare(b.session));
  }

  // The people a session is for, checked: users the server knows, with
  // whoever made it always among them.
  function checkMembers(value, createdBy) {
    if (value === undefined) {
      return [createdBy];
    }
    if (!Array.isArray(value) || value.length > 500 || value.some((name) => typeof name !== 'string')) {
      throw new HttpError(400, '"members" must be a list of user names.');
    }
    for (const name of value) {
      if (!config.user(name)) {
        throw new HttpError(400, `There is no user "${name}".`);
      }
    }
    return [...new Set([createdBy, ...value])];
  }

  // The session media for one or two videos of the media folder, the
  // first shown as A and the second as B.
  function videosMedia(value, user) {
    requireMediaDir();
    if (!Array.isArray(value) || value.length < 1 || value.length > 2 ||
      value.some((video) => typeof video !== 'string')) {
      throw new HttpError(400, '"videos" must be one or two videos of the media folder.');
    }
    if (value.length === 2 && value[0] === value[1]) {
      throw new HttpError(400, 'The two videos are the same file.');
    }
    const file = (relative) => {
      const problem = pathProblem(relative);
      if (problem) {
        throw new HttpError(400, `${relative}: ${problem}`);
      }
      let isFile = false;
      try {
        isFile = statSync(resolveMedia(mediaDir, relative)).isFile();
      } catch {
        // Not there.
      }
      if (!isFile || !isVideoPath(relative)) {
        throw new HttpError(400, `There is no video "${relative}" in the media folder.`);
      }
      return {
        path: relative,
        pathAbsolute: sharePath(config.unc, relative),
        name: path.posix.basename(relative),
      };
    };
    const media = { ...file(value[0]), sharedBy: user.name || user.username };
    if (value.length === 2) {
      media.compare = file(value[1]);
    }
    return media;
  }

  function sameVideos(a, b) {
    const paths = (media) => (media ? [media.path, media.compare?.path ?? ''] : []);
    return JSON.stringify(paths(a)) === JSON.stringify(paths(b));
  }

  async function createSession(req, res, user) {
    const body = await readObject(req);
    const description = textField(body, 'description', MAX_DESCRIPTION, '');
    const date = checkDate(textField(body, 'date', 10, '') || today());
    const members = checkMembers(body.members, user.username);
    const media = videosMedia(body.videos, user);
    let name;
    do {
      name = sessionName(date, description);
    } while (store.has(name) || sessions.has(name));
    store.putInfo(name, { description, date, createdBy: user.username, members });
    const session = getSession(name);
    session.apply({ type: 'media', clientId: '', user: user.username, media }, store);
    log(`Session "${name}" made by ${user.username}, for ${members.join(', ')}`);
    sendJson(res, 200, sessionView(session, user));
  }

  async function changeSession(req, res, session, user) {
    requireEditor(user, session);
    const body = await readObject(req);
    const info = session.info ?? {
      description: '', date: today(), createdBy: user.username, members: [user.username],
    };
    const changed = {
      description: textField(body, 'description', MAX_DESCRIPTION, info.description),
      date: body.date === undefined ? info.date : checkDate(textField(body, 'date', 10, '')),
      createdBy: info.createdBy,
      members: body.members === undefined ? info.members : checkMembers(body.members, info.createdBy),
      created: info.created,
    };
    // The videos first: a change they cannot have changes nothing.
    const media = body.videos === undefined ? null : videosMedia(body.videos, user);
    store.putInfo(session.name, changed);
    session.info = store.info(session.name);
    if (media && !sameVideos(media, session.media)) {
      session.apply({ type: 'media', clientId: '', user: user.username, media }, store);
    }
    // Whoever is no longer asked in is no longer told about the changes.
    for (const client of [...session.clients]) {
      const stream = streams.get(client);
      if (stream && !canSee(config.userView(stream.user) ?? { username: stream.user }, session)) {
        writeEvent(client, { type: 'removed', rev: session.rev });
        client.end();
      }
    }
    log(`Session "${session.name}" changed by ${user.username}`);
    sendJson(res, 200, sessionView(session, user));
  }

  function removeSession(res, session, user) {
    requireEditor(user, session);
    // The DJVs in it are told, so that they leave it rather than put it
    // back with their next note.
    for (const client of [...session.clients]) {
      writeEvent(client, { type: 'removed', rev: session.rev });
      client.end();
    }
    session.clients.clear();
    store.removeSession(session.name);
    sessions.delete(session.name);
    log(`Session "${session.name}" removed by ${user.username}`);
    sendJson(res, 200, { ok: true });
  }

  // Uploads. Each piece is written where it goes in the unfinished file,
  // and the file is moved into place once the last one is in.
  const uploadsBusy = new Set();

  function partFile(id) {
    return path.join(mediaDir, UPLOAD_DIR, `${id}.part`);
  }

  function uploadView(upload) {
    return {
      id: upload.id,
      path: upload.path,
      size: upload.size,
      received: upload.received,
      chunkSize: uploadChunk,
      username: upload.username,
      created: upload.created,
      updated: upload.updated,
    };
  }

  // Whether the folder already has something of that name. Windows sees
  // names in any case as the same, and so does a folder of a Windows host
  // mounted here, but the server cannot count on the mount saying so.
  function nameTaken(relative) {
    const target = resolveMedia(mediaDir, relative);
    if (existsSync(target)) {
      return true;
    }
    try {
      const name = path.basename(target).toLowerCase();
      return readdirSync(path.dirname(target)).some((entry) => entry.toLowerCase() === name);
    } catch {
      return false;
    }
  }

  function requireUploader(upload, user) {
    if (!upload) {
      throw new HttpError(404, 'There is no such upload; it was finished, cancelled or dropped.', { code: 'no-upload' });
    }
    if (upload.username !== user.username && !user.admin) {
      throw new HttpError(403, 'The upload is someone else\'s.');
    }
  }

  async function startUpload(req, res, user) {
    requireMediaDir();
    const body = await readObject(req);
    const relative = typeof body.path === 'string' ? body.path.trim() : '';
    const problem = pathProblem(relative);
    if (problem) {
      throw new HttpError(400, problem);
    }
    if (!isVideoPath(relative)) {
      throw new HttpError(400, 'Only videos can be uploaded: .mov, .mp4, .m4v, .mxf, .mkv, .avi, .webm, .mpg, .mpeg, .ts, .y4m and .wav files.');
    }
    const size = body.size;
    if (!Number.isSafeInteger(size) || size <= 0 || size > MAX_UPLOAD) {
      throw new HttpError(400, 'The size of the file must be given, in bytes.');
    }
    if (nameTaken(relative)) {
      throw new HttpError(409, `There is already a file "${relative}" in the media folder.`, { code: 'exists' });
    }
    const existing = store.uploadFor(relative);
    if (existing) {
      // The same file again, from the same person: carry on where it was.
      if (existing.username === user.username && existing.size === size && existsSync(partFile(existing.id))) {
        sendJson(res, 200, uploadView(existing));
        return;
      }
      if (existing.username !== user.username) {
        throw new HttpError(409, `${existing.username} is uploading a file "${relative}" already.`, { code: 'uploading' });
      }
      // Another file of the name, or one whose piece is gone: start again.
      await rm(partFile(existing.id), { force: true });
      store.removeUpload(existing.id);
    }
    const free = freeSpace(mediaDir);
    if (free !== null && size > free) {
      throw new HttpError(507, `The media folder has ${Math.floor(free / 1024 ** 3)} GB free; the file needs ${Math.ceil(size / 1024 ** 3)} GB.`, { code: 'no-space' });
    }
    const id = randomBytes(12).toString('hex');
    await mkdir(path.join(mediaDir, UPLOAD_DIR), { recursive: true });
    await (await open(partFile(id), 'w')).close();
    store.addUpload(id, user.username, relative, size);
    log(`${user.username} is uploading "${relative}" (${size} bytes)`);
    sendJson(res, 200, uploadView(store.upload(id)));
  }

  async function receivePiece(req, res, url, user, id) {
    const upload = store.upload(id);
    requireUploader(upload, user);
    const offset = Number(url.searchParams.get('offset'));
    if (offset !== upload.received) {
      throw new HttpError(409, `The upload has ${upload.received} bytes; send the piece from there.`, { code: 'offset', received: upload.received });
    }
    if (uploadsBusy.has(id)) {
      throw new HttpError(409, 'A piece of the upload is already on its way.', { code: 'busy', received: upload.received });
    }
    const length = Number(req.headers['content-length']);
    if (!Number.isSafeInteger(length) || length <= 0) {
      throw new HttpError(411, 'A piece must say its length (Content-Length).');
    }
    if (length > uploadChunk || offset + length > upload.size) {
      throw new HttpError(413, `A piece can be up to ${uploadChunk} bytes, and not go past the end of the file.`);
    }
    uploadsBusy.add(id);
    let file = null;
    try {
      file = await open(partFile(id), 'r+');
      let written = 0;
      try {
        for await (const chunk of req) {
          if (written + chunk.length > length) {
            throw new HttpError(413, 'The piece is longer than it said.');
          }
          await file.write(chunk, 0, chunk.length, offset + written);
          written += chunk.length;
        }
      } catch (error) {
        if (error instanceof HttpError) {
          throw error;
        }
        written = -1;
      }
      if (written !== length) {
        // Cut off on the way: the piece is sent again, over what came.
        throw new HttpError(400, 'The piece did not arrive whole; send it again.', { code: 'incomplete', received: upload.received });
      }
      const received = offset + length;
      if (received === upload.size) {
        await file.sync();
      }
      await file.close();
      file = null;
      store.setReceived(id, received);
      if (received < upload.size) {
        sendJson(res, 200, { received, done: false });
        return;
      }
      const video = await finishUpload({ ...upload, received });
      sendJson(res, 200, { received, done: true, video });
    } finally {
      uploadsBusy.delete(id);
      await file?.close().catch(() => {});
    }
  }

  // Move a whole upload into place, never over a file that is there: a
  // link fails if the name is taken, where a rename would replace it. A
  // file system without links gets a rename after a look.
  async function finishUpload(upload) {
    const relative = upload.path;
    const target = resolveMedia(mediaDir, relative);
    const part = partFile(upload.id);
    const exists = () => new HttpError(409, `A file "${relative}" was put in the media folder while this one was uploading; cancel this upload, and upload it under another name.`, { code: 'exists' });
    if (nameTaken(relative)) {
      throw exists();
    }
    await mkdir(path.dirname(target), { recursive: true });
    try {
      await link(part, target);
      await unlink(part);
    } catch (error) {
      if (error.code === 'EEXIST') {
        throw exists();
      }
      if (existsSync(target)) {
        throw exists();
      }
      await rename(part, target);
    }
    store.removeUpload(upload.id);
    log(`${upload.username} uploaded "${relative}"`);
    const stat = statSync(target);
    return {
      path: relative,
      name: path.posix.basename(relative),
      folder: path.posix.dirname(relative) === '.' ? '' : path.posix.dirname(relative),
      size: stat.size,
      modified: stat.mtime.toISOString(),
    };
  }

  async function cancelUpload(res, user, id) {
    const upload = store.upload(id);
    requireUploader(upload, user);
    await rm(partFile(id), { force: true });
    store.removeUpload(id);
    log(`${user.username} cancelled the upload of "${upload.path}"`);
    sendJson(res, 200, { ok: true });
  }

  function videos() {
    if (!mediaDir) {
      return { enabled: false, videos: [], folders: [], free: null, uploading: [] };
    }
    return {
      enabled: true,
      ...listMedia(mediaDir),
      free: freeSpace(mediaDir),
      uploading: store.uploads().map((upload) => ({
        path: upload.path, username: upload.username, size: upload.size, received: upload.received,
      })),
    };
  }

  async function removeVideo(res, url, user) {
    requireAdmin(user);
    requireMediaDir();
    const relative = url.searchParams.get('path') ?? '';
    const problem = pathProblem(relative);
    if (problem) {
      throw new HttpError(400, problem);
    }
    const target = resolveMedia(mediaDir, relative);
    let isFile = false;
    try {
      isFile = statSync(target).isFile();
    } catch {
      // Not there.
    }
    if (!isFile) {
      throw new HttpError(404, `There is no file "${relative}" in the media folder.`);
    }
    const using = store.allMedia()
      .filter(({ media }) => media.path === relative || media.compare?.path === relative)
      .map(({ name }) => name);
    if (using.length > 0 && url.searchParams.get('force') !== '1') {
      throw new HttpError(409, `The sessions ${using.join(', ')} use "${relative}".`, { code: 'in-use', sessions: using });
    }
    await unlink(target);
    log(`${user.username} deleted "${relative}"`);
    sendJson(res, 200, { ok: true });
  }

  function openStream(req, res, session, since, user, identity) {
    res.writeHead(200, {
      'Content-Type': 'text/event-stream; charset=utf-8',
      'Cache-Control': 'no-cache',
      Connection: 'keep-alive',
      // A proxy that buffers would hold the changes back.
      'X-Accel-Buffering': 'no',
    });
    res.write(': djv sync\n\n');
    const missed = session.since(since);
    if (missed === null) {
      // The changes since then are no longer kept: read the state again.
      writeEvent(res, { type: 'reset', rev: session.rev });
      res.end();
      return;
    }
    for (const event of missed) {
      writeEvent(res, event);
    }
    session.clients.add(res);
    streams.set(res, {
      user: user.username,
      pc: identity.id,
      session: session.name,
      ip: identity.ip,
      version: identity.version,
      since: new Date().toISOString(),
    });
    const heartbeat = setInterval(() => res.write(': ping\n\n'), HEARTBEAT_MS);
    const close = () => {
      clearInterval(heartbeat);
      session.clients.delete(res);
      streams.delete(res);
    };
    req.on('close', close);
    res.on('error', close);
  }

  async function handle(req, res) {
    const url = new URL(req.url, 'http://localhost');
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Headers', 'Authorization, Content-Type, X-DJV-Client, X-DJV-Version');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, PUT, DELETE, OPTIONS');
    if (req.method === 'OPTIONS') {
      res.writeHead(204);
      res.end();
      return;
    }

    const parts = url.pathname.split('/').filter(Boolean).map(decodeURIComponent);
    // The pages hold nothing of the server's: what they show, they ask the
    // requests for, which are where the user is checked. The portal is what
    // the server's address opens.
    const page = parts.length === 0 ? 'portal' : parts.length === 1 ? parts[0] : '';
    if ((page === 'portal' || page === 'admin') && req.method === 'GET') {
      const html = readFileSync(path.join(SERVER_DIR, `${page}.html`));
      res.writeHead(200, {
        'Content-Type': 'text/html; charset=utf-8',
        'Content-Length': html.length,
        'Cache-Control': 'no-cache',
      });
      res.end(html);
      return;
    }
    if (parts[0] !== 'api') {
      throw new HttpError(404, 'Not found.');
    }
    if (parts.length === 2 && parts[1] === 'health' && req.method === 'GET') {
      sendJson(res, 200, { ok: true });
      return;
    }
    const identity = identify(req);
    if (parts.length === 2 && parts[1] === 'login' && req.method === 'POST') {
      await login(req, res, identity);
      return;
    }
    const user = authenticate(req);
    if (user) {
      noteUser(user, identity);
    }
    // Hello answers anyone, so that DJV can tell a server that wants it to
    // sign in from one it cannot reach; what it says depends on whether it
    // has.
    if (parts.length === 2 && parts[1] === 'hello' && req.method === 'GET') {
      sendJson(res, 200, hello(user));
      return;
    }
    if (parts.length === 2 && parts[1] === 'logout' && req.method === 'POST') {
      if (user) {
        store.removeToken(user.tokenHash);
        log(`${user.username} signed out from ${identity.id || '?'} (${identity.ip})`);
      }
      sendJson(res, 200, { ok: true });
      return;
    }
    if (parts[1] === 'admin') {
      await handleAdmin(req, res, url, parts, user);
      return;
    }
    requireUser(user);

    if (parts.length === 2 && parts[1] === 'people' && req.method === 'GET') {
      sendJson(res, 200, {
        people: config.users.map((person) => ({ username: person.username, name: person.name })),
      });
      return;
    }
    if (parts.length === 2 && parts[1] === 'videos') {
      if (req.method === 'GET') {
        sendJson(res, 200, videos());
        return;
      }
      if (req.method === 'DELETE') {
        await removeVideo(res, url, user);
        return;
      }
    }
    if (parts[1] === 'uploads') {
      if (parts.length === 2 && req.method === 'GET') {
        requireMediaDir();
        const mine = store.uploads().filter((upload) => user.admin || upload.username === user.username);
        sendJson(res, 200, { uploads: mine.map(uploadView) });
        return;
      }
      if (parts.length === 2 && req.method === 'POST') {
        await startUpload(req, res, user);
        return;
      }
      if (parts.length === 3 && req.method === 'PUT') {
        requireMediaDir();
        await receivePiece(req, res, url, user, parts[2]);
        return;
      }
      if (parts.length === 3 && req.method === 'DELETE') {
        requireMediaDir();
        await cancelUpload(res, user, parts[2]);
        return;
      }
      throw new HttpError(404, 'Not found.');
    }

    if (parts.length === 2 && parts[1] === 'sessions') {
      if (req.method === 'GET') {
        sendJson(res, 200, { sessions: listSessions(user) });
        return;
      }
      if (req.method === 'POST') {
        await createSession(req, res, user);
        return;
      }
    }
    if (parts[1] !== 'sessions' || parts.length < 3) {
      throw new HttpError(404, 'Not found.');
    }
    const session = getSession(parts[2]);

    // Changing or removing a session is for one that is there.
    const changing = (parts.length === 3 && req.method === 'DELETE') ||
      (parts.length === 4 && parts[3] === 'info' && req.method === 'PUT');
    if (changing) {
      if (!store.has(session.name)) {
        throw new HttpError(404, `There is no session "${session.name}".`);
      }
      if (parts.length === 3) {
        removeSession(res, session, user);
      } else {
        await changeSession(req, res, session, user);
      }
      return;
    }
    claim(session, user);
    requireMember(user, session);

    if (parts.length === 3 && req.method === 'GET') {
      sendJson(res, 200, session.snapshot());
      return;
    }

    if (parts.length === 4 && parts[3] === 'events' && req.method === 'GET') {
      const since = Number.parseInt(url.searchParams.get('since') ?? '0', 10);
      openStream(req, res, session, Number.isFinite(since) ? since : 0, user, identity);
      return;
    }

    if (parts.length === 4 && parts[3] === 'media') {
      if (req.method === 'PUT') {
        const body = await readJson(req);
        const event = session.apply({
          type: 'media',
          clientId: typeof body?.clientId === 'string' ? body.clientId : '',
          user: user.username,
          media: checkMedia(body?.media),
        }, store);
        sendJson(res, 200, { rev: event.rev });
        return;
      }
      if (req.method === 'DELETE') {
        const event = session.apply({
          type: 'media',
          clientId: url.searchParams.get('clientId') ?? '',
          user: user.username,
          media: null,
        }, store);
        sendJson(res, 200, { rev: event.rev });
        return;
      }
    }

    if (parts.length === 5 && parts[3] === 'markers') {
      const id = parts[4];
      if (!MARKER_ID_PATTERN.test(id)) {
        throw new HttpError(400, 'The marker identifier can only have letters, digits, ".", "_" and "-".');
      }
      if (req.method === 'PUT') {
        const body = await readJson(req);
        const marker = body?.marker;
        if (!marker || typeof marker !== 'object' || Array.isArray(marker)) {
          throw new HttpError(400, 'The request has no marker.');
        }
        if (marker.id !== undefined && marker.id !== id) {
          throw new HttpError(400, 'The marker identifier does not match the address.');
        }
        const event = session.apply({
          type: 'upsert',
          clientId: typeof body.clientId === 'string' ? body.clientId : '',
          user: user.username,
          marker: { ...marker, id },
        }, store);
        sendJson(res, 200, { rev: event.rev });
        return;
      }
      if (req.method === 'DELETE') {
        // Deleting what is not there is still a change: the client that
        // asked may be the only one that had it.
        const event = session.apply({
          type: 'delete',
          clientId: url.searchParams.get('clientId') ?? '',
          user: user.username,
          id,
        }, store);
        sendJson(res, 200, { rev: event.rev });
        return;
      }
    }
    throw new HttpError(404, 'Not found.');
  }

  const server = createHttpServer((req, res) => {
    handle(req, res).catch((error) => {
      let status = 500;
      if (error instanceof HttpError) {
        status = error.status;
      } else if (error instanceof ConfigError) {
        status = 400;
      }
      if (status === 500) {
        log(`${req.method} ${req.url}: ${error.stack ?? error}`);
      }
      if (!res.headersSent) {
        sendJson(res, status, { error: error.message, ...(error.extra ?? {}) });
      } else {
        res.end();
      }
    });
  });

  const close = server.close.bind(server);
  server.close = (callback) => {
    for (const session of sessions.values()) {
      for (const client of session.clients) {
        client.end();
      }
      session.clients.clear();
    }
    // Every change is already written; the database is closed once no
    // request can still be writing one.
    close((error) => {
      store.close();
      callback?.(error);
    });
    // Idle keep-alive connections would otherwise hold the close up until
    // they time out.
    server.closeAllConnections();
    return server;
  };
  server.config = config;
  return server;
}

// Drop the uploads nobody carried on with for a week, and any unfinished
// file the database knows nothing of.
function sweepUploads(store, mediaDir, log) {
  const dir = path.join(mediaDir, UPLOAD_DIR);
  const known = new Set();
  const old = Date.now() - UPLOAD_KEEP_MS;
  for (const upload of store.uploads()) {
    if (Date.parse(upload.updated) < old) {
      rmSync(path.join(dir, `${upload.id}.part`), { force: true });
      store.removeUpload(upload.id);
      log(`Dropped the unfinished upload of "${upload.path}" by ${upload.username}`);
    } else {
      known.add(`${upload.id}.part`);
    }
  }
  let files = [];
  try {
    files = readdirSync(dir);
  } catch {
    return;
  }
  for (const file of files) {
    if (!known.has(file)) {
      rmSync(path.join(dir, file), { force: true, recursive: true });
    }
  }
}

// A secret, from the file NAME_FILE names -- a Docker secret, say -- or from
// NAME itself. A file written on Windows may start with a byte order mark
// and end with a line break, neither of which is part of the password.
function secret(name, log) {
  const file = process.env[`${name}_FILE`];
  if (file) {
    try {
      return readFileSync(file, 'utf8').replace(/^﻿/, '').replace(/[\r\n]+$/, '');
    } catch (error) {
      if (error.code !== 'ENOENT') {
        throw new ConfigError(`Cannot read ${name}_FILE (${file}): ${error.message}`);
      }
      log(`${name}_FILE (${file}) is not there`);
      return '';
    }
  }
  return process.env[name] ?? '';
}

function main() {
  const port = Number.parseInt(process.env.PORT ?? '8877', 10);
  // This machine only, unless asked: in a container, HOST is 0.0.0.0 and
  // the port is published to the network instead.
  const host = process.env.HOST ?? '127.0.0.1';
  const dataDir = process.env.DATA_DIR ?? path.join(SERVER_DIR, 'data');
  const dbFile = process.env.DB_FILE || undefined;
  const configFile = process.env.CONFIG_FILE || path.join(SERVER_DIR, 'config.json');
  const exclusive = ['1', 'true', 'yes'].includes((process.env.DB_EXCLUSIVE ?? '').toLowerCase());
  const mediaDir = process.env.MEDIA_DIR ?? '';
  const log = (message) => console.log(`${new Date().toISOString()} ${message}`);
  let server;
  let sharePassword;
  try {
    const rootPassword = secret('ROOT_PASSWORD', log);
    sharePassword = secret('SHARE_PASSWORD', log);
    if (mediaDir && !existsSync(mediaDir)) {
      throw new ConfigError(`The media folder MEDIA_DIR (${mediaDir}) is not there.`);
    }
    server = createSyncServer({ dataDir, dbFile, configFile, rootPassword, sharePassword, exclusive, mediaDir, log });
  } catch (error) {
    // A configuration that cannot be read stops the server rather than
    // start it with none: it would have no users, and no administrator.
    console.error(error.message);
    process.exit(1);
  }
  server.listen(port, host, () => {
    log(`DJV sync server ${SERVER_VERSION} on http://${host}:${port}, sessions in ${dbFile ?? path.join(dataDir, 'djv-sync.db')}${exclusive ? ' (exclusive)' : ''}`);
    log(`Configuration in ${configFile}; portal at http://${host}:${port}/, admin page at /admin, as "${ROOT_USER}"`);
    log(mediaDir ? `Videos in ${mediaDir}` : 'No media folder (MEDIA_DIR): the portal cannot list or upload videos.');

    if (!server.config.unc) {
      log('No storage share is set; DJV clients will not map a drive. Set it on the admin page.');
    } else if (!sharePassword) {
      log('No share password (SHARE_PASSWORD_FILE): DJV clients will map the share with the credentials Windows has for it.');
    }
  });
  const stop = () => {
    log('Stopping');
    server.close(() => process.exit(0));
  };
  process.on('SIGINT', stop);
  process.on('SIGTERM', stop);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  main();
}
