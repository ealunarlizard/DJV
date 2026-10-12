# DJV sync server

The sync server lets everyone who has a DJV review open see the same markers
as they are written. Everyone who joins the same *session* sees the same
markers. When one person adds, edits or deletes a marker, the others see the
change within a moment.

A session also remembers the media file its markers are about. Someone who
joins with nothing open gets that file loaded. Drawings are not shared.

People sign in with a user name and a password, which the administrator,
**lldjv**, gives them on the admin page the server serves at `/admin`. Once a
DJV has signed in, the server tells it where the media share is and which
Windows account to map it with, and DJV maps it to a drive. The *Studio
network* page of the DJV documentation (`docs/lan-setup.html`) explains how to
set one up.

The server's address opens the **review portal** (`portal.html`), where the
same people sign in to upload videos to the media share, make review
sessions of one or two of them for the people they choose, and open a
session in DJV with its *review* link. Its guide is `docs/lan/portal.md`.

The server is a few files: `server.js`, `config.js`, `users.js`, `media.js`,
`portal.html` and `admin.html`. It needs nothing beyond Node.js 22.13 or
newer, which has SQLite built in. For the studio it runs in Docker. The
deployment is in [`deploy/`](deploy/) and its guide is in
`docs/lan/docker-deploy.md`.

## Running

### In Docker (the studio's server)

```powershell
cd server
.\build-package.ps1          # the image, and dist\djv-sync-server-<version>.zip
```

The zip installs on any Windows PC with Docker Desktop. Unzip it, then run
`new-secrets.ps1`, `share-setup.ps1` and `start.ps1`. The details are in
`docs/lan/docker-deploy.md`. `deploy/` holds the same files, so you can run the
server from the source tree for a try:

```powershell
cd server\deploy
.\new-secrets.ps1; .\start.ps1     # http://localhost:8877/admin
```

### With Node

```
ROOT_PASSWORD=some-long-password node server/server.js
```

Or, from `server/`, run `npm start`, which also hides Node's warning that its
SQLite module is still experimental.

| Variable | Default | Meaning |
|---|---|---|
| `PORT` | `8877` | The port to listen on. |
| `HOST` | `127.0.0.1` | The address to listen on. Use `0.0.0.0` so other machines can reach it; the image does. |
| `DATA_DIR` | `server/data` | Where the database is kept, unless `DB_FILE` says otherwise. |
| `DB_FILE` | `DATA_DIR/djv-sync.db` | The SQLite database that holds every session and sign-in. |
| `CONFIG_FILE` | `server/config.json` | The configuration: the server name, the media share and its account, the expected DJV version, and the users. |
| `ROOT_PASSWORD_FILE` | (none) | A file holding the password of `lldjv`, the administrator. |
| `ROOT_PASSWORD` | (none) | The same password, given directly, when there is no file. |
| `SHARE_PASSWORD_FILE` | (none) | A file holding the password of the media share's account. |
| `SHARE_PASSWORD` | (none) | The same password, given directly, when there is no file. |
| `DB_EXCLUSIVE` | off | `1` to lock the database exclusively. Needed when `DATA_DIR` is a Windows folder mounted into a container (see *Storage*). |
| `MEDIA_DIR` | (none) | The folder the media share shares. The portal lists the videos in it and uploads to it. Without it, the portal has no videos. The image sets `/media`. |

At every start, the server makes sure `lldjv` exists and has the root
password. A server with no `lldjv` and no root password does not start. A file
read for a password may end with a line break or start with a byte order mark;
neither is taken as part of the password.

## Configuration

`config.json` (copy `config.example.json` to start) holds what the server
tells DJV and who may sign in:

```json
{
  "name": "Dev review server",
  "storage": { "unc": "\\\\192.168.1.40\\media", "user": "djv" },
  "djvVersion": "3.7.5",
  "users": [
    { "username": "anna", "name": "Anna", "passwordHash": "scrypt$16384$8$1$…", "created": "…", "updated": "…" }
  ]
}
```

- `storage.unc` is the read-only share with the review media. DJV maps it to
  the drive letter set on each PC. The server never opens it.
- `storage.user` is the Windows account DJV maps the share with. With no
  domain, DJV signs in as `<share's server>\<user>`. The account's password
  comes from `SHARE_PASSWORD_FILE`, never from this file.
- `djvVersion` is the DJV version every PC should run. A PC running another
  is warned, and flagged on the admin page. Leave it empty to not check.
- `users` lists the people who may sign in. The admin page adds them, and
  their passwords are kept hashed with scrypt. `lldjv` is the only
  administrator.

The server reads the file when it starts, and the admin page writes it. A
file that cannot be read stops the server from starting. Fields starting with
`_` are ignored, so they can hold comments. Fields left over from the first
LAN server (`clients`, `requireRegistration`, `adminToken`) are ignored too,
and dropped the next time the file is written.

## Portal

Open `http://<server>:8877` and sign in as any user. See
`docs/lan/portal.md`. The portal keeps its session token for the browser tab
only.

## Admin page

Open `http://<server>:8877/admin` and sign in as `lldjv`. The page lets you:

- add users, rename them, give them a new password, or remove them;
- see when each user last signed in, from which PC, and with which DJV
  version;
- see who is connected to which session now;
- see every session, with its date, videos and people;
- change the server settings;
- download a backup of the database.

The page keeps its session token for the browser tab only.

DJV and the admin page connect over plain HTTP. The passwords cross the
network at sign-in, and a random token is sent after that. Across an
untrusted network, put the server behind a TLS reverse proxy, or keep it on a
VPN.

## Tests

```
cd server
npm test
```

DJV's own tests include a live test against a running server, signed in as a
user the admin added:

```
DJV_SYNC_TEST_SERVER=http://127.0.0.1:8877 DJV_SYNC_TEST_USER=test DJV_SYNC_TEST_PASSWORD=... djv-test SyncModel
```

## How it works

A session is a set of markers, written as they are in a review file
(`docs/review-format.html`), together with the media they are about.

The server numbers every change it receives; that number is the *revision*.
It passes each change to everyone following the session, in that order. The
change to a marker that reaches the server last is the one that stands, so
every client ends up with the same markers whatever their clocks say. Deleted
markers are remembered, so a client that was away learns of the deletion
instead of sending the marker back.

A client first reads the session's state, then follows its event stream from
the revision that state was at, so nothing falls between the two. The changes
a client makes are sent one at a time, in order, and retried until the server
takes them. A client that was away for more than the last 1000 changes is
told to read the state again. The changes are kept in the database, so a
restart of the server does not count against that.

### Media

The media is the file the markers are about, kept on network storage that
every reviewer can reach. The server never opens it: it only passes on where
it is. Each machine can reach the storage under a different name
(`\\nas\media` on Windows, `/Volumes/media` on macOS, `/mnt/media` on Linux),
so the path is given twice:

- `path` is relative to the client's media folder or mapped drive, with
  forward slashes. Each client joins it to its own folder. It is empty when
  the file is not inside the folder of the client that set it.
- `pathAbsolute` is the path as the client that set it saw it, in UNC form
  for a mapped drive. A client tries it when the relative path does not lead
  to the file.

`name` is the file name, for display, and `sharedBy` is who set it. A client
that joins a session with no media while it has a file open sets it.

### API

Everything is under `/api`.

`POST /login` takes `{ "username", "password" }` and returns
`{ "token", "expires", "user": { "username", "name", "admin" } }`. Every
request after that sends `Authorization: Bearer <token>`.

- A token lasts 30 days from its last use.
- `POST /logout` ends it.
- So does giving its user a new password, or removing the user.
- A request without a valid token gets 401 with `"code": "signed-out"`.
- A wrong name or password gets 401 with `"code": "login-failed"`.
- After 10 failures from one address in 5 minutes, sign-ins from it get 429
  for the rest of that 5-minute window.

`health`, `login` and `hello` need no token.

### Who sees a session

A session is for the people it was made for (`members`), whoever made it
(`createdBy`), and `lldjv`. For anyone else, every request about it gets 403
with `"code": "not-member"`, and `GET /sessions` leaves it out.

- A name that nobody has used yet becomes a session the first time someone
  opens it, as DJV does when someone types a new name. That user is its
  creator and only member.
- A session made before server 2.1 has no members (`members: null`), and is
  open to every user.
- Only the creator and `lldjv` change or delete a session (403
  `not-owner`).

Every request also says which DJV it comes from, for the admin page:

```
X-DJV-Client: edit-bay-1
X-DJV-Version: 3.7.5
```

| Request | Body | Answer |
|---|---|---|
| `GET /health` | | `{ "ok": true }` |
| `POST /login` | `{ "username", "password" }` | see above |
| `POST /logout` | | `{ "ok": true }` |
| `GET /hello` | | `{ "name", "serverVersion", "djvVersion", "authRequired": true, "signedIn" }`, plus, once signed in, `"user": {...}` and `"storage": { "unc", "user", "password" }` |
| `GET /sessions` | | `{ "sessions": [...] }`, the sessions the user may see, newest first (see below) |
| `POST /sessions` | `{ "description"?, "date"?, "members"?, "videos": [a, b?] }` | The new session, as in the list. The server names it `YYYYMMDD-<description>-<4 random>`. |
| `PUT /sessions/<session>/info` | any of `description`, `date`, `members`, `videos` | The session, as in the list. New videos are a media event. |
| `DELETE /sessions/<session>` | | `{ "ok": true }`. The streams of the session get a `removed` event. |
| `GET /sessions/<session>` | | `{ "session", "rev", "media": {...} \| null, "markers": [...], "deleted": [ids] }` |
| `PUT /sessions/<session>/markers/<id>` | `{ "clientId", "marker" }` | `{ "rev" }` |
| `DELETE /sessions/<session>/markers/<id>?clientId=<id>` | | `{ "rev" }` |
| `PUT /sessions/<session>/media` | `{ "clientId", "media": { "path", "pathAbsolute", "name", "sharedBy" } }` | `{ "rev" }` |
| `DELETE /sessions/<session>/media?clientId=<id>` | | `{ "rev" }` |
| `GET /sessions/<session>/events?since=<rev>` | | Server-sent events |
| `GET /people` | | `{ "people": [{ "username", "name" }] }`, to invite |
| `GET /videos` | | `{ "enabled", "videos": [{ "path", "name", "folder", "size", "modified" }], "folders", "free", "uploading" }` |
| `DELETE /videos?path=<path>[&force=1]` | | `{ "ok": true }`. Only `lldjv`. 409 with `"sessions"` while sessions use the file, unless `force=1`. |
| `POST /uploads` | `{ "path", "size" }` | `{ "id", "path", "size", "received", "chunkSize" }`. The same user starting the same file again gets the unfinished upload back. 409 for a name that is taken, 507 for too little space. |
| `PUT /uploads/<id>?offset=<received>` | the next piece, as raw bytes, at most `chunkSize` | `{ "received", "done" }`, and `"video"` when it is whole. 409 with `"received"` for a piece out of order. |
| `GET /uploads` | | `{ "uploads": [...] }`, the user's unfinished ones |
| `DELETE /uploads/<id>` | | `{ "ok": true }` |

An item of `GET /sessions`:

```json
{
  "session": "20261012-sh010-lighting-k7qm", "rev": 12, "markers": 4, "clients": 1,
  "media": "v001.mov",
  "description": "SH010 lighting", "date": "2026-10-12", "createdBy": "anna",
  "members": ["anna", "ben"], "open": false, "canEdit": true,
  "videos": [{ "path", "pathAbsolute", "name" }, { ... }],
  "connected": ["ben"], "created": "...", "updated": "..."
}
```

Session names and marker identifiers are letters, digits, `.`, `_` and `-`,
up to 128 of them. User names follow the same rule, up to 64 of them. A
session that has never been written to is empty. The media's fields are text
of up to 4096 characters, and either `path` or `pathAbsolute` must be given.

The media can have a second file, `"compare": { "path", "pathAbsolute",
"name" }`, which DJV opens as B beside the first. A session the portal makes
of two videos has it.

A path in the media folder is relative, with forward slashes, and must be a
good Windows path:

- no `< > : " | ? * \`;
- no name starting with a dot, or ending with a dot or a space;
- no device names such as `CON`;
- at most 200 characters.

Only video files are uploaded and listed: `.mov .mp4 .m4v .mxf .mkv .avi
.webm .mpg .mpeg .ts .y4m .wav`.

The data of each event is JSON:

```
{ "type": "upsert", "rev": 12, "clientId": "...", "user": "anna", "marker": { ... } }
{ "type": "delete", "rev": 13, "clientId": "...", "user": "anna", "id": "..." }
{ "type": "media",  "rev": 14, "clientId": "...", "user": "anna", "media": { ... } | null }
{ "type": "reset",  "rev": 14 }
{ "type": "removed", "rev": 14 }
```

`clientId` identifies the connection that made the change, so a client can
tell its own changes coming back from other people's. It is new each time
DJV connects, because two DJVs can share a user.

`user` is who made the change.

`reset` means the changes after `since` are no longer kept: read the session
again, and follow it from there.

`removed` means the session was deleted, or the user is no longer one of its
people. The stream ends, and DJV stops sending to the session, since a write
to the name would start a new session.

A comment is written every 15 seconds to keep the stream open.

### Admin API

These requests live under `/api/admin`, for the admin page. They need the
token of an administrator: without a token they get 401, and with a user who
is not `lldjv` they get 403 with `"code": "not-admin"`.

| Request | Body | Answer |
|---|---|---|
| `GET /config` | | `{ "name", "storage": { "unc", "user" }, "djvVersion", "serverVersion", "sharePasswordSet", "file" }` |
| `PUT /config` | any of `name`, `storage`, `djvVersion` | as `GET`, or 400 for a value that cannot be used |
| `GET /users` | | `{ "users": [{ "username", "name", "admin", "created", "updated", "lastSignIn", "lastSeen", "pc", "ip", "version", "connected" }] }` |
| `PUT /users/<name>` | `{ "name"?, "password"? }` | the user, added or changed. A new user needs a password of at least 8 characters, and `lldjv`'s password is only set from its file. |
| `DELETE /users/<name>` | | `{ "ok": true }`, 404 for no such user, 400 for `lldjv` |
| `GET /connected` | | `{ "connected": [{ "user", "pc", "session", "ip", "version", "since" }] }` |
| `GET /sessions` | | as `GET /api/sessions` |
| `GET /backup` | | a copy of the database, `djv-sync-<date>.db` |

### Storage

Every session is in one SQLite database, `DATA_DIR/djv-sync.db`. For each
session it holds:

- the revision and the media;
- the markers;
- the deleted identifiers;
- the last 1000 changes;
- who it is for (`session_info`: the description, date, creator and
  members).

It also holds the unfinished uploads (`uploads`), so that one carries on
after a restart. Their data waits in `MEDIA_DIR/.djv-uploads`, and is moved
into place once whole. Uploads nobody carried on with for a week are dropped
at start.

The database's layout is version 3 (`PRAGMA user_version`). Version 3 only
adds tables, so a database of an older server is brought up to date when the
server starts.

The same database holds the session tokens, stored only as SHA-256 hashes. A
change is written before the server answers the client that sent it, so a
change DJV was told was taken is not lost if the server stops.

The database is in write-ahead mode. While the server runs, the files
`djv-sync.db-wal` and `djv-sync.db-shm` sit beside it, and they belong to it.

A Windows folder mounted into a container cannot share the memory that the
`-shm` file is for. With `DB_EXCLUSIVE=1`, which the compose file sets, the
server keeps that index in its own memory instead. No other program can then
open the database while the server runs.

To back it up while the server runs, use `GET /api/admin/backup`. The admin
page's **Download a backup** and `deploy/backup.ps1` use it too. It writes a
consistent copy with `VACUUM INTO`. Alternatively, stop the server and copy
the files.

Earlier versions of the server kept each session in a JSON file,
`DATA_DIR/<session>.json`. On start, any such file whose session is not yet
in the database is imported and renamed to `<session>.json.imported`.
