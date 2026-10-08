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
| [CR-004](#cr-004-client-identity-and-server-registry) | Client identity and server registry | Implemented | `b14b81bb4`, `5b7fda4d8`, `24756d3db` |
| [CR-005](#cr-005-server-admin-page) | Server admin page | Implemented | `b14b81bb4` |
| [CR-006](#cr-006-media-share-mapped-to-a-drive) | Media share mapped to a drive | Implemented (LAN check pending) | `24756d3db` |
| [CR-007](#cr-007-djv-version-check) | DJV version check | Implemented | `b14b81bb4`, `24756d3db` |
| [CR-008](#cr-008-lan-documentation-and-logs) | LAN documentation and logs | Implemented | `a8832511f` |
| [CR-009](#cr-009-session-picker) | Session picker | Proposed | |
| [CR-010](#cr-010-encrypted-connections) | Encrypted connections | Proposed | |
| [CR-011](#cr-011-shared-drawings) | Shared drawings | Proposed | |
| [CR-012](#cr-012-open-from-the-media-drive) | Open from the media drive | Proposed | |

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
| **Status** | Implemented |
| **Requested** | 2026-10-08 |
| **Implemented** | 2026-10-08, `feature/lan-server`, `b14b81bb4`, `5b7fda4d8`, `24756d3db` |

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
| **Status** | Implemented |
| **Requested** | 2026-10-08 |
| **Implemented** | 2026-10-08, `feature/lan-server`, `b14b81bb4` |

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
