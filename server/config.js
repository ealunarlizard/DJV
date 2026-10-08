// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

// The server's configuration: what it tells DJV about itself and the media
// share, and the registry of the DJV clients allowed to use it.
//
// It is one JSON file, so that it can be read and edited by hand on the
// server as easily as through the admin page, which writes the same file.
// The sessions themselves are not here: they are in the database, and they
// change with every note, where this changes when the admin says so.

import { readFileSync, renameSync, writeFileSync, mkdirSync } from 'node:fs';
import path from 'node:path';

// A client identifier is chosen by whoever sets up a PC -- by default its
// Windows computer name -- and goes into URLs, logs, and the admin page, so it
// is kept to what is safe in all of them. DJV checks the same
// (models::isValidSyncClientId()).
const CLIENT_PATTERN = /^[A-Za-z0-9._-]{1,64}$/;

const MAX_NAME = 200;
const MAX_UNC = 1024;
const MAX_VERSION = 64;

export class ConfigError extends Error {}

export function isValidClientId(id) {
  return typeof id === 'string' && CLIENT_PATTERN.test(id) && id !== '.' && id !== '..';
}

function defaults() {
  return {
    name: 'DJV sync server',
    storage: { unc: '' },
    djvVersion: '',
    // Off unless asked for, so that a server started with no configuration
    // behaves as it always did: anyone with the token can join.
    requireRegistration: false,
    adminToken: '',
    clients: [],
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
    throw new ConfigError('The storage share must be a UNC path, e.g. \\\\10.0.0.5\\media.');
  }
  return unc.replace(/\\+$/, '');
}

function checkClient(client) {
  if (!client || typeof client !== 'object' || !isValidClientId(client.id)) {
    throw new ConfigError('A client identifier can only have letters, digits, ".", "_" and "-", up to 64 of them.');
  }
  return {
    id: client.id,
    name: checkText(client.name ?? '', 'A client name', MAX_NAME),
    added: typeof client.added === 'string' ? client.added : new Date().toISOString(),
  };
}

export class ServerConfig {
  // With no file, the configuration lives in memory only: a test, or a server
  // run without one.
  constructor(file = null, initial = {}) {
    this.file = file;
    this.data = defaults();
    this.apply(initial, { allowToken: true, allowClients: true });
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

  // Take the fields given, checked; the ones not given are left as they are.
  // The admin token and the client list are only taken from the file: the
  // admin page has its own requests for the clients, and does not set the
  // token it is itself let in by.
  apply(json, { allowToken = false, allowClients = false } = {}) {
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
      next.storage = { ...next.storage, unc: checkUnc(json.storage.unc ?? '') };
    }
    if (json.djvVersion !== undefined) {
      next.djvVersion = checkText(json.djvVersion, 'The DJV version', MAX_VERSION);
    }
    if (json.requireRegistration !== undefined) {
      if (typeof json.requireRegistration !== 'boolean') {
        throw new ConfigError('"requireRegistration" must be true or false.');
      }
      next.requireRegistration = json.requireRegistration;
    }
    if (allowToken && json.adminToken !== undefined) {
      next.adminToken = checkText(json.adminToken, 'The admin token', 1024);
    }
    if (allowClients && json.clients !== undefined) {
      if (!Array.isArray(json.clients)) {
        throw new ConfigError('"clients" must be a list.');
      }
      const seen = new Set();
      next.clients = [];
      for (const item of json.clients) {
        const client = checkClient(item);
        if (!seen.has(client.id)) {
          seen.add(client.id);
          next.clients.push(client);
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

  get djvVersion() {
    return this.data.djvVersion;
  }

  get requireRegistration() {
    return this.data.requireRegistration;
  }

  get adminToken() {
    return this.data.adminToken;
  }

  get clients() {
    return this.data.clients;
  }

  client(id) {
    return this.data.clients.find((client) => client.id === id) ?? null;
  }

  // What the admin page is shown: everything but the token that lets it in.
  view() {
    return {
      name: this.data.name,
      storage: { ...this.data.storage },
      djvVersion: this.data.djvVersion,
      requireRegistration: this.data.requireRegistration,
      adminTokenSet: this.data.adminToken !== '',
      file: this.file,
    };
  }

  update(json) {
    this.apply(json);
    this.save();
  }

  setClient(id, name) {
    const client = checkClient({ id, name, added: this.client(id)?.added });
    const index = this.data.clients.findIndex((item) => item.id === id);
    if (index >= 0) {
      this.data.clients[index] = client;
    } else {
      this.data.clients.push(client);
    }
    this.save();
    return client;
  }

  removeClient(id) {
    const before = this.data.clients.length;
    this.data.clients = this.data.clients.filter((client) => client.id !== id);
    if (this.data.clients.length !== before) {
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
