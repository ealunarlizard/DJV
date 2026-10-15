// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

// What the tests share: a server on a free port, signed in to as the
// administrator, and a fetch that sends that sign-in along.

import { mkdtemp, rm } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';

import { createSyncServer } from '../server.js';
import { ROOT_USER } from '../users.js';

export const ROOT_PASSWORD = 'root-password-for-tests';

// The session token each test server gave, by its address. A request to a
// server is sent with its token unless it says otherwise, so that the tests
// of the sessions read as they did before there were users.
const tokens = new Map();

export function fetch(url, init = {}) {
  const token = tokens.get(new URL(url).origin);
  const headers = { ...(token ? { Authorization: `Bearer ${token}` } : {}), ...(init.headers ?? {}) };
  return globalThis.fetch(url, { ...init, headers });
}

export function login(base, username, password, headers = {}) {
  return globalThis.fetch(`${base}/api/login`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', ...headers },
    body: JSON.stringify({ username, password }),
  });
}

// Start a server, and sign in to it as the administrator unless asked not
// to.
export async function start(options = {}) {
  const { signIn = true, ...rest } = options;
  const dataDir = rest.dataDir ?? await mkdtemp(path.join(tmpdir(), 'djv-sync-'));
  const server = createSyncServer({ dataDir, rootPassword: ROOT_PASSWORD, ...rest });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const base = `http://127.0.0.1:${server.address().port}`;
  let token = '';
  if (signIn) {
    const res = await login(base, ROOT_USER, rest.rootPassword ?? ROOT_PASSWORD);
    if (res.status !== 200) {
      throw new Error(`Cannot sign in to the test server: ${res.status} ${await res.text()}`);
    }
    token = (await res.json()).token;
    tokens.set(base, token);
  }
  return {
    server,
    base,
    dataDir,
    token,
    async stop({ keepData = false } = {}) {
      tokens.delete(base);
      await new Promise((resolve) => server.close(resolve));
      if (!keepData) {
        await rm(dataDir, { recursive: true, force: true });
      }
    },
  };
}
