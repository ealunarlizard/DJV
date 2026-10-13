// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

// The studio network: who may sign in, what a signed-in DJV is told about
// the media share, and the admin requests that manage both.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import { DatabaseSync } from 'node:sqlite';
import path from 'node:path';

import { createSyncServer } from '../server.js';
import { ROOT_PASSWORD, login, start } from './helpers.js';
import { hashPasswordSync, verifyPasswordSync } from '../users.js';

const studio = {
  name: 'Dev review server',
  storage: { unc: '\\\\192.168.1.40\\media', user: 'djv' },
  djvVersion: '3.7.5',
};

function pc(id, version = '3.7.5') {
  return { 'X-DJV-Client': id, 'X-DJV-Version': version };
}

function bearer(token, headers = {}) {
  return { Authorization: `Bearer ${token}`, ...headers };
}

function marker(id, text) {
  return { id, text, range: { start: { value: 1, rate: 24 }, duration: { value: 1, rate: 24 } } };
}

function put(base, token, session, m) {
  return fetch(`${base}/api/sessions/${session}/markers/${m.id}`, {
    method: 'PUT',
    headers: bearer(token, { 'Content-Type': 'application/json' }),
    body: JSON.stringify({ clientId: 'c1', marker: m }),
  });
}

// The admin requests, as the user with the token given.
function admin(base, token, method, url, body) {
  return fetch(`${base}/api/admin/${url}`, {
    method,
    headers: bearer(token, { 'Content-Type': 'application/json' }),
    body: body === undefined ? undefined : JSON.stringify(body),
  });
}

async function signIn(base, username, password, headers = {}) {
  const res = await login(base, username, password, headers);
  assert.equal(res.status, 200, `signing in as ${username}`);
  return (await res.json()).token;
}

async function addUser(s, username, password, name = '') {
  const res = await admin(s.base, s.token, 'PUT', `users/${username}`, { name, password });
  assert.equal(res.status, 200, `adding ${username}`);
}

test('the administrator is made from the password file, and kept hashed', async () => {
  const dataDir = await mkdtemp(path.join(tmpdir(), 'djv-auth-'));
  const configFile = path.join(dataDir, 'config.json');
  let s = await start({ dataDir, configFile });
  await s.stop({ keepData: true });
  const saved = JSON.parse(await readFile(configFile, 'utf8'));
  assert.deepEqual(saved.users.map((u) => u.username), ['lldjv']);
  assert.ok(saved.users[0].passwordHash.startsWith('scrypt$'));
  assert.ok(!JSON.stringify(saved).includes(ROOT_PASSWORD));

  // A new password in the file is the new password, and the sign-ins made
  // with the old one end.
  const old = s.token;
  s = await start({ dataDir, configFile, rootPassword: 'a-new-root-password' });
  try {
    assert.equal((await fetch(`${s.base}/api/sessions`, { headers: bearer(old) })).status, 401);
    assert.equal((await login(s.base, 'lldjv', ROOT_PASSWORD)).status, 401);
  } finally {
    await s.stop({ keepData: true });
  }

  // Without a file, the administrator already made is enough.
  const server = createSyncServer({ dataDir, configFile });
  await new Promise((resolve) => server.close(resolve));
  await rm(dataDir, { recursive: true, force: true });
});

test('a server with no administrator and no password for one does not start', async () => {
  const dataDir = await mkdtemp(path.join(tmpdir(), 'djv-auth-'));
  try {
    assert.throws(() => createSyncServer({ dataDir }), /ROOT_PASSWORD_FILE/);
    assert.throws(() => createSyncServer({ dataDir, rootPassword: 'short' }), /at least 8/);
  } finally {
    await rm(dataDir, { recursive: true, force: true });
  }
});

test('the sessions need a signed-in user', async () => {
  const s = await start();
  try {
    assert.equal((await fetch(`${s.base}/api/health`)).status, 200);
    let res = await fetch(`${s.base}/api/sessions/sh010`);
    assert.equal(res.status, 401);
    assert.equal((await res.json()).code, 'signed-out');
    assert.equal((await fetch(`${s.base}/api/sessions/sh010`, { headers: bearer('made-up') })).status, 401);
    assert.equal((await fetch(`${s.base}/api/sessions/sh010/events`)).status, 401);
    assert.equal((await put(s.base, '', 'sh010', marker('m1', 'x'))).status, 401);

    await addUser(s, 'anna', 'anna-password', 'Anna');
    const token = await signIn(s.base, 'anna', 'anna-password');
    res = await put(s.base, token, 'sh010', marker('m1', 'x'));
    assert.equal(res.status, 200);
    res = await fetch(`${s.base}/api/sessions/sh010`, { headers: bearer(token) });
    assert.equal(res.status, 200);
    assert.equal((await res.json()).markers[0].text, 'x');
  } finally {
    await s.stop();
  }
});

test('signing in with a wrong password is refused, and slowed down after many', async () => {
  const s = await start();
  try {
    await addUser(s, 'anna', 'anna-password');
    let res = await login(s.base, 'anna', 'not-her-password');
    assert.equal(res.status, 401);
    assert.equal((await res.json()).code, 'login-failed');
    assert.equal((await login(s.base, 'nobody', 'anna-password')).status, 401);
    assert.equal((await login(s.base, '../x', 'anna-password')).status, 401);
    for (let i = 0; i < 8; ++i) {
      await login(s.base, 'anna', 'still-wrong');
    }
    // Ten failures from this address: even the right password waits.
    res = await login(s.base, 'anna', 'anna-password');
    assert.equal(res.status, 429);
  } finally {
    await s.stop();
  }
});

test('a sign-in lasts across a restart, and ends with signing out', async () => {
  const dataDir = await mkdtemp(path.join(tmpdir(), 'djv-auth-'));
  const configFile = path.join(dataDir, 'config.json');
  let s = await start({ dataDir, configFile });
  await addUser(s, 'anna', 'anna-password');
  const token = await signIn(s.base, 'anna', 'anna-password');
  await s.stop({ keepData: true });

  s = await start({ dataDir, configFile });
  try {
    assert.equal((await fetch(`${s.base}/api/sessions`, { headers: bearer(token) })).status, 200);
    const res = await fetch(`${s.base}/api/logout`, { method: 'POST', headers: bearer(token) });
    assert.equal(res.status, 200);
    assert.equal((await fetch(`${s.base}/api/sessions`, { headers: bearer(token) })).status, 401);
  } finally {
    await s.stop();
  }
});

test('a new password or removing the user ends their sign-ins', async () => {
  const s = await start();
  try {
    await addUser(s, 'anna', 'anna-password');
    await addUser(s, 'ben', 'ben-password');
    const anna = await signIn(s.base, 'anna', 'anna-password');
    const ben = await signIn(s.base, 'ben', 'ben-password');

    // A new name alone keeps her signed in.
    assert.equal((await admin(s.base, s.token, 'PUT', 'users/anna', { name: 'Anna K' })).status, 200);
    assert.equal((await fetch(`${s.base}/api/sessions`, { headers: bearer(anna) })).status, 200);

    assert.equal((await admin(s.base, s.token, 'PUT', 'users/anna', { password: 'another-password' })).status, 200);
    assert.equal((await fetch(`${s.base}/api/sessions`, { headers: bearer(anna) })).status, 401);
    assert.equal((await login(s.base, 'anna', 'anna-password')).status, 401);
    await signIn(s.base, 'anna', 'another-password');

    assert.equal((await admin(s.base, s.token, 'DELETE', 'users/ben')).status, 200);
    assert.equal((await fetch(`${s.base}/api/sessions`, { headers: bearer(ben) })).status, 401);
    assert.equal((await admin(s.base, s.token, 'DELETE', 'users/ben')).status, 404);
  } finally {
    await s.stop();
  }
});

test('only the administrator uses the admin requests', async () => {
  const s = await start();
  try {
    await addUser(s, 'anna', 'anna-password');
    const anna = await signIn(s.base, 'anna', 'anna-password');
    let res = await admin(s.base, anna, 'GET', 'users');
    assert.equal(res.status, 403);
    assert.equal((await res.json()).code, 'not-admin');
    assert.equal((await admin(s.base, anna, 'PUT', 'users/eve', { password: 'eve-password' })).status, 403);
    res = await admin(s.base, '', 'GET', 'users');
    assert.equal(res.status, 401);

    // The administrator cannot be removed, nor given a password other than
    // the file's.
    assert.equal((await admin(s.base, s.token, 'DELETE', 'users/lldjv')).status, 400);
    res = await admin(s.base, s.token, 'PUT', 'users/lldjv', { password: 'something-else' });
    assert.equal(res.status, 400);
    assert.match((await res.json()).error, /ROOT_PASSWORD_FILE/);
    // A new user needs a password, of a decent length, and a safe name.
    assert.equal((await admin(s.base, s.token, 'PUT', 'users/carl', { name: 'Carl' })).status, 400);
    assert.equal((await admin(s.base, s.token, 'PUT', 'users/carl', { password: 'short' })).status, 400);
    assert.equal((await admin(s.base, s.token, 'PUT', 'users/bad%20name', { password: 'long-enough' })).status, 400);
  } finally {
    await s.stop();
  }
});

test('users and settings set on the admin page are kept in the configuration file', async () => {
  const dataDir = await mkdtemp(path.join(tmpdir(), 'djv-auth-'));
  const configFile = path.join(dataDir, 'config.json');
  await writeFile(configFile, JSON.stringify(studio));
  let s = await start({ dataDir, configFile });
  try {
    await addUser(s, 'anna', 'anna-password', 'Anna');
    const { users } = await (await admin(s.base, s.token, 'GET', 'users')).json();
    assert.deepEqual(users.map((u) => [u.username, u.name, u.admin]), [['lldjv', 'Administrator', true], ['anna', 'Anna', false]]);
    assert.ok(users.every((u) => u.passwordHash === undefined));

    let res = await admin(s.base, s.token, 'PUT', 'config', { storage: { unc: '//192.168.1.40/video/', user: '192.168.1.40\\djv' }, djvVersion: '3.7.6' });
    assert.equal(res.status, 200);
    const view = await res.json();
    assert.equal(view.storage.unc, '\\\\192.168.1.40\\video');
    assert.equal(view.storage.user, '192.168.1.40\\djv');
    assert.equal(view.sharePasswordSet, false);
    // What is not a share is refused, and nothing changes.
    assert.equal((await admin(s.base, s.token, 'PUT', 'config', { storage: { unc: 'C:\\media' } })).status, 400);
    assert.equal((await admin(s.base, s.token, 'PUT', 'config', { storage: { user: 'a:b' } })).status, 400);
  } finally {
    await s.stop({ keepData: true });
  }

  const saved = JSON.parse(await readFile(configFile, 'utf8'));
  assert.equal(saved.storage.unc, '\\\\192.168.1.40\\video');
  assert.equal(saved.djvVersion, '3.7.6');
  assert.deepEqual(saved.users.map((u) => u.username), ['lldjv', 'anna']);
  assert.ok(verifyPasswordSync('anna-password', saved.users[1].passwordHash));

  s = await start({ dataDir, configFile });
  try {
    await signIn(s.base, 'anna', 'anna-password');
  } finally {
    await s.stop();
  }
});

test('hello tells a signed-in DJV where the media is, and how to sign in to it', async () => {
  const s = await start({ config: undefined, sharePassword: 'share-password' });
  try {
    await admin(s.base, s.token, 'PUT', 'config', studio);
    await addUser(s, 'anna', 'anna-password', 'Anna');

    let hello = await (await fetch(`${s.base}/api/hello`, { headers: pc('edit-bay-1') })).json();
    assert.equal(hello.name, 'Dev review server');
    assert.equal(hello.djvVersion, '3.7.5');
    assert.equal(hello.authRequired, true);
    assert.equal(hello.signedIn, false);
    assert.equal(hello.storage, undefined);

    const token = await signIn(s.base, 'anna', 'anna-password', pc('edit-bay-1'));
    hello = await (await fetch(`${s.base}/api/hello`, { headers: bearer(token, pc('edit-bay-1')) })).json();
    assert.equal(hello.signedIn, true);
    assert.deepEqual(hello.user, { username: 'anna', name: 'Anna', admin: false });
    assert.deepEqual(hello.storage, { unc: '\\\\192.168.1.40\\media', user: 'djv', password: 'share-password' });

    const config = await (await admin(s.base, s.token, 'GET', 'config')).json();
    assert.equal(config.sharePasswordSet, true);
    assert.ok(!JSON.stringify(config).includes('share-password'));
  } finally {
    await s.stop();
  }
});

test('hello gives the server\'s identity to anyone, the same across a restart', async () => {
  const dataDir = await mkdtemp(path.join(tmpdir(), 'djv-auth-'));
  const configFile = path.join(dataDir, 'config.json');
  await writeFile(configFile, JSON.stringify(studio));
  let s = await start({ dataDir, configFile });
  let id;
  try {
    await addUser(s, 'anna', 'anna-password', 'Anna');
    const anonymous = await (await fetch(`${s.base}/api/hello`)).json();
    id = anonymous.serverId;
    assert.match(id, /^[0-9a-f-]{36}$/);
    const token = await signIn(s.base, 'anna', 'anna-password');
    const signedIn = await (await fetch(`${s.base}/api/hello`, { headers: bearer(token) })).json();
    assert.equal(signedIn.serverId, id);
    // The same server under another address is the same server.
    const other = await (await fetch(`${s.base.replace('127.0.0.1', 'localhost')}/api/hello`)).json();
    assert.equal(other.serverId, id);
  } finally {
    await s.stop({ keepData: true });
  }

  s = await start({ dataDir, configFile });
  try {
    assert.equal((await (await fetch(`${s.base}/api/hello`)).json()).serverId, id);
  } finally {
    await s.stop();
  }

  // Another server's data, another identity.
  s = await start();
  try {
    assert.notEqual((await (await fetch(`${s.base}/api/hello`)).json()).serverId, id);
  } finally {
    await s.stop();
  }
});

test('the changes say who made them, and the admin page who is connected', async () => {
  const s = await start();
  try {
    await addUser(s, 'anna', 'anna-password');
    const token = await signIn(s.base, 'anna', 'anna-password');
    const controller = new AbortController();
    const stream = await fetch(`${s.base}/api/sessions/sh010/events`, {
      headers: bearer(token, pc('edit-bay-1', '3.7.4')),
      signal: controller.signal,
    });
    assert.equal(stream.status, 200);
    try {
      const { connected } = await (await admin(s.base, s.token, 'GET', 'connected')).json();
      assert.equal(connected.length, 1);
      assert.equal(connected[0].user, 'anna');
      assert.equal(connected[0].pc, 'edit-bay-1');
      assert.equal(connected[0].version, '3.7.4');
      assert.equal(connected[0].session, 'sh010');
      const { users } = await (await admin(s.base, s.token, 'GET', 'users')).json();
      const anna = users.find((u) => u.username === 'anna');
      assert.equal(anna.connected, 1);
      assert.equal(anna.pc, 'edit-bay-1');
      assert.ok(anna.lastSignIn);

      await put(s.base, token, 'sh010', marker('m1', 'x'));
      const state = await (await fetch(`${s.base}/api/sessions/sh010`, { headers: bearer(token) })).json();
      assert.equal(state.rev, 1);
      const text = await new Promise((resolve) => {
        const reader = stream.body.getReader();
        let buffer = '';
        const read = () => reader.read().then(({ value }) => {
          buffer += new TextDecoder().decode(value);
          if (buffer.includes('"upsert"')) {
            resolve(buffer);
          } else {
            read();
          }
        });
        read();
      });
      assert.match(text, /"user":"anna"/);
    } finally {
      controller.abort();
    }
  } finally {
    await s.stop();
  }
});

test('the database can be downloaded while the server runs', async () => {
  const s = await start();
  try {
    await put(s.base, s.token, 'sh010', marker('m1', 'backed up'));
    const res = await admin(s.base, s.token, 'GET', 'backup');
    assert.equal(res.status, 200);
    assert.match(res.headers.get('content-disposition'), /djv-sync-.*\.db/);
    const file = path.join(s.dataDir, 'copy.db');
    await writeFile(file, Buffer.from(await res.arrayBuffer()));
    const db = new DatabaseSync(file);
    try {
      const row = db.prepare('SELECT data FROM markers WHERE session = ?').get('sh010');
      assert.equal(JSON.parse(row.data).text, 'backed up');
    } finally {
      db.close();
    }
  } finally {
    await s.stop();
  }
});

test('the database can be kept with exclusive locking', async () => {
  const dataDir = await mkdtemp(path.join(tmpdir(), 'djv-auth-'));
  let s = await start({ dataDir, exclusive: true });
  await put(s.base, s.token, 'sh010', marker('m1', 'kept'));
  await s.stop({ keepData: true });
  s = await start({ dataDir, exclusive: true });
  try {
    const state = await (await fetch(`${s.base}/api/sessions/sh010`, { headers: bearer(s.token) })).json();
    assert.equal(state.markers[0].text, 'kept');
  } finally {
    await s.stop();
  }
});

test('a configuration from the first LAN server still loads, without its registry', async () => {
  const dataDir = await mkdtemp(path.join(tmpdir(), 'djv-auth-'));
  const configFile = path.join(dataDir, 'config.json');
  await writeFile(configFile, JSON.stringify({
    ...studio,
    requireRegistration: true,
    adminToken: 'old',
    clients: [{ id: 'edit-bay-1', name: 'Edit bay 1' }],
  }));
  const s = await start({ dataDir, configFile });
  try {
    // Kept in the data folder, it is not taken for a session either.
    const { sessions } = await (await admin(s.base, s.token, 'GET', 'sessions')).json();
    assert.deepEqual(sessions, []);
  } finally {
    await s.stop({ keepData: true });
  }
  const saved = JSON.parse(await readFile(configFile, 'utf8'));
  assert.equal(saved.name, 'Dev review server');
  assert.equal(saved.clients, undefined);
  assert.equal(saved.adminToken, undefined);
  assert.equal(saved.requireRegistration, undefined);
  await rm(dataDir, { recursive: true, force: true });
});

test('a configuration that cannot be read stops the server from starting', async () => {
  const dataDir = await mkdtemp(path.join(tmpdir(), 'djv-auth-'));
  const configFile = path.join(dataDir, 'config.json');
  try {
    await writeFile(configFile, '{ not json');
    assert.throws(() => createSyncServer({ dataDir, configFile, rootPassword: ROOT_PASSWORD }), /Cannot read the configuration/);
    await writeFile(configFile, JSON.stringify({ storage: { unc: 'not a share' } }));
    assert.throws(() => createSyncServer({ dataDir, configFile, rootPassword: ROOT_PASSWORD }), /UNC/);
    await writeFile(configFile, JSON.stringify({ users: [{ username: 'anna', passwordHash: 'plain' }] }));
    assert.throws(() => createSyncServer({ dataDir, configFile, rootPassword: ROOT_PASSWORD }), /password hash/);
    // A hash made elsewhere is fine.
    await writeFile(configFile, JSON.stringify({ users: [{ username: 'anna', passwordHash: hashPasswordSync('anna-password') }] }));
    const server = createSyncServer({ dataDir, configFile, rootPassword: ROOT_PASSWORD });
    await new Promise((resolve) => server.close(resolve));
  } finally {
    await rm(dataDir, { recursive: true, force: true });
  }
});
