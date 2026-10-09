# Change requests: DJV review on a studio LAN

The change requests for running DJV reviews on a studio network: what was
asked for, what was agreed, and where it stands. The user and admin guide
is the *Studio network* page of the manual (`docs/lan-setup.html`); what
changed in each release is in [CHANGELOG.md](CHANGELOG.md).

This file and the changelog are for the team. They are not part of the
published manual: the docs install copies only the HTML pages and their
assets.

## How to use this log

- Each request gets the next number, `CR-NNN`, and keeps it for good, even
  when rejected.
- A request is added as **Proposed**. It becomes **Approved** when it is
  agreed to be done, **In progress** while being worked on, and
  **Implemented** when it is on the branch with its tests. A request that
  will not be done is **Rejected**, with the reason.
- When a request is implemented, fill in the commits, the files, and how
  it was verified. Then add a line to the changelog citing it.
- Acceptance criteria are written so that someone else can check them.
  "Verified" says which ones were checked, and how: by automated tests, or
  by hand on the studio network.

### Template

```markdown
## CR-NNN: Title

| | |
|---|---|
| **Status** | Proposed / Approved / In progress / Implemented / Rejected |
| **Requested** | YYYY-MM-DD, by whom |
| **Implemented** | YYYY-MM-DD, branch, commits |

**Description.** What is wanted, and why.

**Acceptance criteria.**
1. ...

**Implementation.** How it was done, and the main files.

**Verified.** Which criteria were checked, and how.
```

## Summary

| CR | Title | Status | Commits |
|---|---|---|---|
| [CR-001](#cr-001-live-marker-sync) | Live marker sync | Implemented | `e5b5680eb` |
| [CR-002](#cr-002-persistent-sessions) | Persistent sessions (SQLite) | Implemented | `e5b5680eb` |
| [CR-003](#cr-003-session-media-and-auto-load) | Session media and auto-load | Implemented | `e5b5680eb` |
| [CR-004](#cr-004-client-identity-and-server-registry) | Client identity and server registry | Implemented; registry superseded by CR-014 | `b14b81bb4`, `5b7fda4d8`, `24756d3db` |
| [CR-005](#cr-005-server-admin-page) | Server admin page | Implemented; reworked by CR-014 | `b14b81bb4` |
| [CR-006](#cr-006-media-share-mapped-to-a-drive) | Media share mapped to a drive | Implemented (LAN check pending) | `24756d3db` |
| [CR-007](#cr-007-djv-version-check) | DJV version check | Implemented | `b14b81bb4`, `24756d3db` |
| [CR-008](#cr-008-lan-documentation-and-logs) | LAN documentation and logs | Implemented | `a8832511f` |
| [CR-009](#cr-009-session-picker) | Session picker | Proposed | |
| [CR-010](#cr-010-encrypted-connections) | Encrypted connections | Proposed | |
| [CR-011](#cr-011-shared-drawings) | Shared drawings | Proposed | |
| [CR-012](#cr-012-open-from-the-media-drive) | Open from the media drive | Proposed | |
| [CR-013](#cr-013-docker-package-for-the-sync-server) | Docker package for the sync server | Implemented | this branch, server commit |
| [CR-014](#cr-014-user-accounts-with-passwords) | User accounts with passwords | Implemented | this branch, server and DJV commits |
| [CR-015](#cr-015-share-credentials-from-the-server) | Share credentials from the server | Implemented (LAN check pending) | this branch, DJV commit |
| [CR-016](#cr-016-sharing-the-media-folder) | Sharing the media folder | Implemented (share check pending) | this branch, server and docs commits |
| [CR-017](#cr-017-notes-signed-by-the-signed-in-user) | Notes signed by the signed-in user | Proposed | |

## CR-001: Live marker sync

| | |
|---|---|
| **Status** | Implemented |
| **Requested** | 2026-10-07 |
| **Implemented** | 2026-10-07, `feature/lan-server`, `e5b5680eb` |

**Description.** Several reviewers add notes (review markers) to the same
media at the same time, each in their own DJV, and see each other's notes
as they are written.

**Acceptance criteria.**
1. An addition, edit or deletion made in one DJV appears in the others
   connected to the same session within a second.
2. Two edits of the same marker at once end the same everywhere: the last
   one the server received wins.
3. A DJV that loses the connection keeps its changes, reconnects on its
   own, and sends them.
4. The session is saved in the review (`.djvr`), so opening the review
   joins it again.

**Implementation.**
- `server/server.js` is a Node.js server with no dependencies. It numbers
  the changes and passes them on as server-sent events.
- `lib/djv/Models/SyncModel.*` holds the client, which runs two network
  threads, and `SyncMerger`, which holds the merge rules.
- The Review tool has a Sync section, and there are
  `Review/SyncConnect` and `Review/SyncDisconnect` commands.

**Verified.** `server/test/server.test.js`, plus `djv-test SyncModel` with
`DJV_SYNC_TEST_SERVER`: two clients against a running server.

## CR-002: Persistent sessions

| | |
|---|---|
| **Status** | Implemented |
| **Requested** | 2026-10-07 |
| **Implemented** | 2026-10-07, `feature/lan-server`, `e5b5680eb` |

**Description.** The server keeps its sessions in a database, so that
nothing is lost when it stops, however it stops.

**Acceptance criteria.**
1. A change is stored before the server answers it.
2. After a restart, the sessions are all there, and a client that was away
   catches up from the changes kept, rather than reading everything again.
3. Sessions kept as JSON files by the first version of the server are
   brought in once.

**Implementation.**
- The database is a single SQLite file (`node:sqlite`, Node 22.13 or
  newer), `server/data/djv-sync.db`, in write-ahead mode.
- Each change is written in one transaction, and the last 1000 changes of
  each session are kept.

**Verified.** `server/test/server.test.js`: tests for restart, catch-up,
written-before-answered, and import.

## CR-003: Session media and auto-load

| | |
|---|---|
| **Status** | Implemented |
| **Requested** | 2026-10-07 |
| **Implemented** | 2026-10-07, `feature/lan-server`, `e5b5680eb` |

**Description.** The video sits on network storage. The session remembers
which file its notes are about, and a DJV that joins with nothing open
loads it.

**Acceptance criteria.**
1. The session keeps the media's path relative to the media folder (or
   drive) and its absolute path.
2. Joining with nothing open loads the media. With a file open, the media
   is offered (**Load**) but not forced.
3. Connecting with a file open to a session with no media gives the
   session that file. **Share** replaces it.

**Implementation.**
- On the server: `PUT`/`DELETE /api/sessions/<s>/media` and the `media`
  event.
- In DJV: `SyncMedia`, `syncMediaFromPath()`, `resolveSyncMedia()`, and
  `App::_syncMediaUpdate()`.
- The Review tool has Share and Load buttons, and there are
  `Review/SyncShareMedia` and `Review/SyncLoadMedia` commands.

**Verified.** Server tests for the media requests and events.
`djv-test SyncModel`: path helpers, merge, and the live media round trip.

## CR-004: Client identity and server registry

| | |
|---|---|
| **Status** | Implemented; the registry is superseded by CR-014 |
| **Requested** | 2026-10-08 |
| **Implemented** | 2026-10-08, `feature/lan-server`, `b14b81bb4`, `5b7fda4d8`, `24756d3db` |

> Since CR-014, people sign in with a user name and a password, and the
> registry of client IDs is gone. The client ID stays as the PC's name, sent
> with every request and shown on the admin page (criteria 1, 2 and 5 still
> hold). Criteria 3 and 4 were replaced by signing in.

**Description.** Every PC has a unique, configurable client ID. The admin
registers the IDs on the server. Only registered PCs may use the sessions.
DJV keeps its configuration locally, in its settings file.

**Acceptance criteria.**
1. The client ID is set in DJV's **Sync Server** settings. It defaults to
   the computer name and is kept in DJV's `settings.json` (section
   `/Sync.1`).
2. Every request DJV makes carries its client ID and version
   (`X-DJV-Client`, `X-DJV-Version`).
3. With `requireRegistration`, a PC that is not registered is refused, and
   DJV says so in words the reviewer can act on. Once the admin approves it,
   DJV joins without any action on the PC.
4. A refused PC appears on the admin page as waiting for approval.
5. Two DJVs on one PC may share a client ID without their notes getting
   mixed up.
6. The server, the token and the media folder saved in the Review settings
   by earlier builds move across to the new section.

**Implementation.**
- `server/config.js` handles `config.json`: the registry and the
  validation. `server.js` adds identity, registration and hello.
- In DJV, `SyncSettings` and `readSyncSettings()` (the migration) are in
  `SettingsModel`, and `SyncIdentity` is in `SyncModel`.
- Echo detection still uses the per-connection ID, which is what makes
  criterion 5 hold.

**Verified.**
- `server/test/lan.test.js`: hello, registration, waiting clients, and
  `client` on events.
- `djv-test SyncModel`: the settings round trip, the migration, and
  parsing the hello reply.
- Live, against a server with registration on: the unregistered refusal.
- curl: refused, then approved, then let in.

## CR-005: Server admin page

| | |
|---|---|
| **Status** | Implemented; reworked by CR-014 |
| **Requested** | 2026-10-08 |
| **Implemented** | 2026-10-08, `feature/lan-server`, `b14b81bb4` |

> Since CR-014, the page signs in as `lldjv` instead of asking for an admin
> token, and manages users instead of client IDs. Criterion 2 is replaced by
> CR-014's criterion 5.

**Description.** The admin manages the server from a web page: the
registered clients, the PCs waiting for approval, who is connected, the
sessions, and the server settings (name, media share, expected DJV
version, registration).

**Acceptance criteria.**
1. `http://<server>:8787/admin` works with no internet access: it is a
   single self-contained page.
2. With an admin token set, the admin requests need it. Without one, they
   are answered only from the server itself.
3. Clients and settings changed on the page are saved to `config.json` and
   survive a restart. The admin token is never sent to the page.
4. A setting that cannot be used, such as a share that is not a UNC path,
   is refused with a message, and nothing changes.

**Implementation.** `server/admin.html`, and `/api/admin/*` in
`server/server.js`.

**Verified.** `server/test/lan.test.js`: admin auth, CRUD persisted
across a restart, validation, and the page served. The page's script was
syntax-checked; the page has not yet been tried in a browser on the studio
network.

## CR-006: Media share mapped to a drive

| | |
|---|---|
| **Status** | Implemented (LAN check pending) |
| **Requested** | 2026-10-08 |
| **Implemented** | 2026-10-08, `feature/lan-server`, `24756d3db` |

**Description.** The media is a read-only share on the dev server. The
server sends the share's UNC path when DJV contacts it. On Windows, DJV
maps the share to a drive letter chosen on each PC. It does this again at
every start, and keeps the letter and the share in its configuration.

**Acceptance criteria.**
1. The admin sets the share once, on the admin page or in `config.json`,
   and every PC learns it from `GET /api/hello`.
2. Each PC chooses its own drive letter. Only free letters are offered.
3. At start, DJV maps the drive in the background; the window does not
   wait.
4. With the server down, DJV maps the share it last heard of.
5. A letter mapped to another share, or used by a disk, is refused, not
   taken over. Credential problems are explained, pointing to the Windows
   Credential Manager (`cmdkey`). No password is stored.
6. The mapped drive is the folder session media is found in. Media shared
   from the drive carries its UNC path, so PCs with other letters find
   it.

**Implementation.**
- `lib/djv/Models/NetworkDrive.*` calls `WNetAddConnection2` without
  remembering the mapping at logon, and links `mpr`.
- `lib/djv/Models/LanModel.*` asks the server and maps the drive on
  threads of its own, and caches the share.
- The UI is the **Sync Server** settings panel, with its Check button.
- There are `Review/SyncTestServer` and `Review/SyncMapDrive` commands.

**Verified.**
- Automated: `djv-test SyncModel` covers the drive letter helpers, the
  refusal of a letter used by a disk, `toUncPath`, and the model with the
  server unreachable.
- **Pending:** mapping a real share on the studio network. To check
  criteria 2 to 6 by hand:
  1. Set up two PCs as in *Setting up a PC* (`docs/lan-setup.html`), with
     different drive letters.
  2. Restart DJV on both, and check that each maps its drive.
  3. Share a file from one, and check that the other loads it from its own
     letter.
  4. Stop the server and restart DJV, and check that the cached share is
     still mapped.

## CR-007: DJV version check

| | |
|---|---|
| **Status** | Implemented |
| **Requested** | 2026-10-08 |
| **Implemented** | 2026-10-08, `feature/lan-server`, `b14b81bb4`, `24756d3db` |

**Description.** Every PC on the studio network runs the same DJV. The
server says which version that is. A PC running another version is
warned, and flagged on the admin page.

**Acceptance criteria.**
1. `djvVersion` is set in the server configuration, or on the admin page.
   Empty means no check.
2. DJV compares it with its own version, and shows a mismatch in the Sync
   Server settings and the log. The notes still sync.
3. The admin page shows each client's version, and flags those that
   differ.

**Implementation.** `LanModel::_tick()` makes the comparison. On the
server, `X-DJV-Version` is recorded per client.

**Verified.** Server tests for the version recorded per client. The DJV
side was checked by reading the code; it has no automated test of its
own.

## CR-008: LAN documentation and logs

| | |
|---|---|
| **Status** | Implemented |
| **Requested** | 2026-10-08 |
| **Implemented** | 2026-10-08, `feature/lan-server`, `a8832511f` |

**Description.** Documentation for setting up and using DJV on the studio
network, a log of the change requests, and a changelog of what is
implemented.

**Acceptance criteria.**
1. The manual has a *Studio network* page covering the dev server setup,
   the admin page, setting up a PC, daily use, and troubleshooting. It is
   listed in the navigation.
2. This log, and `CHANGELOG.md` beside it.
3. `server/README.md` documents the configuration, the admin page, and
   the API.

**Implementation.**
- `docs/lan-setup.html`, `docs/nav.txt`, and the regenerated navigation.
- `docs/reviews.html` and `docs/settings.html`.
- `docs/lan/`, and `server/README.md`.

**Verified.** The `djvDocsNavCheck` target passes.

## CR-009: Session picker

| | |
|---|---|
| **Status** | Proposed |
| **Requested** | 2026-10-08 |

**Description.** Instead of typing a session name, the Review tool's Sync
section lists the server's sessions, with their media, to pick from.
`GET /api/sessions` already gives the list.

## CR-010: Encrypted connections

| | |
|---|---|
| **Status** | Proposed |
| **Requested** | 2026-10-08 |

**Description.** DJV talks to the sync server over plain HTTP. That is
acceptable on a closed studio network, but not beyond it. Options:
- build cpp-httplib with OpenSSL, so DJV can use HTTPS;
- put the server behind a TLS proxy.

## CR-011: Shared drawings

| | |
|---|---|
| **Status** | Proposed |
| **Requested** | 2026-10-08 |

**Description.** Share the annotations (drawings) through the session the
way the markers are shared. Today they stay in each reviewer's review.

## CR-012: Open from the media drive

| | |
|---|---|
| **Status** | Proposed |
| **Requested** | 2026-10-08 |

**Description.** The file browser opens on the mapped media drive when one
is mapped, so reviewers start where the media is.

## CR-013: Docker package for the sync server

| | |
|---|---|
| **Status** | Implemented |
| **Requested** | 2026-10-09 |
| **Implemented** | 2026-10-09, `feature/lan-server`, server commit |

**Description.** The sync server is deployed with Docker on the dev server,
a Windows machine with Docker Desktop at 192.168.1.40, on port 8877. It comes
as a package that can be tried on a local PC first and then copied to the dev
server. The SQLite file is kept in a folder of the host, not hidden inside
the container.

**Acceptance criteria.**
1. One script builds the image and a zip holding the image (`docker save`),
   the compose file and the scripts. The dev server needs neither the source
   nor the internet.
2. The server listens on 8877. Its data, `djv-sync.db` and `config.json`, is
   in a bind-mounted `data` folder on the host, and survives the container
   being removed and made again.
3. The passwords reach the container as Docker secrets, from local files.
   They are in neither the image nor the environment.
4. The database can be copied, consistently, while the server runs.
5. Start, stop, backup and update are each one command, documented step by
   step.

**Implementation.**
- `server/Dockerfile` (node:24-alpine, the `node` user, a health check) and
  `server/.dockerignore`.
- `server/deploy/`: `docker-compose.yml`, `.env.example`, `new-secrets.ps1`,
  `start.ps1`, `stop.ps1`, `backup.ps1`, `share-setup.ps1` and `README.md`.
- `server/build-package.ps1` writes `dist/djv-sync-server-<version>.zip`.
- `DB_EXCLUSIVE`: exclusive locking for a Windows bind mount, which cannot
  share the memory of the WAL index (`-shm`).
- `GET /api/admin/backup` copies the database with `VACUUM INTO`.
- Guide: `docs/lan/docker-deploy.md`.

**Verified.**
- On this PC (Docker Desktop 29.5), the image was built and run from a
  scratch copy of `deploy/`:
  - The health check reported healthy.
  - lldjv was made from the secrets file, a user was added, and that user
    signed in and wrote a marker.
  - `data/djv-sync.db` and `config.json` appeared on the host.
  - After `stop.ps1` and `start.ps1`, the user, the marker and the session
    token were still there.
  - `backup.ps1` wrote a copy that opens in SQLite.
- With `DB_EXCLUSIVE=0` the bind mount also ran. Exclusive locking stays the
  default, since a `-shm` file on a Windows bind mount is not dependable.
- `server/test/auth.test.js` checks the backup and the exclusive mode.
- **Pending:** installing on 192.168.1.40.

## CR-014: User accounts with passwords

| | |
|---|---|
| **Status** | Implemented |
| **Requested** | 2026-10-09 |
| **Implemented** | 2026-10-09, `feature/lan-server`, server and DJV commits |

**Description.** The server authenticates people with a user name and a
password, not only a client ID. The administrator is the root user `lldjv`,
with a strong password kept in a local file. The admin registers the users
from a simple admin page. In DJV, the password is kept in the Windows
Credential Manager, and DJV signs in by itself.

**Acceptance criteria.**
1. `POST /api/login` with a user name and a password gives a session token.
   Every other session request needs it.
2. Passwords are only kept hashed (scrypt). Session tokens are only kept as
   SHA-256 hashes, and survive a restart.
3. A wrong password is refused, without telling whether the user exists.
   Repeated failures from one address are slowed down.
4. Giving a user a new password, or removing them, ends their sign-ins at
   once.
5. Only `lldjv` uses the admin page and the admin API. Its password is set
   from `ROOT_PASSWORD_FILE` at every start, and cannot be changed or
   removed from the page.
6. In DJV's **Sync Server** settings, a reviewer types their user and
   password once, in a masked field. On success, the password is kept in the
   Credential Manager, never in `settings.json`. **Sign out** forgets it.
7. DJV signs in at start, before it connects. A session refused for want of
   a sign-in joins as soon as DJV has signed in. A token the server stops
   accepting leads to one fresh sign-in.
8. The client ID stays the PC's name, sent for the admin page only.

**Implementation.**
- Server:
  - `server/users.js`: hashing and tokens.
  - `server/config.js`: users instead of clients, and the share user.
  - `server/server.js`: login, logout, the `tokens` table, the throttle, the
    admin API, and the root bootstrap.
  - `server/admin.html`: rewritten.
- DJV:
  - `models::syncLogin()` and `syncLogout()`, and `SyncServerInfo` with the
    user and the share.
  - `lib/djv/Models/Credentials.*`: CredWrite and CredRead.
  - `LanModel`: `observeAuth()`, `signIn()`, `signOut()`; `start()` signs in
    with the kept password.
  - `SyncSettings::username`, with the token dropped. The default server is
    `http://192.168.1.40:8877`.
  - `ui::PasswordEdit`, a masked field with no copy and no undo, and the
    reworked `SyncSettingsWidget`.
  - `App` connects with the token, and connects again on sign-in or when the
    token is refused.
- Root password: `server/deploy/new-secrets.ps1` writes it into
  `secrets/lldjv_password.txt`, which is gitignored.

**Verified.**
- `server/test/auth.test.js` (14 tests) covers criteria 1 to 5, and an old
  configuration loading.
- `server/test/server.test.js` passes with every request signed in.
- `djv-test SyncModel`:
  - the settings with the user, and the token ignored;
  - parsing hello signed in and signed out;
  - `syncLogin` with nothing to sign in with, and with the server
    unreachable;
  - a Credential Manager round trip;
  - `LanModel` sign-in with the server unreachable.
- Live, against the Docker container, as a test user:
  - signing in with a wrong password was refused;
  - a session without a token was refused with the signed-out message;
  - with the token, two models synced.
- **Not yet tried:** the settings panel, and the masked field, by hand in a
  running DJV.

## CR-015: Share credentials from the server

| | |
|---|---|
| **Status** | Implemented (LAN check pending) |
| **Requested** | 2026-10-09 |
| **Implemented** | 2026-10-09, `feature/lan-server`, DJV commit |

**Description.** The media share is read with one Windows account, `djv`,
which has a standard password. DJV maps the drive with that account
automatically: the server hands the account to signed-in users, so nothing
is set up on each PC.

**Acceptance criteria.**
1. Hello gives the share's UNC, account and password only to a signed-in
   user. The password is read from `SHARE_PASSWORD_FILE` and never written
   to `config.json`. The admin page only says whether it is loaded.
2. DJV maps the drive with the account, as `<server>\djv` unless a domain is
   given.
3. After mapping, DJV keeps the account for the server in the Credential
   Manager, as `cmdkey` would. The drive is then mapped even when the sync
   server is down.
4. A refused account, and a conflicting connection to the same server, are
   explained in words the reviewer can act on.

**Implementation.**
- `mapNetworkDrive(letter, unc, user, password)`, `shareAccountName()` and
  `getUncHost()` in `NetworkDrive.*`.
- `saveShareCredential()` (CRED_TYPE_DOMAIN_PASSWORD) in `Credentials.*`.
- `LanModel::_mapDrive()`, which keeps the share password out of the
  `SyncServerInfo` that it shows.

**Verified.**
- `djv-test SyncModel`: `shareAccountName` and `getUncHost`, and hello
  parsed with the share.
- Server tests: the share credentials only when signed in, and never in the
  admin view.
- **Pending:** mapping a real share with the server's account. On this PC
  with `share-setup.ps1` and a test folder, then on the studio network.

## CR-016: Sharing the media folder

| | |
|---|---|
| **Status** | Implemented (share check pending) |
| **Requested** | 2026-10-09 |
| **Implemented** | 2026-10-09, `feature/lan-server`, server and docs commits |

**Description.** A document that describes how to share a folder with a new
`djv` user that has a standard password. The password is kept in a local
file.

**Acceptance criteria.**
1. One local file holds the standard password: `secrets/share_password.txt`,
   generated or given.
2. A script, run as an administrator:
   - makes the account, set so its password never expires;
   - shares the folder read-only to it;
   - sets the NTFS permissions;
   - opens the firewall.

   It can show what it would do, and undo it.
3. The same steps by hand, a check from a PC with `net use`, and how to
   change the password, are documented.

**Implementation.** `server/deploy/new-secrets.ps1`,
`server/deploy/share-setup.ps1`, and `docs/lan/share-setup.md`.

**Verified.**
- `new-secrets.ps1` ran on this PC. The secrets are gitignored.
- **Pending:** running `share-setup.ps1` itself. It changes Windows accounts
  and shares, so it was left for the admin to run, first with `-WhatIf`.

## CR-017: Notes signed by the signed-in user

| | |
|---|---|
| **Status** | Proposed |
| **Requested** | 2026-10-09 |

**Description.** A marker's author is the Windows account name of the PC
(`reviewAuthor()`). With people signing in, it could be the signed-in user's
name instead, so that notes read the same whichever PC they were written on.
