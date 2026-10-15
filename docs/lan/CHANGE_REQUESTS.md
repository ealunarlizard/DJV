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
| [CR-013](#cr-013-docker-package-for-the-sync-server) | Docker package for the sync server | Implemented | `082518945` |
| [CR-014](#cr-014-user-accounts-with-passwords) | User accounts with passwords | Implemented | `082518945`, `5f5a3b75b` |
| [CR-015](#cr-015-share-credentials-from-the-server) | Share credentials from the server | Implemented (LAN check pending) | `5f5a3b75b` |
| [CR-016](#cr-016-sharing-the-media-folder) | Sharing the media folder | Implemented (share check pending) | `082518945`, `bc741ef6a` |
| [CR-017](#cr-017-notes-signed-by-the-signed-in-user) | Notes signed by the signed-in user | Proposed | |
| [CR-018](#cr-018-uploading-videos-to-the-media-share) | Uploading videos to the media share | Implemented | `9dab3ba4a` |
| [CR-019](#cr-019-sessions-for-the-people-they-are-made-for) | Sessions for the people they are made for | Implemented | `9dab3ba4a`, `1335c3a2a` |
| [CR-020](#cr-020-review-portal) | Review portal | Implemented (browser check pending) | `9dab3ba4a` |
| [CR-021](#cr-021-sessions-of-two-videos-compared) | Sessions of two videos, compared | Implemented | `9dab3ba4a`, `1335c3a2a` |
| [CR-022](#cr-022-review-links-that-open-djv) | Review links that open DJV | Implemented | `1335c3a2a`, `486f0dd19` |
| [CR-023](#cr-023-review-links-under-another-address-autoconnect-and-their-log) | Review links under another address, autoConnect, and their log | Implemented | `b46bac5ab`, `486f0dd19` |
| [CR-024](#cr-024-the-studios-own-values-in-env) | The studio's own values in .env | Implemented | (this branch) |
| [CR-025](#cr-025-the-server-image-from-a-private-registry) | The server image from a private registry | Implemented (dev server check pending) | (this branch) |

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

> Since CR-014, the page signs in as `admin` instead of asking for an admin
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

The portal (CR-020) lists the sessions in a browser, and its review links
(CR-022) open one in DJV; a list inside DJV is still open.

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
| **Implemented** | 2026-10-09, `feature/lan-server`, `082518945` |

**Description.** The sync server is deployed with Docker on the dev server,
a Windows machine with Docker Desktop at 192.168.0.100, on port 8877. It comes
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
  - admin was made from the secrets file, a user was added, and that user
    signed in and wrote a marker.
  - `data/djv-sync.db` and `config.json` appeared on the host.
  - After `stop.ps1` and `start.ps1`, the user, the marker and the session
    token were still there.
  - `backup.ps1` wrote a copy that opens in SQLite.
- With `DB_EXCLUSIVE=0` the bind mount also ran. Exclusive locking stays the
  default, since a `-shm` file on a Windows bind mount is not dependable.
- `server/test/auth.test.js` checks the backup and the exclusive mode.
- **Pending:** installing on 192.168.0.100.

## CR-014: User accounts with passwords

| | |
|---|---|
| **Status** | Implemented |
| **Requested** | 2026-10-09 |
| **Implemented** | 2026-10-09, `feature/lan-server`, `082518945`, `5f5a3b75b` |

**Description.** The server authenticates people with a user name and a
password, not only a client ID. The administrator is the root user `admin`,
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
5. Only `admin` uses the admin page and the admin API. Its password is set
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
    `http://192.168.0.100:8877`.
  - `ui::PasswordEdit`, a masked field with no copy and no undo, and the
    reworked `SyncSettingsWidget`.
  - `App` connects with the token, and connects again on sign-in or when the
    token is refused.
- Root password: `server/deploy/new-secrets.ps1` writes it into
  `secrets/admin_password.txt`, which is gitignored.

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
| **Implemented** | 2026-10-09, `feature/lan-server`, `5f5a3b75b` |

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
| **Implemented** | 2026-10-09, `feature/lan-server`, `082518945`, `bc741ef6a` |

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

## CR-018: Uploading videos to the media share

| | |
|---|---|
| **Status** | Implemented |
| **Requested** | 2026-10-12 |
| **Implemented** | 2026-10-12, `feature/lan-server`, `9dab3ba4a` |

**Description.** Reviewers put a video on the media share from their own
PC, through the portal, rather than copying it onto the dev server by hand.
The server must be able to write to the shared folder, which the Docker
configuration has to allow.

**Acceptance criteria.**
1. Any signed-in user can upload a video from the browser, into the media
   folder or a folder inside it.
2. Files of many gigabytes upload in resumable pieces. A dropped
   connection, a reload of the page, or a restart of the server costs one
   piece, not the whole file.
3. A file is listed only when it is whole. It never replaces a file of the
   same name, whatever the case of the letters.
4. The list of videos is the folder's content, as DJV sees it.
5. Only admin can delete videos, and is warned about sessions that use one.
6. The share itself stays read-only for the account DJV maps it with.

**Implementation.**
- `server/media.js` checks that a path can live on a Windows share, lists
  the videos, and reports the free space.
- Uploads use `POST /api/uploads`, `PUT /api/uploads/<id>?offset=`,
  `DELETE` and `GET`:
  - each 32 MB piece is written in place into
    `MEDIA_DIR/.djv-uploads/<id>.part`;
  - the `uploads` table tracks how much has arrived;
  - the finished file is linked into place.
- Videos use `GET /api/videos`, plus `DELETE /api/videos?path=` for admin.
- Docker: `MEDIA_DIR=/media` is bound read-write to `DJV_MEDIA_DIR`.
  `share-setup.ps1` sets that in `.env` to the folder it shares.

**Verified.**
- Criteria 1 to 5: `server/test/portal.test.js` covers the order of pieces,
  resuming across a restart, a name in another case, cancelling, and the
  right to delete.
- Criterion 1 in Docker: a script uploaded a 70 MB file, in three pieces, to
  the container on this PC, and it landed in the Windows folder.
- Not yet checked:
  - uploading a real video from a browser;
  - the dev server.

## CR-019: Sessions for the people they are made for

| | |
|---|---|
| **Status** | Implemented |
| **Requested** | 2026-10-12 |
| **Implemented** | 2026-10-12, `feature/lan-server`, `9dab3ba4a`, `1335c3a2a` |

**Description.** A session has a date, a description and the people it is
for. Only those people, the creator and admin can see it. Anyone can make
one, and session names never clash.

**Acceptance criteria.**
1. The server names each session made on the portal, and the names never
   clash.
2. Users see and join only the sessions they belong to. Anyone else is
   refused with 403 `not-member`, in the portal and in DJV.
3. A name typed in DJV that nobody has used yet becomes that user's
   session. Only they can open it until they invite others.
4. Only the creator and admin can change or delete a session. Deleting it
   tells the DJVs in it, which leave it rather than create it again.
5. Sessions from a server before 2.1 stay open to everyone.

**Implementation.**
- The server:
  - the `session_info` table (schema 3);
  - `POST /api/sessions`, `PUT /api/sessions/<s>/info` and
    `DELETE /api/sessions/<s>`;
  - the `removed` event;
  - `GET /api/people`.
- DJV: a `removed` event ends the connection with a message
  (`SyncModel.cpp`).

**Verified.**
- `server/test/portal.test.js` covers criteria 1 to 5, including the
  database migration.
- The container on this PC kept the 2.0 sessions, open to everyone.

## CR-020: Review portal

| | |
|---|---|
| **Status** | Implemented (browser check pending) |
| **Requested** | 2026-10-12 |
| **Implemented** | 2026-10-12, `feature/lan-server`, `9dab3ba4a` |

**Description.** A web page on the sync server, used with the same accounts
as DJV. It is for uploading videos, seeing the sessions with their date,
description and people, and making new sessions of one or two videos.

**Acceptance criteria.**
1. The server's address opens the portal, and anyone with a DJV account can
   sign in.
2. It has three tabs, Sessions, New session and Videos, as CR-018, CR-019
   and CR-021 describe.
3. Each session has a **review** link (CR-022).

**Implementation.**
- `server/portal.html` is one self-contained page, served at `/` and
  `/portal`.
- The admin page's session list shows the same fields.
- The guide is `docs/lan/portal.md`.

**Verified.**
- A test checks that the page is served.
- The tests cover the requests the page makes.
- Not yet checked: clicking through it in a browser.

## CR-021: Sessions of two videos, compared

| | |
|---|---|
| **Status** | Implemented |
| **Requested** | 2026-10-12 |
| **Implemented** | 2026-10-12, `feature/lan-server`, `9dab3ba4a`, `1335c3a2a` |

**Description.** DJV opens a session made of two videos as A and B,
compared.

**Acceptance criteria.**
1. The session's media includes the second video as `compare`.
2. DJV opens B and A, and shows them side by side unless the person has
   chosen another way to compare.
3. A DJV that shares its media also shares the B file it is comparing.
4. If B is missing, DJV reports it and opens A alone.

**Implementation.**
- `SyncMediaFile` and `SyncMedia::compare` (`SyncModel.h`).
- The server's `checkMedia`.
- `App::loadSyncMedia` and `App::_syncLocalMediaUpdate`.

**Verified.**
- `SyncModelTest` checks the JSON round trip.
- The live test shares a compare file between two DJVs through the
  container.
- No test covers opening the two in the player.

## CR-022: Review links that open DJV

| | |
|---|---|
| **Status** | Implemented |
| **Requested** | 2026-10-12 |
| **Implemented** | 2026-10-12, `feature/lan-server`, `1335c3a2a`; fixed by CR-023 |

**Description.** Clicking the portal's **review** link opens DJV on that
PC, signed in, in that session, with its videos loaded. If DJV is already
running, the running copy takes the link instead of a new one starting.

**Acceptance criteria.**
1. `djv://session/<session>?server=<server>` starts DJV. DJV is registered
   for djv: links in two ways:
   - the installer registers it for the whole machine;
   - DJV registers itself for the user when no handler is registered.
2. A running DJV takes the link from the new one, which exits. Only the same
   Windows user can hand it a link.
3. DJV refuses a link for any server other than the one in its settings.
   This stops a web page from sending DJV, and its password, somewhere
   else. (CR-023 tells the same server under another address from another
   server.)
4. If DJV is not signed in, it shows the Sync Server settings, and opens the
   session once the user has signed in.
5. DJV asks about unsaved review changes before it closes the review.

**Implementation.**
- `lib/djv/Models/DeepLink.h/.cpp`: parsing, the pipe, and the registry.
- `bin/djv/main.cpp`.
- `App::openDeepLink`, and the command `Review/SyncOpenLink`.
- The NSIS keys in `cmake/Modules/Package.cmake`.
- **Review links** in the Sync Server settings.

**Verified.**
- `SyncModelTest` checks the parsing, the server matching, and the
  hand-over of two links through a test pipe of its own.
- 2026-10-13: a link clicked in the browser started DJV, which then refused
  it (see CR-023). With that fixed, the same link, opened through the
  Windows handler as the browser opens it, joined the session and opened
  its video with DJV closed, and was taken by the running DJV with DJV
  open.

## CR-023: Review links under another address, autoConnect, and their log

| | |
|---|---|
| **Status** | Implemented |
| **Requested** | 2026-10-13 |
| **Implemented** | 2026-10-13, `feature/lan-server`, `b46bac5ab` (server 2.1.1), `486f0dd19` (DJV) |

**Description.** The portal's link
`djv://session/review-d87fe0c3?server=http%3A%2F%2Flocalhost%3A8877`
started DJV, but no session was joined. DJV's log said why, in a message
that could not be read:

```
ERROR: The review link is for the sync server {0}, but this DJV uses {1}. ...
```

- DJV was set to `http://127.0.0.1:8877`, and the portal was opened at
  `http://localhost:8877`. The two were compared as text, so DJV refused
  the link. A portal opened by host name, with DJV set to the IP address,
  would be refused the same way.
- `ftk::Format` cannot fill a placeholder used twice, and left the message
  as it was.

Also asked for:
- an `autoConnect=true|false` parameter on the link;
- a log of every step a link takes in DJV, so that a failure can be seen
  there.

**Acceptance criteria.**
1. A link that names another address of DJV's own server is followed. The
   session is joined on the server of the settings, never on the link's.
2. A link for another server, one that cannot be reached, or a server too
   old to say, is refused with a message that says which.
3. `autoConnect=true`, or no `autoConnect` at all, joins the session.
   `autoConnect=false` fills the session in, in the Review tool's Sync
   section, for the person to connect. Any other value is refused. The
   portal's links say `true`.
4. Every step is logged:
   - where the link came from;
   - what it asks for;
   - the sign-in;
   - the server check;
   - closing the review, or the person keeping it;
   - joining.

   Every change of the sync connection is logged too, and so is opening the
   session's videos. Problems are errors, which show in the Messages tool
   and the status bar.

**Implementation.**
- **Server 2.1.1:** `hello` gives `serverId`, made once and kept in the
  database's new `meta` table.
- **`DeepLink::autoConnect`** and `SyncServerInfo::serverId`.
- **`App::openDeepLink`:**
  - it signs in first;
  - it accepts the same address at once;
  - for another address, it asks that address's `hello` on a thread, with
    no token and no password, and compares the `serverId`s.
- **`App::observeSyncProposal`**, which the Review tool fills its Sync
  fields from.
- **`confirmClose`** takes an `onCancel`.
- **The log:**
  - `djv::app::ReviewLink`: the link's steps, and at start which DJV the
    links open and whether this one takes them;
  - `djv::app::Sync`: the connection and the session's videos.
- **The portal** adds `&autoConnect=true` to its links.

**Verified.**
- The server tests (40) and `djv-test` pass, including the live part.
  - The live part checks that `localhost` and `127.0.0.1` give the same
    `serverId`.
  - The link's `autoConnect` values are tested.
- With the test container on 2.1.1 and DJV set to `http://127.0.0.1:8877`,
  links were opened through the Windows handler. Each was followed as
  logged:
  - **The user's link, DJV closed:** DJV started, signed in, found
    `localhost:8877` to be the same server, joined `review-d87fe0c3`, and
    opened its video from `W:`.
  - **The same link, DJV running:** it was taken from the new DJV
    ("Already in the session").
  - **`autoConnect=false`:** the session was filled in, and nothing was
    joined.
  - **`autoConnect=maybe`:** refused, with the reason.
  - **`localhost:9999`:** refused, as it cannot be reached.
  - **A session `test` is not one of the people of:** the server's
    `not-member` refusal is logged as an error.

## CR-024: The studio's own values in .env

| | |
|---|---|
| **Status** | Implemented |
| **Requested** | 2026-10-14 |
| **Implemented** | 2026-10-14, `feature/lan-server` (server 2.2.0) |

**Description.** Before the branch is pushed to a public repository, the
studio's server address and the administrator's name come out of the code
and the docs. They go into a `.env` file that git ignores, with a committed
`.env.example` that holds example values: `192.168.0.100` and `admin`.

**Acceptance criteria.**
1. No tracked file holds the studio's address or its administrator's name.
2. DJV's default sync server comes from `DJV_SYNC_SERVER` in `.env`, or
   from `.env.example` when there is no `.env`.
3. The server's administrator is `ADMIN_USER`, `admin` when unset. Its
   password file is named for the role, not the user.
4. A deployment made before keeps its administrator, its password and its
   users when it is updated.

**Implementation.**
- **`.env.example`** at the top of the tree, and `/.env` in `.gitignore`.
- **`cmake/Modules/DotEnv.cmake`** reads a key from `.env` or
  `.env.example`. `lib/djv/Models/CMakeLists.txt` passes `DJV_SYNC_SERVER`
  to `models::defaultSyncServer()`, which `SyncSettings::server` starts
  with. A `-DDJV_SYNC_SERVER=` wins over both files.
- **The server** reads `ADMIN_USER` once, at start (`ROOT_USER` in
  `users.js`), and refuses a name that is not a user name. `npm start`
  loads the top `.env`. The admin page names no user; it shows whoever
  signed in.
- **The deployment:** `ADMIN_USER` in `server/deploy/.env.example` and the
  compose file, and the secret `admin_password`. `start.ps1` renames an
  older deployment's password file and sets `ADMIN_USER` to its old name.
  `backup.ps1` signs in as `ADMIN_USER`.
- **Docs, comments and tests** use the example values. The server tests
  name `ROOT_USER`, and pass under any `ADMIN_USER`.

**Verification.** The server tests pass, 41 of 41, both with no
`ADMIN_USER` and with another name. DJV builds with the address from
`.env`, and `djv-test` passes.

## CR-025: The server image from a private registry

| | |
|---|---|
| **Status** | Implemented (dev server check pending) |
| **Requested** | 2026-10-14 |
| **Implemented** | 2026-10-14, `feature/lan-server` (server 2.2.0) |

**Description.** The server's image goes to the studio's private
registry (GitHub's), and the dev server pulls it from there. An update is
then a new version in `.env` rather than a new package copied over.

**Acceptance criteria.**
1. `build-package.ps1 -Push <repository>` pushes `<version>` and `latest`,
   and the package then pulls from that repository.
2. With `DJV_SYNC_IMAGE` set in the deployment's `.env`, `start.ps1` pulls
   `DJV_SYNC_IMAGE:DJV_SYNC_VERSION` and runs it.
3. Without it, a deployment loads the package's `.tar`, as before.
4. A server whose registry cannot be reached still starts, from the image
   Docker has or from the `.tar`.
5. No registry credentials are in the repository, the package or `.env`.

**Implementation.**
- **`docker-compose.yml`** runs
  `${DJV_SYNC_IMAGE:-djv-sync-server}:${DJV_SYNC_VERSION:-latest}`;
  `DJV_SYNC_IMAGE` is in `server/deploy/.env.example`, empty.
- **`start.ps1`** pulls when `DJV_SYNC_IMAGE` is set, and on a failure
  falls back to the local image, or loads the `.tar` and tags it with the
  registry's name.
- **`build-package.ps1 -Push`** tags and pushes, and writes the repository
  into the package's `.env.example`.
- **The sign-in** is `docker login ghcr.io` on each machine, kept by Docker
  in the Windows Credential Manager: a classic token with `write:packages`
  where the image is built, `read:packages` on the dev server.
- **Docs:** `docs/lan/docker-deploy.md`, `server/deploy/README.md`.

**Verification.** Both scripts parse. `docker compose config` gives the
registry's image with `DJV_SYNC_IMAGE` set, and the local one without it.
`ghcr.io/<organization>/djv-sync-server:2.2.0`, pushed by hand, pulls on the
build machine. Still to check on the dev server: criteria 2 and 4.
