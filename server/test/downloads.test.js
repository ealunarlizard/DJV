// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

// The download page at /app: the DJV packages the server carries, offered
// to anyone without signing in, the newest version's Windows installer
// first.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { mkdir, mkdtemp, rm, utimes, writeFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';

import { start } from './helpers.js';

// A request with no sign-in at all, which is how a PC new to the studio
// asks.
const anonymous = (url, init) => globalThis.fetch(url, { redirect: 'manual', ...init });

async function folder(files) {
  const dir = await mkdtemp(path.join(tmpdir(), 'djv-downloads-'));
  for (const [name, content] of Object.entries(files)) {
    await writeFile(path.join(dir, name), content);
  }
  return dir;
}

test('the newest version\'s Windows installer is the one offered', async (t) => {
  const downloadsDir = await folder({
    'djv-3.7.4-windows-amd64.exe': 'old installer',
    'djv-3.7.5-windows-amd64.zip': 'new zip',
    'djv-3.7.5-windows-amd64.exe': 'new installer',
    'djv-3.7.5-dev-windows-amd64.exe': 'a build before the release',
    'djv-3.7.10-linux-x86_64.tar.gz': 'linux',
    'notes.txt': 'not a package',
    '.hidden': 'never listed',
  });
  // A newer file is not a newer version.
  await utimes(path.join(downloadsDir, 'djv-3.7.4-windows-amd64.exe'), new Date(), new Date(Date.now() + 60000));
  await mkdir(path.join(downloadsDir, 'folder'));
  const s = await start({ downloadsDir, signIn: false });
  t.after(async () => {
    await s.stop();
    await rm(downloadsDir, { recursive: true, force: true });
  });

  let res = await anonymous(`${s.base}/api/downloads`);
  assert.equal(res.status, 200);
  const { latest, files } = await res.json();
  assert.deepEqual(files.map((file) => file.file), [
    'djv-3.7.10-linux-x86_64.tar.gz',
    'djv-3.7.5-windows-amd64.exe',
    'djv-3.7.5-windows-amd64.zip',
    'djv-3.7.5-dev-windows-amd64.exe',
    'djv-3.7.4-windows-amd64.exe',
    'notes.txt',
  ]);
  assert.equal(files[0].version, '3.7.10');
  // 3.7.10 has no Windows installer, so its first package is offered.
  assert.equal(latest.file, 'djv-3.7.10-linux-x86_64.tar.gz');
  assert.equal(latest.system, 'linux');
  assert.equal(latest.kind, 'archive');

  await rm(path.join(downloadsDir, 'djv-3.7.10-linux-x86_64.tar.gz'));
  const again = await (await anonymous(`${s.base}/api/downloads`)).json();
  assert.equal(again.latest.file, 'djv-3.7.5-windows-amd64.exe');
  assert.equal(again.latest.version, '3.7.5');
  assert.equal(again.latest.kind, 'installer');
  assert.equal(again.latest.url, '/app/djv-3.7.5-windows-amd64.exe');
  assert.equal(again.latest.size, 'new installer'.length);
  assert.deepEqual(again.files.map((file) => file.version), ['3.7.5', '3.7.5', '3.7.5-dev', '3.7.4', '']);

  res = await anonymous(`${s.base}/app/latest`);
  assert.equal(res.status, 302);
  assert.equal(res.headers.get('location'), '/app/djv-3.7.5-windows-amd64.exe');

  res = await anonymous(`${s.base}/app/djv-3.7.5-windows-amd64.exe`);
  assert.equal(res.status, 200);
  assert.equal(res.headers.get('content-disposition'), 'attachment; filename="djv-3.7.5-windows-amd64.exe"');
  assert.equal(res.headers.get('content-length'), String('new installer'.length));
  assert.equal(await res.text(), 'new installer');

  res = await anonymous(`${s.base}/app/djv-3.7.5-windows-amd64.zip`, { method: 'HEAD' });
  assert.equal(res.status, 200);
  assert.equal(res.headers.get('content-length'), String('new zip'.length));
  assert.equal(await res.text(), '');
});

test('only what the folder lists is served', async (t) => {
  const parent = await mkdtemp(path.join(tmpdir(), 'djv-downloads-'));
  const downloadsDir = path.join(parent, 'downloads');
  await mkdir(downloadsDir);
  await writeFile(path.join(parent, 'secret.txt'), 'outside');
  await writeFile(path.join(downloadsDir, '.hidden'), 'hidden');
  await mkdir(path.join(downloadsDir, 'folder'));
  const s = await start({ downloadsDir, signIn: false });
  t.after(async () => {
    await s.stop();
    await rm(parent, { recursive: true, force: true });
  });

  for (const name of ['..%2Fsecret.txt','.hidden', 'folder', 'missing.exe', 'latest']) {
    const res = await anonymous(`${s.base}/app/${name}`);
    assert.equal(res.status, 404, name);
    await res.body?.cancel();
  }
  const res = await anonymous(`${s.base}/app/folder/x`);
  assert.equal(res.status, 404);
  await res.body?.cancel();
});

test('a server with no download folder offers nothing, and still serves the page', async (t) => {
  const s = await start({ downloadsDir: path.join(tmpdir(), 'djv-downloads-not-there'), signIn: false });
  t.after(() => s.stop());

  let res = await anonymous(`${s.base}/api/downloads`);
  assert.deepEqual(await res.json(), { latest: null, files: [] });

  res = await anonymous(`${s.base}/app/latest`);
  assert.equal(res.status, 404);
  await res.body?.cancel();

  res = await anonymous(`${s.base}/app`);
  assert.equal(res.status, 200);
  assert.match(res.headers.get('content-type'), /^text\/html/);
  assert.match(await res.text(), /Download DJV/);
});
