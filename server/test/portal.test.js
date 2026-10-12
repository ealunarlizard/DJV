// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

// The portal: the videos in the media folder, uploading new ones in pieces,
// and the sessions, which are for the people they were made for.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdir, mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import { existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { DatabaseSync } from 'node:sqlite';
import path from 'node:path';

import { pathProblem, sharePath } from '../media.js';
import { login, start } from './helpers.js';

const CHUNK = 1024;

// A request as the user with the token given.
function as(token) {
  return (url, init = {}) => globalThis.fetch(url, {
    ...init,
    headers: {
      Authorization: `Bearer ${token}`,
      ...(init.body !== undefined && typeof init.body === 'string' ? { 'Content-Type': 'application/json' } : {}),
      ...(init.headers ?? {}),
    },
  });
}

async function json(res, status = 200) {
  const body = await res.json();
  assert.equal(res.status, status, JSON.stringify(body));
  return body;
}

// A server with a media folder holding a few files, and the users anna and
// ben signed in.
async function studio(options = {}) {
  const mediaDir = options.mediaDir ?? await mkdtemp(path.join(tmpdir(), 'djv-media-'));
  if (!options.mediaDir) {
    await mkdir(path.join(mediaDir, 'shots', 'sh010'), { recursive: true });
    await mkdir(path.join(mediaDir, '.hidden'), { recursive: true });
    await writeFile(path.join(mediaDir, 'intro.mov'), 'intro');
    await writeFile(path.join(mediaDir, 'shots', 'sh010', 'v001.mp4'), 'v001');
    await writeFile(path.join(mediaDir, 'shots', 'sh010', 'v002.mp4'), 'v002');
    await writeFile(path.join(mediaDir, 'shots', 'notes.txt'), 'not a video');
    await writeFile(path.join(mediaDir, '.hidden', 'secret.mov'), 'hidden');
  }
  // The users are kept in a configuration file, so that they are still
  // there when a test starts the server again on the same data.
  const restart = options.dataDir !== undefined;
  const dataDir = options.dataDir ?? await mkdtemp(path.join(tmpdir(), 'djv-sync-'));
  const s = await start({ uploadChunk: CHUNK, mediaDir, dataDir, configFile: path.join(dataDir, 'config.json') });
  const admin = as(s.token);
  const people = {};
  for (const [username, name] of [['anna', 'Anna'], ['ben', 'Ben'], ['cleo', 'Cleo']]) {
    if (!restart) {
      await json(await admin(`${s.base}/api/admin/users/${username}`, {
        method: 'PUT', body: JSON.stringify({ name, password: `${username}-password` }),
      }));
    }
    const res = await login(s.base, username, `${username}-password`);
    people[username] = as((await json(res)).token);
  }
  s.server.config.update({ storage: { unc: '\\\\192.168.1.40\\media', user: 'djv' } });
  return {
    ...s,
    mediaDir,
    admin,
    ...people,
    async stop(opts = {}) {
      await s.stop(opts);
      if (!opts.keepData) {
        await rm(mediaDir, { recursive: true, force: true });
      }
    },
  };
}

function piece(f, base, id, offset, data) {
  return f(`${base}/api/uploads/${id}?offset=${offset}`, { method: 'PUT', body: data });
}

test('a media path must be a plain path inside the folder that Windows can use', () => {
  assert.equal(pathProblem('shots/sh010/v001.mov'), '');
  assert.equal(pathProblem('Résumé 2026/take 1.mov'), '');
  for (const bad of [
    '', '/abs.mov', '../up.mov', 'a/../b.mov', 'a//b.mov', 'a\\b.mov', 'what?.mov', 'a:b.mov',
    '.hidden/x.mov', 'trailing./x.mov', 'x.mov ', ' x.mov', 'CON.mov', 'shots/lpt1', 'tab\there.mov',
    'x'.repeat(201),
  ]) {
    assert.notEqual(pathProblem(bad), '', bad);
  }
  assert.equal(sharePath('\\\\192.168.1.40\\media', 'a/b.mov'), '//192.168.1.40/media/a/b.mov');
  assert.equal(sharePath('', 'a/b.mov'), '');
});

test('the portal is what the server\'s address opens', async () => {
  const s = await studio();
  try {
    for (const url of ['/', '/portal']) {
      const res = await globalThis.fetch(`${s.base}${url}`);
      assert.equal(res.status, 200);
      assert.match(await res.text(), /<title>DJV review portal<\/title>/);
    }
  } finally {
    await s.stop();
  }
});

test('the videos are what the media folder holds', async () => {
  const s = await studio();
  try {
    const list = await json(await s.anna(`${s.base}/api/videos`));
    assert.equal(list.enabled, true);
    assert.deepEqual(list.videos.map((v) => v.path), ['intro.mov', 'shots/sh010/v001.mp4', 'shots/sh010/v002.mp4']);
    assert.deepEqual(list.folders, ['shots', 'shots/sh010']);
    assert.equal(list.videos[1].folder, 'shots/sh010');
    assert.equal(list.videos[1].size, 4);
    assert.equal((await globalThis.fetch(`${s.base}/api/videos`)).status, 401);
  } finally {
    await s.stop();
  }

  // Without a media folder there is nothing to list, and nowhere to upload.
  const bare = await start();
  try {
    const f = as(bare.token);
    assert.equal((await json(await f(`${bare.base}/api/videos`))).enabled, false);
    const res = await f(`${bare.base}/api/uploads`, { method: 'POST', body: JSON.stringify({ path: 'a.mov', size: 1 }) });
    assert.equal(res.status, 503);
  } finally {
    await bare.stop();
  }
});

test('a video is uploaded in pieces, in order, and appears when it is whole', async () => {
  const s = await studio();
  try {
    const data = Buffer.alloc(CHUNK * 2 + 100);
    for (let i = 0; i < data.length; ++i) {
      data[i] = i % 251;
    }
    let upload = await json(await s.anna(`${s.base}/api/uploads`, {
      method: 'POST', body: JSON.stringify({ path: 'new/take 1.mov', size: data.length }),
    }));
    assert.equal(upload.chunkSize, CHUNK);
    assert.equal(upload.received, 0);

    // Out of order, too long, or from someone else: refused.
    let res = await piece(s.anna, s.base, upload.id, CHUNK, data.subarray(CHUNK, CHUNK * 2));
    assert.equal(res.status, 409);
    assert.equal((await res.json()).received, 0);
    assert.equal((await piece(s.anna, s.base, upload.id, 0, data.subarray(0, CHUNK + 1))).status, 413);
    assert.equal((await piece(s.ben, s.base, upload.id, 0, data.subarray(0, CHUNK))).status, 403);

    assert.deepEqual(await json(await piece(s.anna, s.base, upload.id, 0, data.subarray(0, CHUNK))), { received: CHUNK, done: false });
    // Not listed while it is unfinished, but shown as on its way.
    let list = await json(await s.anna(`${s.base}/api/videos`));
    assert.ok(!list.videos.some((v) => v.path === 'new/take 1.mov'));
    assert.deepEqual(list.uploading.map((u) => u.path), ['new/take 1.mov']);

    // Starting the same file again carries on where it was.
    upload = await json(await s.anna(`${s.base}/api/uploads`, {
      method: 'POST', body: JSON.stringify({ path: 'new/take 1.mov', size: data.length }),
    }));
    assert.equal(upload.received, CHUNK);
    // Someone else cannot take the name meanwhile.
    res = await s.ben(`${s.base}/api/uploads`, { method: 'POST', body: JSON.stringify({ path: 'new/take 1.mov', size: 5 }) });
    assert.equal(res.status, 409);

    await json(await piece(s.anna, s.base, upload.id, CHUNK, data.subarray(CHUNK, CHUNK * 2)));
    const last = await json(await piece(s.anna, s.base, upload.id, CHUNK * 2, data.subarray(CHUNK * 2)));
    assert.equal(last.done, true);
    assert.equal(last.video.path, 'new/take 1.mov');
    assert.equal(last.video.folder, 'new');
    assert.deepEqual(await readFile(path.join(s.mediaDir, 'new', 'take 1.mov')), data);
    list = await json(await s.anna(`${s.base}/api/videos`));
    assert.ok(list.videos.some((v) => v.path === 'new/take 1.mov'));
    assert.deepEqual(list.uploading, []);

    // A name that is there, in any case, is not uploaded over.
    res = await s.anna(`${s.base}/api/uploads`, { method: 'POST', body: JSON.stringify({ path: 'INTRO.MOV', size: 5 }) });
    assert.equal(res.status, 409);
    assert.equal((await res.json()).code, 'exists');
    // Only videos, and only good paths.
    res = await s.anna(`${s.base}/api/uploads`, { method: 'POST', body: JSON.stringify({ path: 'notes.txt', size: 5 }) });
    assert.equal(res.status, 400);
    res = await s.anna(`${s.base}/api/uploads`, { method: 'POST', body: JSON.stringify({ path: '../x.mov', size: 5 }) });
    assert.equal(res.status, 400);
  } finally {
    await s.stop();
  }
});

test('an upload carries on after the server restarts, and can be cancelled', async () => {
  let s = await studio();
  const { dataDir, mediaDir } = s;
  try {
    const data = Buffer.from('x'.repeat(CHUNK + 10));
    const upload = await json(await s.anna(`${s.base}/api/uploads`, {
      method: 'POST', body: JSON.stringify({ path: 'later.mov', size: data.length }),
    }));
    await json(await piece(s.anna, s.base, upload.id, 0, data.subarray(0, CHUNK)));
    await s.stop({ keepData: true });

    s = await studio({ dataDir, mediaDir });
    const { uploads } = await json(await s.anna(`${s.base}/api/uploads`));
    assert.equal(uploads.length, 1);
    assert.equal(uploads[0].received, CHUNK);
    assert.deepEqual((await json(await s.ben(`${s.base}/api/uploads`))).uploads, []);
    assert.equal((await s.ben(`${s.base}/api/uploads/${upload.id}`, { method: 'DELETE' })).status, 403);

    await json(await s.anna(`${s.base}/api/uploads/${upload.id}`, { method: 'DELETE' }));
    assert.equal(existsSync(path.join(mediaDir, '.djv-uploads', `${upload.id}.part`)), false);
    assert.equal((await piece(s.anna, s.base, upload.id, CHUNK, data.subarray(CHUNK))).status, 404);
  } finally {
    await s.stop();
  }
});

test('only the administrator deletes videos, and not those in use without asking', async () => {
  const s = await studio();
  try {
    await json(await s.anna(`${s.base}/api/sessions`, {
      method: 'POST', body: JSON.stringify({ description: 'Intro', videos: ['intro.mov'] }),
    }));
    const url = `${s.base}/api/videos?path=${encodeURIComponent('intro.mov')}`;
    assert.equal((await s.anna(url, { method: 'DELETE' })).status, 403);
    const res = await s.admin(url, { method: 'DELETE' });
    assert.equal(res.status, 409);
    assert.equal((await res.json()).sessions.length, 1);
    await json(await s.admin(`${url}&force=1`, { method: 'DELETE' }));
    assert.equal(existsSync(path.join(s.mediaDir, 'intro.mov')), false);
    await json(await s.admin(`${s.base}/api/videos?path=shots%2Fsh010%2Fv002.mp4`, { method: 'DELETE' }));
    assert.equal((await s.admin(`${s.base}/api/videos?path=nothing.mov`, { method: 'DELETE' })).status, 404);
  } finally {
    await s.stop();
  }
});

test('a session made on the portal has its videos, date, description and people', async () => {
  const s = await studio();
  try {
    const made = await json(await s.anna(`${s.base}/api/sessions`, {
      method: 'POST',
      body: JSON.stringify({
        description: 'SH010 lighting, v1 vs v2',
        date: '2026-10-14',
        members: ['ben'],
        videos: ['shots/sh010/v001.mp4', 'shots/sh010/v002.mp4'],
      }),
    }));
    assert.match(made.session, /^20261014-sh010-lighting-v1-vs-v2-[a-z0-9]{4}$/);
    assert.equal(made.createdBy, 'anna');
    assert.deepEqual(made.members, ['anna', 'ben']);
    assert.equal(made.canEdit, true);
    assert.deepEqual(made.videos.map((v) => v.name), ['v001.mp4', 'v002.mp4']);

    // DJV reads the two videos as the session's media, A and B.
    const state = await json(await s.ben(`${s.base}/api/sessions/${made.session}`));
    assert.equal(state.media.path, 'shots/sh010/v001.mp4');
    assert.equal(state.media.pathAbsolute, '//192.168.1.40/media/shots/sh010/v001.mp4');
    assert.equal(state.media.sharedBy, 'Anna');
    assert.equal(state.media.compare.path, 'shots/sh010/v002.mp4');

    // Names never clash, however many are made at once.
    const many = await Promise.all(Array.from({ length: 50 }, () => s.anna(`${s.base}/api/sessions`, {
      method: 'POST', body: JSON.stringify({ description: 'Same', date: '2026-10-14', videos: ['intro.mov'] }),
    }).then((res) => json(res))));
    assert.equal(new Set(many.map((m) => m.session)).size, 50);

    // What cannot be a session is refused.
    for (const body of [
      { videos: [] },
      { videos: ['nothing.mov'] },
      { videos: ['intro.mov', 'intro.mov'] },
      { videos: ['a.mov', 'b.mov', 'c.mov'] },
      { videos: ['intro.mov'], members: ['nobody'] },
      { videos: ['intro.mov'], date: '14.10.2026' },
      { videos: ['../outside.mov'] },
    ]) {
      const res = await s.anna(`${s.base}/api/sessions`, { method: 'POST', body: JSON.stringify(body) });
      assert.equal(res.status, 400, JSON.stringify(body));
    }

    const { people } = await json(await s.ben(`${s.base}/api/people`));
    assert.deepEqual(people.map((p) => p.username).sort(), ['anna', 'ben', 'cleo', 'lldjv']);
  } finally {
    await s.stop();
  }
});

test('a session is only for the people it was made for', async () => {
  const s = await studio();
  try {
    const { session } = await json(await s.anna(`${s.base}/api/sessions`, {
      method: 'POST', body: JSON.stringify({ description: 'Private', members: ['ben'], videos: ['intro.mov'] }),
    }));
    const url = `${s.base}/api/sessions/${session}`;
    const names = async (f) => (await json(await f(`${s.base}/api/sessions`))).sessions.map((x) => x.session);
    assert.deepEqual(await names(s.ben), [session]);
    assert.deepEqual(await names(s.cleo), []);
    assert.deepEqual(await names(s.admin), [session]);

    let res = await s.cleo(url);
    assert.equal(res.status, 403);
    assert.equal((await res.json()).code, 'not-member');
    assert.equal((await s.cleo(`${url}/events`)).status, 403);
    res = await s.cleo(`${url}/markers/m1`, { method: 'PUT', body: JSON.stringify({ clientId: 'c', marker: { text: 'x' } }) });
    assert.equal(res.status, 403);
    assert.equal((await s.ben(`${url}/markers/m1`, { method: 'PUT', body: JSON.stringify({ clientId: 'c', marker: { text: 'x' } }) })).status, 200);

    // Only whoever made it, or the administrator, changes it.
    assert.equal((await s.ben(`${url}/info`, { method: 'PUT', body: JSON.stringify({ members: ['ben', 'cleo'] }) })).status, 403);
    const changed = await json(await s.anna(`${url}/info`, {
      method: 'PUT', body: JSON.stringify({ description: 'Now with Cleo', members: ['cleo'], date: '2026-11-01' }),
    }));
    assert.deepEqual(changed.members, ['anna', 'cleo']);
    assert.equal(changed.date, '2026-11-01');
    assert.equal((await s.cleo(url)).status, 200);
    assert.equal((await s.ben(url)).status, 403);
    assert.equal((await s.admin(`${url}/info`, { method: 'PUT', body: JSON.stringify({ description: 'By the admin' }) })).status, 200);
    assert.equal((await s.cleo(`${url}`, { method: 'DELETE' })).status, 403);
  } finally {
    await s.stop();
  }
});

test('a name first opened from DJV is that user\'s session', async () => {
  const s = await studio();
  try {
    // Only looking does not list it, but it is anna's from then on.
    await json(await s.anna(`${s.base}/api/sessions/typed-name`));
    assert.equal((await s.ben(`${s.base}/api/sessions/typed-name`)).status, 403);
    const [mine] = (await json(await s.anna(`${s.base}/api/sessions`))).sessions;
    assert.equal(mine.session, 'typed-name');
    assert.equal(mine.createdBy, 'anna');
    assert.deepEqual(mine.members, ['anna']);
    assert.equal(mine.description, '');
    assert.match(mine.date, /^\d{4}-\d{2}-\d{2}$/);

    // She asks Ben in, and gives it its videos.
    const changed = await json(await s.anna(`${s.base}/api/sessions/typed-name/info`, {
      method: 'PUT', body: JSON.stringify({ members: ['ben'], videos: ['intro.mov', 'shots/sh010/v001.mp4'] }),
    }));
    assert.equal(changed.videos.length, 2);
    assert.equal((await json(await s.ben(`${s.base}/api/sessions/typed-name`))).media.compare.name, 'v001.mp4');
  } finally {
    await s.stop();
  }
});

test('a removed session tells the DJVs in it, and is gone', async () => {
  const s = await studio();
  try {
    const { session } = await json(await s.anna(`${s.base}/api/sessions`, {
      method: 'POST', body: JSON.stringify({ description: 'Gone soon', members: ['ben'], videos: ['intro.mov'] }),
    }));
    const url = `${s.base}/api/sessions/${session}`;
    await json(await s.ben(`${url}/markers/m1`, { method: 'PUT', body: JSON.stringify({ clientId: 'c', marker: { text: 'x' } }) }));
    const controller = new AbortController();
    const stream = await s.ben(`${url}/events?since=2`, { signal: controller.signal });
    assert.equal(stream.status, 200);
    const text = (async () => {
      let out = '';
      for await (const chunk of stream.body) {
        out += new TextDecoder().decode(chunk);
      }
      return out;
    })();
    // The stream is open once it is listed.
    for (let i = 0; i < 50; ++i) {
      const [x] = (await json(await s.anna(`${s.base}/api/sessions`))).sessions;
      if (x.connected.includes('ben')) {
        break;
      }
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
    assert.equal((await s.ben(url, { method: 'DELETE' })).status, 403);
    await json(await s.anna(url, { method: 'DELETE' }));
    assert.match(await text, /"type":"removed"/);
    assert.deepEqual((await json(await s.anna(`${s.base}/api/sessions`))).sessions, []);
    assert.equal((await s.anna(url, { method: 'DELETE' })).status, 404);
  } finally {
    await s.stop();
  }
});

test('the sessions of an older server are open to everyone', async () => {
  const dataDir = await mkdtemp(path.join(tmpdir(), 'djv-sync-'));
  // A database as server 2.0 left it.
  const db = new DatabaseSync(path.join(dataDir, 'djv-sync.db'));
  db.exec(`
    CREATE TABLE sessions (name TEXT PRIMARY KEY, rev INTEGER NOT NULL, media TEXT, created TEXT NOT NULL, updated TEXT NOT NULL);
    INSERT INTO sessions VALUES ('old-one', 3, NULL, '2026-10-01T00:00:00Z', '2026-10-01T00:00:00Z');
    PRAGMA user_version = 2;
  `);
  db.close();
  const t = await start({ dataDir });
  try {
    await json(await as(t.token)(`${t.base}/api/admin/users/anna`, {
      method: 'PUT', body: JSON.stringify({ password: 'anna-password' }),
    }));
    const anna = as((await json(await login(t.base, 'anna', 'anna-password'))).token);
    const { sessions } = await json(await anna(`${t.base}/api/sessions`));
    assert.equal(sessions.length, 1);
    assert.equal(sessions[0].open, true);
    assert.equal(sessions[0].members, null);
    assert.equal(sessions[0].canEdit, false);
    assert.equal((await json(await anna(`${t.base}/api/sessions/old-one`))).rev, 3);
  } finally {
    await t.stop();
  }
});
