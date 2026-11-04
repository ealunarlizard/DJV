// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

// The drawings of a session, and who made and changed the markers and the
// drawings: the server writes that in itself, from the user signed in.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdtemp } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';

// The fetch that signs in as the test server's administrator.
import { fetch, login, start } from './helpers.js';
import { ROOT_USER } from '../users.js';

function drawing(id, points, extra = {}) {
  return {
    id,
    media: 'shots/sh010.mov',
    space: 'image',
    time: { value: 12, rate: 24 },
    strokes: [{ color: [1, 0.365, 0.02, 1], width: 4, widthSpace: 'image', points }],
    ...extra,
  };
}

function marker(id, text, extra = {}) {
  return { id, text, color: [0, 0.6, 0, 1], ...extra };
}

function bearer(token, headers = {}) {
  return token ? { Authorization: `Bearer ${token}`, ...headers } : headers;
}

// A change, as the user with the token given, or as the administrator
// without one.
function putItem(base, session, kind, key, item, { token = '', clientId = 'a' } = {}) {
  return fetch(`${base}/api/sessions/${session}/${kind}/${item.id}`, {
    method: 'PUT',
    headers: bearer(token, { 'Content-Type': 'application/json' }),
    body: JSON.stringify({ clientId, [key]: item }),
  });
}

function putDrawing(base, session, d, options) {
  return putItem(base, session, 'drawings', 'drawing', d, options);
}

function putMarker(base, session, m, options) {
  return putItem(base, session, 'markers', 'marker', m, options);
}

async function state(base, session, token = '') {
  return (await fetch(`${base}/api/sessions/${session}`, { headers: bearer(token) })).json();
}

async function addUser(s, username, password) {
  const res = await fetch(`${s.base}/api/admin/users/${username}`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ name: '', password }),
  });
  assert.equal(res.status, 200, `adding ${username}`);
  const signedIn = await login(s.base, username, password);
  assert.equal(signedIn.status, 200, `signing in as ${username}`);
  return (await signedIn.json()).token;
}

// Read the first events of a stream.
async function readEvents(base, session, since, count) {
  const controller = new AbortController();
  const res = await fetch(`${base}/api/sessions/${session}/events?since=${since}`, { signal: controller.signal });
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

test('a drawing is in the state, apart from the markers, and numbered with them', async () => {
  const s = await start();
  try {
    await putMarker(s.base, 'sh010', marker('m1', 'Too cold'));
    const res = await putDrawing(s.base, 'sh010', drawing('d1', [1, 2, 3, 4]));
    assert.equal(res.status, 200);
    assert.deepEqual(await res.json(), { rev: 2 });

    const st = await state(s.base, 'sh010');
    assert.equal(st.rev, 2);
    assert.equal(st.markers.length, 1);
    assert.equal(st.drawings.length, 1);
    assert.equal(st.drawings[0].id, 'd1');
    assert.equal(st.drawings[0].media, 'shots/sh010.mov');
    assert.deepEqual(st.drawings[0].strokes[0].points, [1, 2, 3, 4]);
    assert.deepEqual(st.deletedDrawings, []);

    const [first, second] = await readEvents(s.base, 'sh010', 0, 2);
    assert.equal(first.type, 'upsert');
    assert.equal(second.type, 'upsert-drawing');
    assert.equal(second.rev, 2);
    assert.equal(second.drawing.id, 'd1');
    assert.equal(second.clientId, 'a');
    assert.equal(second.user, ROOT_USER);
  } finally {
    await s.stop();
  }
});

test('a deleted drawing is remembered apart from the deleted markers', async () => {
  const s = await start();
  try {
    await putDrawing(s.base, 'sh010', drawing('d1', [1, 2]));
    const res = await fetch(`${s.base}/api/sessions/sh010/drawings/d1?clientId=b`, { method: 'DELETE' });
    assert.equal(res.status, 200);
    let st = await state(s.base, 'sh010');
    assert.deepEqual(st.drawings, []);
    assert.deepEqual(st.deletedDrawings, ['d1']);
    assert.deepEqual(st.deleted, []);

    const events = await readEvents(s.base, 'sh010', 1, 1);
    assert.equal(events[0].type, 'delete-drawing');
    assert.equal(events[0].id, 'd1');
    assert.equal(events[0].clientId, 'b');

    // Undone by the drawing coming back, as an undo in DJV sends it.
    await putDrawing(s.base, 'sh010', drawing('d1', [1, 2]));
    st = await state(s.base, 'sh010');
    assert.equal(st.drawings.length, 1);
    assert.deepEqual(st.deletedDrawings, []);
  } finally {
    await s.stop();
  }
});

test('drawings are kept across a restart', async () => {
  const dataDir = await mkdtemp(path.join(tmpdir(), 'djv-sync-'));
  let s = await start({ dataDir });
  await putDrawing(s.base, 'sh010', drawing('d1', [5, 6]));
  await fetch(`${s.base}/api/sessions/sh010/drawings/d0`, { method: 'DELETE' });
  await s.stop({ keepData: true });

  s = await start({ dataDir });
  try {
    const st = await state(s.base, 'sh010');
    assert.equal(st.rev, 2);
    assert.deepEqual(st.drawings[0].strokes[0].points, [5, 6]);
    assert.deepEqual(st.deletedDrawings, ['d0']);
  } finally {
    await s.stop();
  }
});

test('a drawing that does not say which video it is on is refused', async () => {
  const s = await start();
  try {
    let res = await putDrawing(s.base, 'sh010', drawing('d1', [1, 2], { media: '' }));
    assert.equal(res.status, 400);
    res = await putDrawing(s.base, 'sh010', drawing('d1', [1, 2], { media: 3 }));
    assert.equal(res.status, 400);
    res = await fetch(`${s.base}/api/sessions/sh010/drawings/d1`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ clientId: 'a', marker: drawing('d1', [1, 2]) }),
    });
    assert.equal(res.status, 400);
    res = await fetch(`${s.base}/api/sessions/sh010/drawings/d2`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ clientId: 'a', drawing: drawing('d3', [1, 2]) }),
    });
    assert.equal(res.status, 400);
    // Only markers and drawings: not whatever an object happens to have.
    res = await fetch(`${s.base}/api/sessions/sh010/constructor/d1`, {
      method: 'PUT',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ clientId: 'a', constructor: {} }),
    });
    assert.equal(res.status, 404);
  } finally {
    await s.stop();
  }
});

test('the server says who made a marker and who changed it last', async () => {
  const s = await start();
  try {
    const anna = await addUser(s, 'anna', 'anna-password');

    // Anna's, whoever the client says wrote it.
    await putMarker(s.base, 'sh010', marker('m1', 'Too cold', {
      author: 'someone-else', created: '2026-01-02T03:04:05Z', modified: '', modifiedBy: 'someone-else',
    }), { token: anna });
    let [m] = (await state(s.base, 'sh010', anna)).markers;
    assert.equal(m.author, 'anna');
    assert.equal(m.created, '2026-01-02T03:04:05Z');
    assert.equal(m.modified, undefined);
    assert.equal(m.modifiedBy, undefined);

    // Changed by the administrator: still Anna's, and changed by them now.
    await putMarker(s.base, 'sh010', marker('m1', 'Too warm', { author: ROOT_USER, created: '2027-01-01T00:00:00Z' }));
    [m] = (await state(s.base, 'sh010', anna)).markers;
    assert.equal(m.author, 'anna');
    assert.equal(m.created, '2026-01-02T03:04:05Z');
    assert.equal(m.modifiedBy, ROOT_USER);
    assert.match(m.modified, /^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$/);
    const modified = m.modified;

    // Sent again with the same content by Anna, as a DJV that reads less
    // of a marker than the server keeps would: nothing changed, so the
    // administrator is still the last to have changed it.
    await putMarker(s.base, 'sh010', marker('m1', 'Too warm'), { token: anna });
    [m] = (await state(s.base, 'sh010', anna)).markers;
    assert.equal(m.author, 'anna');
    assert.equal(m.modifiedBy, ROOT_USER);
    assert.equal(m.modified, modified);

    // The event carries the marker as the server keeps it.
    const events = await readEvents(s.base, 'sh010', 0, 3);
    assert.equal(events[1].marker.modifiedBy, ROOT_USER);
    assert.equal(events[1].marker.author, 'anna');
  } finally {
    await s.stop();
  }
});

test('the server says who made a drawing and who changed it last', async () => {
  const s = await start();
  try {
    const anna = await addUser(s, 'anna', 'anna-password');
    await putDrawing(s.base, 'sh010', drawing('d1', [1, 2, 3, 4], { author: ROOT_USER }), { token: anna });
    let [d] = (await state(s.base, 'sh010', anna)).drawings;
    assert.equal(d.author, 'anna');
    assert.ok(d.created);
    assert.equal(d.modifiedBy, undefined);

    // The administrator's eraser takes a stroke out of Anna's drawing.
    await putDrawing(s.base, 'sh010', drawing('d1', [], { strokes: [] }));
    [d] = (await state(s.base, 'sh010', anna)).drawings;
    assert.equal(d.author, 'anna');
    assert.equal(d.modifiedBy, ROOT_USER);
    assert.deepEqual(d.strokes, []);
  } finally {
    await s.stop();
  }
});

test('the list of sessions counts the drawings', async () => {
  const s = await start();
  try {
    await putDrawing(s.base, 'sh010', drawing('d1', [1, 2]));
    await putDrawing(s.base, 'sh010', drawing('d2', [3, 4]));
    await putMarker(s.base, 'sh010', marker('m1', 'x'));
    const { sessions } = await (await fetch(`${s.base}/api/sessions`)).json();
    const session = sessions.find((i) => i.session === 'sh010');
    assert.equal(session.markers, 1);
    assert.equal(session.drawings, 2);
  } finally {
    await s.stop();
  }
});
