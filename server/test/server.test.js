// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp, readdir, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';

// The fetch that signs in as the test server's administrator.
import { fetch, start } from './helpers.js';

function marker(id, text, extra = {}) {
  return {
    id,
    text,
    color: [0, 0.6, 0, 1],
    range: {
      start: { value: 10, rate: 24 },
      duration: { value: 1, rate: 24 },
    },
    ...extra,
  };
}

function put(base, session, m, clientId = 'a', headers = {}) {
  return fetch(`${base}/api/sessions/${session}/markers/${m.id}`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json', ...headers },
    body: JSON.stringify({ clientId, marker: m }),
  });
}

// Read events from a stream until the predicate has what it wants.
async function readEvents(base, session, since, count, headers = {}) {
  const controller = new AbortController();
  const res = await fetch(`${base}/api/sessions/${session}/events?since=${since}`, {
    headers,
    signal: controller.signal,
  });
  assert.equal(res.status, 200);
  const reader = res.body.getReader();
  const decoder = new TextDecoder();
  const events = [];
  let buffer = '';
  const timeout = setTimeout(() => controller.abort(), 5000);
  try {
    while (events.length < count) {
      const { value, done } = await reader.read();
      if (done) {
        break;
      }
      buffer += decoder.decode(value, { stream: true });
      let end;
      while ((end = buffer.indexOf('\n\n')) >= 0) {
        const block = buffer.slice(0, end);
        buffer = buffer.slice(end + 2);
        const data = block.split('\n').filter((line) => line.startsWith('data:')).map((line) => line.slice(5).trim());
        if (data.length > 0) {
          events.push(JSON.parse(data.join('\n')));
        }
      }
    }
  } finally {
    clearTimeout(timeout);
    controller.abort();
  }
  return events;
}

test('a change is in the state and numbered', async () => {
  const s = await start();
  try {
    let res = await put(s.base, 'sh010', marker('m1', 'Too cold'));
    assert.equal(res.status, 200);
    assert.deepEqual(await res.json(), { rev: 1 });

    res = await fetch(`${s.base}/api/sessions/sh010`);
    const state = await res.json();
    assert.equal(state.rev, 1);
    assert.equal(state.markers.length, 1);
    assert.equal(state.markers[0].text, 'Too cold');
    assert.deepEqual(state.deleted, []);
  } finally {
    await s.stop();
  }
});

test('a session nobody has written to is empty', async () => {
  const s = await start();
  try {
    const res = await fetch(`${s.base}/api/sessions/new-one`);
    assert.deepEqual(await res.json(), { session: 'new-one', rev: 0, media: null, markers: [], deleted: [] });
  } finally {
    await s.stop();
  }
});

test('the stream sends the changes after the given revision, then the new ones', async () => {
  const s = await start();
  try {
    await put(s.base, 'sh010', marker('m1', 'one'));
    await put(s.base, 'sh010', marker('m2', 'two'));
    const events = readEvents(s.base, 'sh010', 1, 2);
    // Give the stream time to open before the live change.
    await new Promise((resolve) => setTimeout(resolve, 100));
    await fetch(`${s.base}/api/sessions/sh010/markers/m1?clientId=b`, { method: 'DELETE' });
    const [first, second] = await events;
    assert.equal(first.type, 'upsert');
    assert.equal(first.rev, 2);
    assert.equal(first.marker.id, 'm2');
    assert.equal(first.clientId, 'a');
    assert.equal(second.type, 'delete');
    assert.equal(second.rev, 3);
    assert.equal(second.id, 'm1');
    assert.equal(second.clientId, 'b');
  } finally {
    await s.stop();
  }
});

test('a deletion is remembered, and undone by the marker coming back', async () => {
  const s = await start();
  try {
    await put(s.base, 'sh010', marker('m1', 'one'));
    await fetch(`${s.base}/api/sessions/sh010/markers/m1`, { method: 'DELETE' });
    let state = await (await fetch(`${s.base}/api/sessions/sh010`)).json();
    assert.deepEqual(state.markers, []);
    assert.deepEqual(state.deleted, ['m1']);

    await put(s.base, 'sh010', marker('m1', 'one again'));
    state = await (await fetch(`${s.base}/api/sessions/sh010`)).json();
    assert.equal(state.markers[0].text, 'one again');
    assert.deepEqual(state.deleted, []);
  } finally {
    await s.stop();
  }
});

test('the last change received wins', async () => {
  const s = await start();
  try {
    await put(s.base, 'sh010', marker('m1', 'from a'), 'a');
    await put(s.base, 'sh010', marker('m1', 'from b'), 'b');
    const state = await (await fetch(`${s.base}/api/sessions/sh010`)).json();
    assert.equal(state.markers.length, 1);
    assert.equal(state.markers[0].text, 'from b');
  } finally {
    await s.stop();
  }
});

test('sessions are kept across a restart, with the changes for catching up', async () => {
  const dataDir = await mkdtemp(path.join(tmpdir(), 'djv-sync-'));
  let s = await start({ dataDir });
  await put(s.base, 'sh010', marker('m1', 'kept'));
  await fetch(`${s.base}/api/sessions/sh010/markers/m0`, { method: 'DELETE' });
  await s.stop({ keepData: true });

  s = await start({ dataDir });
  try {
    const state = await (await fetch(`${s.base}/api/sessions/sh010`)).json();
    assert.equal(state.rev, 2);
    assert.equal(state.markers[0].text, 'kept');
    assert.deepEqual(state.deleted, ['m0']);

    // The changes before the restart are kept, so a client that was away
    // catches up rather than reading the state again.
    const missed = await readEvents(s.base, 'sh010', 0, 2);
    assert.deepEqual(missed.map((i) => [i.rev, i.type]), [[1, 'upsert'], [2, 'delete']]);

    // Up to date: nothing to catch up on, and the stream stays open for
    // what comes next.
    const events = readEvents(s.base, 'sh010', 2, 1);
    await new Promise((resolve) => setTimeout(resolve, 100));
    await put(s.base, 'sh010', marker('m2', 'new'));
    const [next] = await events;
    assert.equal(next.rev, 3);
  } finally {
    await s.stop();
  }
});

test('a change is written before it is answered', async () => {
  const dataDir = await mkdtemp(path.join(tmpdir(), 'djv-sync-'));
  const first = await start({ dataDir });
  await put(first.base, 'sh010', marker('m1', 'at once'));

  // A second server on the same database, while the first still runs: what
  // it reads is only what was written.
  const s = await start({ dataDir });
  try {
    const state = await (await fetch(`${s.base}/api/sessions/sh010`)).json();
    assert.equal(state.rev, 1);
    assert.equal(state.markers[0].text, 'at once');
  } finally {
    await first.stop({ keepData: true });
    await s.stop();
  }
});

test('a client too far behind is told to read the state again', async () => {
  const dataDir = await mkdtemp(path.join(tmpdir(), 'djv-sync-'));
  // A session from before the database: its changes were never kept.
  await writeFile(path.join(dataDir, 'sh010.json'), JSON.stringify({
    session: 'sh010',
    rev: 5,
    markers: [marker('m1', 'old')],
    deleted: {},
  }));
  const s = await start({ dataDir });
  try {
    const [reset] = await readEvents(s.base, 'sh010', 0, 1);
    assert.deepEqual(reset, { type: 'reset', rev: 5 });
  } finally {
    await s.stop();
  }
});

test('sessions kept as JSON files are brought into the database once', async () => {
  const dataDir = await mkdtemp(path.join(tmpdir(), 'djv-sync-'));
  await writeFile(path.join(dataDir, 'sh010.json'), JSON.stringify({
    session: 'sh010',
    rev: 8,
    markers: [marker('m1', 'one'), marker('m2', 'two')],
    deleted: { m0: 3 },
  }));
  await writeFile(path.join(dataDir, 'broken.json'), 'not json');
  let s = await start({ dataDir });
  await s.stop({ keepData: true });

  const files = await readdir(dataDir);
  assert.ok(files.includes('sh010.json.imported'));
  assert.ok(!files.includes('sh010.json'));
  // A file that cannot be read is left where it is, for someone to look at.
  assert.ok(files.includes('broken.json'));

  s = await start({ dataDir });
  try {
    const state = await (await fetch(`${s.base}/api/sessions/sh010`)).json();
    assert.equal(state.rev, 8);
    assert.deepEqual(state.markers.map((i) => i.text), ['one', 'two']);
    assert.deepEqual(state.deleted, ['m0']);
    assert.equal(state.media, null);

    // The numbering carries on from the imported revision.
    assert.deepEqual(await (await put(s.base, 'sh010', marker('m3', 'three'))).json(), { rev: 9 });
  } finally {
    await s.stop();
  }
});

test('the list of sessions', async () => {
  const s = await start();
  try {
    await put(s.base, 'b-session', marker('m1', 'one'));
    await put(s.base, 'a-session', marker('m1', 'one'));
    await put(s.base, 'a-session', marker('m2', 'two'));
    const { sessions } = await (await fetch(`${s.base}/api/sessions`)).json();
    assert.deepEqual(sessions.map((i) => [i.session, i.markers, i.media]), [['a-session', 2, null], ['b-session', 1, null]]);
  } finally {
    await s.stop();
  }
});

test('names that are not safe are refused', async () => {
  const s = await start();
  try {
    assert.equal((await fetch(`${s.base}/api/sessions/..`)).status, 404);
    assert.equal((await fetch(`${s.base}/api/sessions/a%2F..%2Fb`)).status, 400);
    assert.equal((await fetch(`${s.base}/api/sessions/has%20space`)).status, 400);
    assert.equal((await put(s.base, 'sh010', marker('bad id', 'x'))).status, 400);
  } finally {
    await s.stop();
  }
});

test('a change that is not a marker is refused', async () => {
  const s = await start();
  try {
    let res = await fetch(`${s.base}/api/sessions/sh010/markers/m1`, {
      method: 'PUT',
      body: 'not json',
    });
    assert.equal(res.status, 400);
    res = await fetch(`${s.base}/api/sessions/sh010/markers/m1`, {
      method: 'PUT',
      body: JSON.stringify({ marker: marker('m2', 'mismatch') }),
    });
    assert.equal(res.status, 400);
    res = await fetch(`${s.base}/api/sessions/sh010/markers/m1`, {
      method: 'PUT',
      body: JSON.stringify({ clientId: 'a' }),
    });
    assert.equal(res.status, 400);
    const state = await (await fetch(`${s.base}/api/sessions/sh010`)).json();
    assert.equal(state.rev, 0);
  } finally {
    await s.stop();
  }
});

function putMedia(base, session, media, clientId = 'a') {
  return fetch(`${base}/api/sessions/${session}/media`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ clientId, media }),
  });
}

const shot = {
  path: 'shows/abc/sh010/sh010_v003.mov',
  pathAbsolute: '//nas/media/shows/abc/sh010/sh010_v003.mov',
  name: 'sh010_v003.mov',
  sharedBy: 'someone',
};

test('a session keeps the media it is about', async () => {
  const dataDir = await mkdtemp(path.join(tmpdir(), 'djv-sync-'));
  let s = await start({ dataDir });
  try {
    await put(s.base, 'sh010', marker('m1', 'one'));
    const events = readEvents(s.base, 'sh010', 1, 2);
    await new Promise((resolve) => setTimeout(resolve, 100));

    let res = await putMedia(s.base, 'sh010', shot, 'b');
    assert.equal(res.status, 200);
    assert.deepEqual(await res.json(), { rev: 2 });
    res = await fetch(`${s.base}/api/sessions/sh010/media?clientId=c`, { method: 'DELETE' });
    assert.deepEqual(await res.json(), { rev: 3 });

    const [set, cleared] = await events;
    assert.deepEqual(set, { type: 'media', rev: 2, clientId: 'b', user: 'lldjv', media: shot });
    assert.deepEqual(cleared, { type: 'media', rev: 3, clientId: 'c', user: 'lldjv', media: null });

    await putMedia(s.base, 'sh010', shot);
    const state = await (await fetch(`${s.base}/api/sessions/sh010`)).json();
    assert.deepEqual(state.media, shot);
    assert.equal(state.markers.length, 1);
    const { sessions } = await (await fetch(`${s.base}/api/sessions`)).json();
    assert.equal(sessions[0].media, 'sh010_v003.mov');
  } finally {
    await s.stop({ keepData: true });
  }

  s = await start({ dataDir });
  try {
    const state = await (await fetch(`${s.base}/api/sessions/sh010`)).json();
    assert.deepEqual(state.media, shot);
    assert.equal(state.rev, 4);
  } finally {
    await s.stop();
  }
});

test('media that is not a path is refused', async () => {
  const s = await start();
  try {
    assert.equal((await putMedia(s.base, 'sh010', null)).status, 400);
    assert.equal((await putMedia(s.base, 'sh010', ['a'])).status, 400);
    assert.equal((await putMedia(s.base, 'sh010', { name: 'no path' })).status, 400);
    assert.equal((await putMedia(s.base, 'sh010', { path: 12 })).status, 400);
    assert.equal((await putMedia(s.base, 'sh010', { path: 'x'.repeat(5000) })).status, 400);
    // A path relative to the media folder alone is enough.
    assert.equal((await putMedia(s.base, 'sh010', { path: 'a.mov' })).status, 200);
    const state = await (await fetch(`${s.base}/api/sessions/sh010`)).json();
    assert.deepEqual(state.media, { path: 'a.mov', pathAbsolute: '', name: '', sharedBy: '' });
  } finally {
    await s.stop();
  }
});
