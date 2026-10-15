// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

// The server's configuration: what it tells DJV about itself and the media
// share, and the people who may sign in.
//
// It is one JSON file, so that it can be read and edited by hand on the
// server as easily as through the admin page, which writes the same file.
// The sessions themselves are not here: they are in the database, and they
// change with every note, where this changes when the admin says so. Nor
// are the secrets: the passwords are kept hashed, and the share's password
// is read from a file of its own when the server starts (see server.js).

import { readFileSync, renameSync, writeFileSync, mkdirSync } from 'node:fs';
import path from 'node:path';

import { ROOT_USER, isPasswordHash, isValidUsername } from './users.js';

const MAX_NAME = 200;
const MAX_UNC = 1024;
const MAX_VERSION = 64;
const MAX_SHARE_USER = 256;

export class ConfigError extends Error {}

function defaults() {
  return {
    name: 'DJV sync server',
    // The account DJV maps the share with. "djv" is the one the share setup
    // script makes (see docs/lan/share-setup.md).
    storage: { unc: '', user: 'djv' },
    djvVersion: '',
    users: [],
  };
}

function checkText(value, what, max) {
  if (typeof value !== 'string' || value.length > max) {
    throw new ConfigError(`${what} must be text of up to ${max} characters.`);
  }
  return value.trim();
}

// The share is given as Windows will be asked to map it: a UNC path, with
// either kind of slash. Anything else could not be mapped to a drive.
function checkUnc(value) {
  const unc = checkText(value, 'The storage share', MAX_UNC).replaceAll('/', '\\');
  if (unc && !/^\\\\[^\\]+\\[^\\]+/.test(unc)) {
    throw new ConfigError('The storage share must be a UNC path, e.g. \\\\192.168.0.100\\media.');
  }
  return unc.replace(/\\+$/, '');
}

// A Windows account, alone or with the machine or domain it belongs to:
// "djv", or "192.168.0.100\djv".
function checkShareUser(value) {
  const user = checkText(value, 'The share user', MAX_SHARE_USER);
  if (/["/[\]:;|=,+*?<>]/.test(user)) {
    throw new ConfigError('The share user cannot have any of " / [ ] : ; | = , + * ? < >.');
  }
  return user;
}

function checkUser(user) {
  if (!user || typeof user !== 'object' || !isValidUsername(user.username)) {
    throw new ConfigError('A user name can only have letters, digits, ".", "_" and "-", up to 64 of them.');
  }
  if (!isPasswordHash(user.passwordHash)) {
    throw new ConfigError(`The user "${user.username}" has no password hash.`);
  }
  const now = new Date().toISOString();
  return {
    username: user.username,
    name: checkText(user.name ?? '', 'A user\'s name', MAX_NAME),
    passwordHash: user.passwordHash,
    created: typeof user.created === 'string' ? user.created : now,
    updated: typeof user.updated === 'string' ? user.updated : now,
  };
}

// What anyone but the user is shown of one: never the hash.
function userView(user) {
  return {
    username: user.username,
    name: user.name,
    admin: user.username === ROOT_USER,
    created: user.created,
    updated: user.updated,
  };
}

export class ServerConfig {
  // With no file, the configuration lives in memory only: a test, or a server
  // run without one.
  constructor(file = null, initial = {}) {
    this.file = file;
    this.data = defaults();
    this.apply(initial, { allowUsers: true });
  }

  static load(file) {
    let json = {};
    try {
      json = JSON.parse(readFileSync(file, 'utf8'));
    } catch (error) {
      if (error.code !== 'ENOENT') {
        throw new ConfigError(`Cannot read the configuration ${file}: ${error.message}`);
      }
    }
    return new ServerConfig(file, json);
  }

  // Take the fields given, checked; the ones not given are left as they are,
  // and the ones not known -- the client registry and the admin token of
  // the first LAN server among them -- are left out, and so dropped when the
  // file is next written. The users are only taken from the file: the admin
  // page has its own requests for them.
  apply(json, { allowUsers = false } = {}) {
    if (!json || typeof json !== 'object' || Array.isArray(json)) {
      throw new ConfigError('The configuration must be a JSON object.');
    }
    const next = structuredClone(this.data);
    if (json.name !== undefined) {
      next.name = checkText(json.name, 'The server name', MAX_NAME);
    }
    if (json.storage !== undefined) {
      if (!json.storage || typeof json.storage !== 'object') {
        throw new ConfigError('The storage must be an object with a "unc" path.');
      }
      if (json.storage.unc !== undefined) {
        next.storage.unc = checkUnc(json.storage.unc);
      }
      if (json.storage.user !== undefined) {
        next.storage.user = checkShareUser(json.storage.user);
      }
    }
    if (json.djvVersion !== undefined) {
      next.djvVersion = checkText(json.djvVersion, 'The DJV version', MAX_VERSION);
    }
    if (allowUsers && json.users !== undefined) {
      if (!Array.isArray(json.users)) {
        throw new ConfigError('"users" must be a list.');
      }
      const seen = new Set();
      next.users = [];
      for (const item of json.users) {
        const user = checkUser(item);
        if (!seen.has(user.username)) {
          seen.add(user.username);
          next.users.push(user);
        }
      }
    }
    this.data = next;
  }

  get name() {
    return this.data.name;
  }

  get unc() {
    return this.data.storage.unc;
  }

  get shareUser() {
    return this.data.storage.user;
  }

  get djvVersion() {
    return this.data.djvVersion;
  }

  get users() {
    return this.data.users.map(userView);
  }

  // The user with the hash, for checking a password; null when there is no
  // such user.
  user(username) {
    return this.data.users.find((user) => user.username === username) ?? null;
  }

  userView(username) {
    const user = this.user(username);
    return user ? userView(user) : null;
  }

  // What the admin page is shown.
  view() {
    return {
      name: this.data.name,
      storage: { ...this.data.storage },
      djvVersion: this.data.djvVersion,
      file: this.file,
    };
  }

  update(json) {
    this.apply(json);
    this.save();
  }

  // Add a user or change one. The hash is made by the caller, which can take
  // its time over it without holding up the configuration.
  setUser(username, { name, passwordHash } = {}) {
    const current = this.user(username);
    if (!current && passwordHash === undefined) {
      throw new ConfigError('A new user needs a password.');
    }
    const user = checkUser({
      username,
      name: name ?? current?.name ?? '',
      passwordHash: passwordHash ?? current.passwordHash,
      created: current?.created,
    });
    const index = this.data.users.findIndex((item) => item.username === username);
    if (index >= 0) {
      this.data.users[index] = user;
    } else {
      this.data.users.push(user);
    }
    this.save();
    return userView(user);
  }

  removeUser(username) {
    const before = this.data.users.length;
    this.data.users = this.data.users.filter((user) => user.username !== username);
    if (this.data.users.length !== before) {
      this.save();
      return true;
    }
    return false;
  }

  // Written beside, then renamed over, so that a crash part way leaves the
  // last good file rather than half of a new one.
  save() {
    if (!this.file) {
      return;
    }
    mkdirSync(path.dirname(this.file), { recursive: true });
    const temp = `${this.file}.${process.pid}.tmp`;
    writeFileSync(temp, `${JSON.stringify(this.data, null, 2)}\n`);
    renameSync(temp, this.file);
  }
}
