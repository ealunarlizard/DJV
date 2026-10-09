// SPDX-License-Identifier: BSD-3-Clause
// Copyright Contributors to the DJV project.

// The people who may use the server: their names, and their passwords,
// which are only ever kept hashed.
//
// A password is hashed with scrypt, which is in Node and is slow on purpose,
// so that a copy of the configuration file is not a list of passwords. The
// hash carries its own parameters and salt, so the cost can be raised later
// without the passwords already set becoming unreadable.
//
// Signing in gives a session token, which is what DJV then sends with every
// request. Only its SHA-256 is kept, in the database: a token is random and
// long, so a fast hash is enough, and a copy of the database lets nobody in.

import { createHash, randomBytes, scrypt, scryptSync, timingSafeEqual } from 'node:crypto';
import { promisify } from 'node:util';

const scryptAsync = promisify(scrypt);

// The administrator. Its password is not set on the admin page but in a
// file on the server (see ROOT_PASSWORD_FILE in server.js), so that whoever
// runs the server can always get back in.
export const ROOT_USER = 'lldjv';

// A user name goes into URLs, logs and the admin page, so it is kept to what
// is safe in all of them, like the session names. DJV checks the same
// (models::isValidSyncUser()).
const USER_PATTERN = /^[A-Za-z0-9._-]{1,64}$/;

export const MIN_PASSWORD = 8;
export const MAX_PASSWORD = 1024;

// About 50 ms on a current machine: unnoticed when signing in, and slow
// enough to make guessing from a stolen hash expensive.
const SCRYPT = { N: 16384, r: 8, p: 1, keylen: 32 };

export function isValidUsername(name) {
  return typeof name === 'string' && USER_PATTERN.test(name) && name !== '.' && name !== '..';
}

// What is wrong with a password, or an empty string when nothing is.
export function passwordProblem(password) {
  if (typeof password !== 'string') {
    return 'The password must be text.';
  }
  if (password.length < MIN_PASSWORD) {
    return `The password must have at least ${MIN_PASSWORD} characters.`;
  }
  if (password.length > MAX_PASSWORD) {
    return `The password can have at most ${MAX_PASSWORD} characters.`;
  }
  return '';
}

function format(salt, hash) {
  return `scrypt$${SCRYPT.N}$${SCRYPT.r}$${SCRYPT.p}$${salt.toString('base64')}$${hash.toString('base64')}`;
}

function parse(stored) {
  const parts = typeof stored === 'string' ? stored.split('$') : [];
  if (parts.length !== 6 || parts[0] !== 'scrypt') {
    return null;
  }
  const [N, r, p] = parts.slice(1, 4).map((value) => Number.parseInt(value, 10));
  if (![N, r, p].every(Number.isInteger)) {
    return null;
  }
  const salt = Buffer.from(parts[4], 'base64');
  const hash = Buffer.from(parts[5], 'base64');
  return salt.length > 0 && hash.length > 0 ? { N, r, p, salt, hash } : null;
}

export function isPasswordHash(stored) {
  return parse(stored) !== null;
}

export async function hashPassword(password) {
  const salt = randomBytes(16);
  const hash = await scryptAsync(password, salt, SCRYPT.keylen, { N: SCRYPT.N, r: SCRYPT.r, p: SCRYPT.p });
  return format(salt, hash);
}

// For the server starting, which has nothing else to do while it hashes.
export function hashPasswordSync(password) {
  const salt = randomBytes(16);
  const hash = scryptSync(password, salt, SCRYPT.keylen, { N: SCRYPT.N, r: SCRYPT.r, p: SCRYPT.p });
  return format(salt, hash);
}

function options(parsed) {
  // Room for the parameters a stored hash was made with, which may be
  // higher than today's.
  return { N: parsed.N, r: parsed.r, p: parsed.p, maxmem: 256 * parsed.N * parsed.r + 1024 * 1024 };
}

export async function verifyPassword(password, stored) {
  const parsed = parse(stored);
  if (!parsed || typeof password !== 'string') {
    return false;
  }
  const hash = await scryptAsync(password, parsed.salt, parsed.hash.length, options(parsed));
  return timingSafeEqual(hash, parsed.hash);
}

export function verifyPasswordSync(password, stored) {
  const parsed = parse(stored);
  if (!parsed || typeof password !== 'string') {
    return false;
  }
  const hash = scryptSync(password, parsed.salt, parsed.hash.length, options(parsed));
  return timingSafeEqual(hash, parsed.hash);
}

export function newToken() {
  return randomBytes(32).toString('base64url');
}

export function tokenHash(token) {
  return createHash('sha256').update(token).digest('hex');
}
