// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

// The media folder: the folder of the server's host that is shared to the
// studio as the media share, mounted into the server so that the portal can
// list what is there and put new videos in it.
//
// The folder is on a Windows host, shared over SMB, and read by DJV on
// Windows PCs, so a path the server makes in it has to be a good Windows path
// as well as a good path here: no characters NTFS refuses, no names Windows
// reserves for devices, no trailing dots or spaces, which Windows quietly
// drops. Paths are relative to the folder, with forward slashes, as the
// session media's "path" is (see README.md).

import { readdirSync, statSync, statfsSync } from 'node:fs';
import path from 'node:path';

// What the portal lists and takes uploads of: the containers DJV plays as
// video. Image sequences are left out: one shot would be a thousand entries.
export const VIDEO_EXTENSIONS = new Set([
  '.mov', '.mp4', '.m4v', '.mxf', '.mkv', '.avi', '.webm', '.mpg', '.mpeg', '.ts', '.y4m', '.wav',
]);

// Long enough for any sensible folder layout, and short enough that the path
// on the share, under a drive letter on each PC, stays well inside the 260
// characters older Windows programs can open.
export const MAX_MEDIA_PATH = 200;

// Where unfinished uploads are kept, inside the media folder so that a
// finished one is moved into place rather than copied. The dot keeps it out
// of the list, and out of the way.
export const UPLOAD_DIR = '.djv-uploads';

const RESERVED = /^(con|prn|aux|nul|com[0-9¹²³]|lpt[0-9¹²³])(\..*)?$/i;
const FORBIDDEN = /[<>:"|?*\\\u0000-\u001f]/;

// Why the relative path cannot be used, or '' when it can.
export function pathProblem(value) {
  if (typeof value !== 'string' || value === '') {
    return 'The path is empty.';
  }
  if (value.length > MAX_MEDIA_PATH) {
    return `The path is longer than ${MAX_MEDIA_PATH} characters.`;
  }
  if (value.startsWith('/')) {
    return 'The path must be inside the media folder.';
  }
  if (FORBIDDEN.test(value)) {
    return 'A name in the path has a character Windows does not allow: < > : " | ? * \\';
  }
  for (const part of value.split('/')) {
    if (part === '') {
      return 'The path has an empty folder name.';
    }
    if (part === '.' || part === '..') {
      return 'The path must be inside the media folder.';
    }
    if (part.startsWith('.')) {
      return `"${part}": names starting with a dot are kept for the server.`;
    }
    if (/[. ]$/.test(part) || /^ /.test(part)) {
      return `"${part}": a name cannot start with a space or end with a dot or a space.`;
    }
    if (RESERVED.test(part)) {
      return `"${part}" is a name Windows keeps for a device.`;
    }
  }
  return '';
}

export function isVideoPath(value) {
  return VIDEO_EXTENSIONS.has(path.posix.extname(value).toLowerCase());
}

// The file or folder a relative path names, which pathProblem() must have
// passed.
export function resolveMedia(root, relative) {
  return path.join(root, ...relative.split('/'));
}

// Folders a Windows drive has at its root that are not media.
function isSystemFolder(name) {
  return name.startsWith('$') || name === 'System Volume Information';
}

// Every video in the folder and the folders below it, and every folder, as
// the share shows them. A folder that cannot be read is left out rather than
// fail the whole list.
export function listMedia(root) {
  const videos = [];
  const folders = [];
  const walk = (dir, relative) => {
    let entries;
    try {
      entries = readdirSync(dir, { withFileTypes: true });
    } catch {
      return;
    }
    for (const entry of entries) {
      if (entry.name.startsWith('.') || isSystemFolder(entry.name)) {
        continue;
      }
      const rel = relative ? `${relative}/${entry.name}` : entry.name;
      const full = path.join(dir, entry.name);
      if (entry.isDirectory()) {
        folders.push(rel);
        walk(full, rel);
      } else if (entry.isFile() && isVideoPath(entry.name)) {
        try {
          const stat = statSync(full);
          videos.push({
            path: rel,
            name: entry.name,
            folder: relative,
            size: stat.size,
            modified: stat.mtime.toISOString(),
          });
        } catch {
          // Gone between the listing and the look.
        }
      }
    }
  };
  walk(root, '');
  const byPath = (a, b) => a.localeCompare(b, undefined, { sensitivity: 'base', numeric: true });
  videos.sort((a, b) => byPath(a.path, b.path));
  folders.sort(byPath);
  return { videos, folders };
}

// The space left in the folder for uploads, in bytes, or null when the file
// system does not say.
export function freeSpace(root) {
  try {
    const stat = statfsSync(root);
    return stat.bavail * stat.bsize;
  } catch {
    return null;
  }
}

// The path of a file on the share, as a Windows PC reaches it, with forward
// slashes as the session media's "pathAbsolute" has them, from the share's
// UNC path as the admin set it (\\server\share). Empty without a share.
export function sharePath(unc, relative) {
  if (!unc) {
    return '';
  }
  return `${unc.replace(/\\/g, '/').replace(/\/+$/, '')}/${relative}`;
}
