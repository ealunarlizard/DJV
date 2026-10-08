// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

// The studio network: which PCs are let in, what they are told about the
// media share, and the admin requests that manage both.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';

import { createSyncServer } from '../server.js';
import { ServerConfig } from '../config.js';

async function start(options = {}) {
  const dataDir = options.dataDir ?? await mkdtemp(path.join(tmpdir(), 'djv-lan-'));
  const server = createSyncServer({ dataDir, ...options });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const base = `http://127.0.0.1:${server.address().port}`;
  return {
    server,
    base,
    dataDir,
    async stop({ keepData = false } = {}) {
      await new Promise((resolve) => server.close(resolve));
      if (!keepData) {
        await rm(dataDir, { recursive: true, force: true });
      }
    },
  };
}

const studio = {
  name: 'Dev review server',
  storage: { unc: '\\\\10.0.0.5\\media' },
  djvVersion: '3.7.5',
  requireRegistration: true,
  clients: [{ id: 'edit-bay-1', name: 'Edit bay 1' }],
};

function as(client, version = '3.7.5') {
  return { 'X-DJV-Client': client, 'X-DJV-Version': version };
}

function marker(id, text) {
  return { id, text, range: { start: { value: 1, rate: 24 }, duration: { value: 1, rate: 24 } } };
}

async function json(res) {
  return { status: res.status, body: await res.json() };
}

test('hello tells a client where the media is and whether it is let in', async () => {
  const s = await start({ config: new ServerConfig(null, studio) });
  try {
    let { status, body } = await json(await fetch(`${s.base}/api/hello`, { headers: as('edit-bay-1') }));
    assert.equal(status, 200);
    assert.equal(body.name, 'Dev review server');
    assert.equal(body.storage.unc, '\\\\10.0.0.5\\media');
    assert.equal(body.djvVersion, '3.7.5');
    assert.equal(body.requireRegistration, true);
    assert.deepEqual(body.client, { id: 'edit-bay-1', name: 'Edit bay 1', registered: true });
    assert.match(body.serverVersion, /^\d+\.\d+\.\d+$/);

    // A PC the admin has not let in still gets its answer: it is how DJV
    // learns to say so.
    ({ status, body } = await json(await fetch(`${s.base}/api/hello`, { headers: as('edit-bay-9') })));
    assert.equal(status, 200);
    assert.deepEqual(body.client, { id: 'edit-bay-9', name: '', registered: false });
  } finally {
    await s.stop();
  }
});

test('with registration required, only registered clients use the sessions', async () => {
  const s = await start({ config: new ServerConfig(null, studio) });
  try {
    assert.equal((await fetch(`${s.base}/api/health`)).status, 200);

    let { status, body } = await json(await fetch(`${s.base}/api/sessions/sh010`));
    assert.equal(status, 403);
    assert.equal(body.code, 'unregistered');

    ({ status, body } = await json(await fetch(`${s.base}/api/sessions/sh010`, { headers: as('edit-bay-9', '3.7.4') })));
    assert.equal(status, 403);
    assert.equal(body.code, 'unregistered');
    assert.equal(body.client, 'edit-bay-9');
    assert.match(body.error, /edit-bay-9/);

    // Refused, it waits on the admin page with what it said about itself.
    const clients = await (await fetch(`${s.base}/api/admin/clients`)).json();
    assert.deepEqual(clients.waiting.map((c) => [c.id, c.version]), [['edit-bay-9', '3.7.4']]);

    const put = await fetch(`${s.base}/api/sessions/sh010/markers/m1`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json', ...as('edit-bay-1') },
      body: JSON.stringify({ clientId: 'conn-1', marker: marker('m1', 'Let in.') }),
    });
    assert.equal(put.status, 200);
    const state = await (await fetch(`${s.base}/api/sessions/sh010`, { headers: as('edit-bay-1') })).json();
    assert.equal(state.markers[0].text, 'Let in.');
  } finally {
    await s.stop();
  }
});

test('without registration, any client is let in, as before', async () => {
  const s = await start();
  try {
    assert.equal((await fetch(`${s.base}/api/sessions/sh010`)).status, 200);
    assert.equal((await fetch(`${s.base}/api/sessions/sh010`, { headers: as('anyone') })).status, 200);
  } finally {
    await s.stop();
  }
});

test('the changes say which client made them', async () => {
  const s = await start({ config: new ServerConfig(null, studio) });
  try {
    await fetch(`${s.base}/api/sessions/sh010/markers/m1`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json', ...as('edit-bay-1') },
      body: JSON.stringify({ clientId: 'conn-1', marker: marker('m1', 'One.') }),
    });
    const controller = new AbortController();
    const res = await fetch(`${s.base}/api/sessions/sh010/events?since=0`, {
      headers: as('edit-bay-1'),
      signal: controller.signal,
    });
    const reader = res.body.getReader();
    let text = '';
    while (!text.includes('"rev":1')) {
      text += new TextDecoder().decode((await reader.read()).value);
    }
    controller.abort();
    const event = JSON.parse(text.split('\n').find((line) => line.startsWith('data:')).slice(5));
    assert.equal(event.client, 'edit-bay-1');
    assert.equal(event.clientId, 'conn-1');
  } finally {
    await s.stop();
  }
});

test('the admin page shows who is connected, to which session', async () => {
  const s = await start({ config: new ServerConfig(null, studio) });
  const controller = new AbortController();
  try {
    const stream = await fetch(`${s.base}/api/sessions/sh010/events?since=0`, {
      headers: as('edit-bay-1', '3.7.5'),
      signal: controller.signal,
    });
    assert.equal(stream.status, 200);
    await stream.body.getReader().read();

    const clients = await (await fetch(`${s.base}/api/admin/clients`)).json();
    assert.equal(clients.connected.length, 1);
    assert.equal(clients.connected[0].client, 'edit-bay-1');
    assert.equal(clients.connected[0].session, 'sh010');
    assert.equal(clients.connected[0].version, '3.7.5');
    assert.equal(clients.registered[0].connected, 1);
    assert.equal(clients.registered[0].version, '3.7.5');

    const sessions = await (await fetch(`${s.base}/api/admin/sessions`)).json();
    assert.equal(sessions.sessions[0].session, 'sh010');
  } finally {
    controller.abort();
    await s.stop();
  }
});

test('the admin requests need the admin token when one is set', async () => {
  const s = await start({ config: new ServerConfig(null, studio), adminToken: 'let-me-in' });
  try {
    let { status, body } = await json(await fetch(`${s.base}/api/admin/config`));
    assert.equal(status, 401);
    assert.equal(body.code, 'admin-token');
    ({ status } = await json(await fetch(`${s.base}/api/admin/config`, {
      headers: { Authorization: 'Bearer wrong' },
    })));
    assert.equal(status, 403);
    ({ status, body } = await json(await fetch(`${s.base}/api/admin/config`, {
      headers: { Authorization: 'Bearer let-me-in' },
    })));
    assert.equal(status, 200);
    // The token itself is never sent back.
    assert.equal(body.adminTokenSet, true);
    assert.equal(body.adminToken, undefined);

    // The page itself holds nothing, so it needs no token.
    const page = await fetch(`${s.base}/admin`);
    assert.equal(page.status, 200);
    assert.match(page.headers.get('content-type'), /text\/html/);
  } finally {
    await s.stop();
  }
});

test('clients and settings set on the admin page are kept in the configuration file', async () => {
  const dataDir = await mkdtemp(path.join(tmpdir(), 'djv-lan-'));
  const configFile = path.join(dataDir, 'config.json');
  await writeFile(configFile, JSON.stringify({ ...studio, adminToken: 'kept' }));
  let s = await start({ dataDir, configFile });
  const admin = (method, url, body) => fetch(`${s.base}/api/admin/${url}`, {
    method,
    headers: { 'Content-Type': 'application/json', Authorization: 'Bearer kept' },
    body: body === undefined ? undefined : JSON.stringify(body),
  });
  try {
    // A client waiting is approved; it no longer waits.
    await fetch(`${s.base}/api/hello`, { headers: as('grading') });
    let res = await admin('PUT', 'clients/grading', { name: 'Grading suite' });
    assert.equal(res.status, 200);
    let clients = await (await admin('GET', 'clients')).json();
    assert.deepEqual(clients.waiting, []);
    assert.deepEqual(clients.registered.map((c) => [c.id, c.name]), [['edit-bay-1', 'Edit bay 1'], ['grading', 'Grading suite']]);

    // Renamed in place, and one removed.
    await admin('PUT', 'clients/grading', { name: 'Grading' });
    assert.equal((await admin('DELETE', 'clients/edit-bay-1')).status, 200);
    assert.equal((await admin('DELETE', 'clients/edit-bay-1')).status, 404);
    assert.equal((await admin('PUT', 'clients/bad%20id', { name: 'x' })).status, 400);

    res = await admin('PUT', 'config', { storage: { unc: '//10.0.0.6/media/' }, djvVersion: '3.7.6' });
    assert.equal(res.status, 200);
    assert.equal((await res.json()).storage.unc, '\\\\10.0.0.6\\media');
    // What is not a share is refused, and nothing changes.
    res = await admin('PUT', 'config', { storage: { unc: 'C:\\media' } });
    assert.equal(res.status, 400);
    res = await admin('PUT', 'config', { requireRegistration: 'yes' });
    assert.equal(res.status, 400);
  } finally {
    await s.stop({ keepData: true });
  }

  const saved = JSON.parse(await readFile(configFile, 'utf8'));
  assert.equal(saved.adminToken, 'kept');
  assert.equal(saved.storage.unc, '\\\\10.0.0.6\\media');
  assert.equal(saved.djvVersion, '3.7.6');
  assert.deepEqual(saved.clients.map((c) => [c.id, c.name]), [['grading', 'Grading']]);

  s = await start({ dataDir, configFile });
  try {
    const hello = await (await fetch(`${s.base}/api/hello`, { headers: as('grading') })).json();
    assert.equal(hello.client.registered, true);
    assert.equal(hello.storage.unc, '\\\\10.0.0.6\\media');
  } finally {
    await s.stop();
  }
});

test('a configuration kept in the data folder is not taken for a session', async () => {
  const dataDir = await mkdtemp(path.join(tmpdir(), 'djv-lan-'));
  const configFile = path.join(dataDir, 'config.json');
  await writeFile(configFile, JSON.stringify(studio));
  let s = await start({ dataDir, configFile });
  await s.stop({ keepData: true });
  s = await start({ dataDir, configFile });
  try {
    // Still there, and still in force.
    assert.equal(JSON.parse(await readFile(configFile, 'utf8')).name, 'Dev review server');
    const { sessions } = await (await fetch(`${s.base}/api/admin/sessions`)).json();
    assert.deepEqual(sessions, []);
    const hello = await (await fetch(`${s.base}/api/hello`, { headers: as('edit-bay-1') })).json();
    assert.equal(hello.client.registered, true);
  } finally {
    await s.stop();
  }
});

test('a configuration that cannot be read stops the server from starting', async () => {
  const dataDir = await mkdtemp(path.join(tmpdir(), 'djv-lan-'));
  const configFile = path.join(dataDir, 'config.json');
  try {
    await writeFile(configFile, '{ not json');
    assert.throws(() => createSyncServer({ dataDir, configFile }), /Cannot read the configuration/);
    await writeFile(configFile, JSON.stringify({ storage: { unc: 'not a share' } }));
    assert.throws(() => createSyncServer({ dataDir, configFile }), /UNC/);
  } finally {
    await rm(dataDir, { recursive: true, force: true });
  }
});
