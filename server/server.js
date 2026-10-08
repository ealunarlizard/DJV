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
// Nothing but Node: a server for a handful of people reviewing together
// should start with "node server.js". Node has SQLite built in from 22.13.
// See README.md for the protocol.

import { createServer as createHttpServer } from 'node:http';
import { mkdirSync, readFileSync, readdirSync, renameSync } from 'node:fs';
import { timingSafeEqual } from 'node:crypto';
import { DatabaseSync } from 'node:sqlite';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

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

// The longest path the media of a session can have. Paths on a network
// share are long, but not this long; anything longer is not a path.
const MAX_MEDIA_PATH = 4096;

// The stream is kept open with a comment this often, which also tells a
// client that a silent stream has gone. DJV waits three times as long.
const HEARTBEAT_MS = 15000;

// The layout of the database, numbered so that a later server can tell an
// older file and bring it up to date.
const SCHEMA_VERSION = 1;

class HttpError extends Error {
  constructor(status, message) {
    super(message);
    this.status = status;
  }
}

function isValidSession(name) {
  return SESSION_PATTERN.test(name) && name !== '.' && name !== '..';
}

// The media of a session as a client sent it, checked. The paths are only
// passed on: the server never opens them, and need not be able to reach the
// network storage they are on. Fields the server does not know are kept, so
// a newer DJV can say more about the media without the server's help.
function checkMedia(media) {
  if (!media || typeof media !== 'object' || Array.isArray(media)) {
    throw new HttpError(400, 'The request has no media.');
  }
  const out = { ...media };
  for (const key of ['path', 'pathAbsolute', 'name', 'sharedBy']) {
    const value = media[key] ?? '';
    if (typeof value !== 'string' || value.length > MAX_MEDIA_PATH) {
      throw new HttpError(400, `The media's "${key}" must be text of up to ${MAX_MEDIA_PATH} characters.`);
    }
    out[key] = value;
  }
  if (!out.path && !out.pathAbsolute) {
    throw new HttpError(400, 'The media has no path.');
  }
  return out;
}

// The sessions on disk. Each change is one transaction: the session's
// revision, the marker or media it changed, and the change itself for the
// clients catching up, all written together or not at all.
class Store {
  constructor(file) {
    this.db = new DatabaseSync(file);
    // Write-ahead logging lets the snapshot a client reads not wait on a
    // change being written, and a commit costs one write rather than two.
    // With it, NORMAL is still safe against the server crashing; only the
    // machine losing power could lose the very last changes.
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
      PRAGMA user_version = ${SCHEMA_VERSION};
    `);
    const db = this.db;
    this.statements = {
      session: db.prepare('SELECT rev, media FROM sessions WHERE name = ?'),
      sessionNames: db.prepare('SELECT name FROM sessions ORDER BY name'),
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
    };
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
    return this.statements.session.get(name) !== undefined;
  }

  names() {
    return this.statements.sessionNames.all().map((row) => row.name);
  }

  // The session as it was last written, or an empty one when it never was.
  load(name) {
    const session = new Session(name);
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

function tokenMatches(given, expected) {
  const a = Buffer.from(given ?? '');
  const b = Buffer.from(expected);
  return a.length === b.length && timingSafeEqual(a, b);
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
      store.import(name, JSON.parse(readFileSync(filePath, 'utf8')));
      renameSync(filePath, `${filePath}.imported`);
      log(`Imported session "${name}" from ${file}`);
    } catch (error) {
      log(`Cannot import session "${name}" from ${file}: ${error.message}`);
    }
  }
}

export function createSyncServer(options = {}) {
  const dataDir = options.dataDir ?? path.join(process.cwd(), 'data');
  const dbFile = options.dbFile ?? path.join(dataDir, 'djv-sync.db');
  const token = options.token ?? '';
  const log = options.log ?? (() => {});

  mkdirSync(path.dirname(dbFile), { recursive: true });
  const store = new Store(dbFile);
  importJsonSessions(store, dataDir, log);

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

  function checkToken(req, url) {
    if (!token) {
      return;
    }
    const header = req.headers.authorization ?? '';
    const given = header.startsWith('Bearer ') ? header.slice(7) : url.searchParams.get('token');
    if (!given) {
      throw new HttpError(401, 'A token is needed.');
    }
    if (!tokenMatches(given, token)) {
      throw new HttpError(403, 'The token is not right.');
    }
  }

  function listSessions() {
    const names = new Set([...sessions.keys(), ...store.names()]);
    return [...names].filter(isValidSession).sort().map((name) => {
      const session = getSession(name);
      return {
        session: name,
        rev: session.rev,
        markers: session.markers.size,
        clients: session.clients.size,
        media: session.media?.name ?? null,
      };
    });
  }

  function openStream(req, res, session, since) {
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
    const heartbeat = setInterval(() => res.write(': ping\n\n'), HEARTBEAT_MS);
    const close = () => {
      clearInterval(heartbeat);
      session.clients.delete(res);
    };
    req.on('close', close);
    res.on('error', close);
  }

  async function handle(req, res) {
    const url = new URL(req.url, 'http://localhost');
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Headers', 'Authorization, Content-Type');
    res.setHeader('Access-Control-Allow-Methods', 'GET, PUT, DELETE, OPTIONS');
    if (req.method === 'OPTIONS') {
      res.writeHead(204);
      res.end();
      return;
    }

    const parts = url.pathname.split('/').filter(Boolean).map(decodeURIComponent);
    if (parts[0] !== 'api') {
      throw new HttpError(404, 'Not found.');
    }
    if (parts.length === 2 && parts[1] === 'health' && req.method === 'GET') {
      sendJson(res, 200, { ok: true });
      return;
    }
    checkToken(req, url);

    if (parts.length === 2 && parts[1] === 'sessions' && req.method === 'GET') {
      sendJson(res, 200, { sessions: listSessions() });
      return;
    }
    if (parts[1] !== 'sessions' || parts.length < 3) {
      throw new HttpError(404, 'Not found.');
    }
    const session = getSession(parts[2]);

    if (parts.length === 3 && req.method === 'GET') {
      sendJson(res, 200, session.snapshot());
      return;
    }

    if (parts.length === 4 && parts[3] === 'events' && req.method === 'GET') {
      const since = Number.parseInt(url.searchParams.get('since') ?? '0', 10);
      openStream(req, res, session, Number.isFinite(since) ? since : 0);
      return;
    }

    if (parts.length === 4 && parts[3] === 'media') {
      if (req.method === 'PUT') {
        const body = await readJson(req);
        const event = session.apply({
          type: 'media',
          clientId: typeof body?.clientId === 'string' ? body.clientId : '',
          media: checkMedia(body?.media),
        }, store);
        sendJson(res, 200, { rev: event.rev });
        return;
      }
      if (req.method === 'DELETE') {
        const event = session.apply({
          type: 'media',
          clientId: url.searchParams.get('clientId') ?? '',
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
      const status = error instanceof HttpError ? error.status : 500;
      if (status === 500) {
        log(`${req.method} ${req.url}: ${error.stack ?? error}`);
      }
      if (!res.headersSent) {
        sendJson(res, status, { error: error.message });
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
  return server;
}

function main() {
  const port = Number.parseInt(process.env.PORT ?? '8787', 10);
  // This machine only, unless asked: there are no accounts, and a token is
  // the only lock there is.
  const host = process.env.HOST ?? '127.0.0.1';
  const dataDir = process.env.DATA_DIR ?? path.join(path.dirname(fileURLToPath(import.meta.url)), 'data');
  const dbFile = process.env.DB_FILE || undefined;
  const token = process.env.DJV_SYNC_TOKEN ?? '';
  const log = (message) => console.log(`${new Date().toISOString()} ${message}`);
  const server = createSyncServer({ dataDir, dbFile, token, log });
  server.listen(port, host, () => {
    log(`DJV sync server on http://${host}:${port}, sessions in ${dbFile ?? path.join(dataDir, 'djv-sync.db')}${token ? ', token required' : ''}`);
    if (host !== '127.0.0.1' && host !== 'localhost' && !token) {
      log('Warning: listening beyond this machine without a token; set DJV_SYNC_TOKEN.');
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
