# DJV sync server

Shares the markers of a DJV review between the people who have it open, as
they are written. Everyone who joins the same *session* sees the same
markers; an addition, an edit or a deletion made by one of them reaches the
others within a moment. A session also remembers the media file its markers
are about, so someone who joins with nothing open gets that file loaded.
Drawings are not shared.

On a studio network it is also where DJV learns its place: each DJV says
which PC it is, the server lets in the PCs the admin registered, and tells
them where the media share is, which DJV maps to a drive. The admin manages
this from a web page the server serves at `/admin`. See the *Studio network*
page of the DJV documentation (`docs/lan-setup.html`) for setting one up.

It is a few files (`server.js`, `config.js`, `admin.html`) with no
dependencies beyond Node.js 22.13 or newer, which has SQLite built in.

## Running

```
node server/server.js
```

or, from `server/`, `npm start`, which also hides Node's warning that its
SQLite module is still experimental.

| Variable         | Default                   | Meaning                                                           |
|------------------|---------------------------|-------------------------------------------------------------------|
| `PORT`           | `8787`                    | The port to listen on.                                            |
| `HOST`           | `127.0.0.1`               | The address to listen on. `0.0.0.0` for other machines to reach it. |
| `DATA_DIR`       | `server/data`             | Where the database is kept, unless `DB_FILE` says otherwise.      |
| `DB_FILE`        | `DATA_DIR/djv-sync.db`    | The SQLite database that holds every session.                     |
| `DJV_SYNC_TOKEN` | (none)                    | An access token every client must give. Set it whenever `HOST` is not this machine alone and registration is off. |
| `CONFIG_FILE`    | `server/config.json`      | The configuration: server name, media share, expected DJV version, and the registered clients. |
| `ADMIN_TOKEN`    | (the file's `adminToken`) | The token the admin page asks for. Without one, the admin page only works on the server itself. |

For example, for a team on the local network:

```
HOST=0.0.0.0 ADMIN_TOKEN=some-long-secret node server/server.js
```

In DJV, the server address, the token, the client ID and the drive letter go
in the **Sync Server** section of the **Settings** tool; the session is
chosen in the **Sync** section of the **Review** tool (see the Review page of
the documentation).

## Configuration

`config.json` (copy `config.example.json` to start) holds what the server
tells DJV and who it lets in:

```json
{
  "name": "Dev review server",
  "storage": { "unc": "\\\\10.0.0.5\\media" },
  "djvVersion": "3.7.5",
  "requireRegistration": true,
  "adminToken": "",
  "clients": [ { "id": "edit-bay-1", "name": "Edit bay 1" } ]
}
```

- `storage.unc`: the read-only share with the review media. DJV maps it to
  the drive letter set on each PC. The server never opens it.
- `djvVersion`: the DJV version every PC should run. A PC running another is
  warned, and flagged on the admin page. Empty to not check.
- `requireRegistration`: only the `clients` listed may use the sessions.
  Others are refused with 403 and wait on the admin page for approval. Off
  when there is no configuration, so a server started without one lets in
  whoever has the token, as before.
- `clients`: the registered PCs, by the client ID set in each DJV.

The file is read when the server starts, and written by the admin page. A
file that cannot be read stops the server from starting rather than letting
everyone in. Fields starting with `_` are ignored, for comments.

## Admin page

`http://<server>:8787/admin` lists the PCs waiting for approval, the
registered clients (with their DJV version, address and when they were last
seen), who is connected to which session now, the sessions, and the server
settings. With an admin token set, it asks for it once and keeps it for the
browser tab; without one, it only works from the server itself.

DJV connects over plain HTTP. Across an untrusted network, put the server
behind a TLS reverse proxy and reach it through a tunnel, or keep it on a
VPN: the token is sent with every request.

## Tests

```
cd server
npm test
```

DJV's own tests include a live test against a running server:

```
DJV_SYNC_TEST_SERVER=http://127.0.0.1:8787 djv-test SyncModel
```

## How it works

A session is a set of markers, as they are written in a review file
(`docs/review-format.html`), and the media they are about. The server
numbers every change it receives — the *revision* — and passes it to
everyone following the session, in that order. The change it receives last
to a marker is the one that stands, so every client ends with the same
markers whatever their clocks say. Deleted markers are remembered, so a
client that was away learns of the deletion instead of sending the marker
back.

A client reads the session's state, then follows its event stream from the
revision the state was at, so nothing falls between the two. Changes it
makes are sent one at a time, in order, retried until the server takes
them. A client that was away for more than the last 1000 changes is told to
read the state again. The changes are kept in the database, so a restart of
the server does not count.

### Media

The media is the file the markers are about, kept on network storage that
every reviewer can reach. The server never opens it: it only passes on where
it is. Since each machine can reach the storage under a different name
(`\nas\media` on Windows, `/Volumes/media` on macOS, `/mnt/media` on
Linux), the path is given twice:

- `path`: relative to the client's *network media folder*, with forward
  slashes. Each client joins it to its own folder. Empty when the file is
  not inside the folder of the client that set it.
- `pathAbsolute`: the path as the client that set it saw it, tried when the
  relative one does not lead to the file.

`name` is the file name, for showing, and `sharedBy` who set it. A client
that joins a session with no media while it has a file open sets it.

### API

All under `/api`. With a token set, every request but `health` and the
admin ones needs `Authorization: Bearer <token>` (or `?token=<token>`, for
an EventSource): 401 without one, 403 with the wrong one.

Every request says which client it comes from, in two headers:

```
X-DJV-Client: edit-bay-1
X-DJV-Version: 3.7.5
```

With registration required, a session request from a client not registered
is refused: 403, `{ "error", "code": "unregistered", "client" }`. `health`
and `hello` are always answered.

| Request | Body | Answer |
|---|---|---|
| `GET /health` | | `{ "ok": true }` |
| `GET /hello` | | `{ "name", "serverVersion", "djvVersion", "requireRegistration", "storage": { "unc" }, "client": { "id", "name", "registered" } }` |
| `GET /sessions` | | `{ "sessions": [{ "session", "rev", "markers", "clients", "media" }] }`, `media` being the file's name or `null` |
| `GET /sessions/<session>` | | `{ "session", "rev", "media": {...} \| null, "markers": [...], "deleted": [ids] }` |
| `PUT /sessions/<session>/markers/<id>` | `{ "clientId", "marker" }` | `{ "rev" }` |
| `DELETE /sessions/<session>/markers/<id>?clientId=<id>` | | `{ "rev" }` |
| `PUT /sessions/<session>/media` | `{ "clientId", "media": { "path", "pathAbsolute", "name", "sharedBy" } }` | `{ "rev" }` |
| `DELETE /sessions/<session>/media?clientId=<id>` | | `{ "rev" }` |
| `GET /sessions/<session>/events?since=<rev>` | | Server-sent events |

Session names and marker identifiers are letters, digits, `.`, `_` and `-`,
up to 128 of them. A session that has never been written to is empty. The
media's fields are text of up to 4096 characters, and `path` or
`pathAbsolute` must be given.

Each event's data is JSON:

```
{ "type": "upsert", "rev": 12, "clientId": "...", "marker": { ... } }
{ "type": "delete", "rev": 13, "clientId": "...", "id": "..." }
{ "type": "media",  "rev": 14, "clientId": "...", "media": { ... } | null }
{ "type": "reset",  "rev": 14 }
```

`clientId` is the connection that made the change, so a client can tell its
own changes coming back from other people's; it is new each time DJV
connects, since two DJVs on one PC share a client ID. `client` is that
client ID (the `X-DJV-Client` header), for saying who made it. `reset` means
the changes after `since` are no longer kept: read the session again, and
follow from there. A comment is written every 15 seconds to keep the stream
open.

### Admin API

Under `/api/admin`, for the admin page. With an admin token set, every
request needs `Authorization: Bearer <admin token>` (401 without, 403 with a
wrong one, both with `"code": "admin-token"`); without one, only requests
from the server itself are answered.

| Request | Body | Answer |
|---|---|---|
| `GET /config` | | `{ "name", "storage": { "unc" }, "djvVersion", "requireRegistration", "adminTokenSet", "file" }` |
| `PUT /config` | any of `name`, `storage`, `djvVersion`, `requireRegistration` | as `GET`; 400 for a value that cannot be used |
| `GET /clients` | | `{ "registered": [...], "waiting": [...], "connected": [...] }` |
| `PUT /clients/<id>` | `{ "name" }` | the client, added or renamed |
| `DELETE /clients/<id>` | | `{ "ok": true }`, 404 for a client not registered |
| `GET /sessions` | | as `GET /api/sessions` |

Client IDs are letters, digits, `.`, `_` and `-`, up to 64 of them. The
waiting clients and when each client was last seen are kept in memory only.

### Storage

Every session is in one SQLite database, `DATA_DIR/djv-sync.db`: the
revision and media of each session, its markers, the deleted identifiers,
and the last 1000 changes. A change is written before the server answers
the client that sent it, so a change DJV was told was taken is not lost if
the server stops.

The database is in write-ahead mode, so beside it are `djv-sync.db-wal`
and `djv-sync.db-shm` while the server runs; they belong to it. To back it
up, copy the three files while the server is stopped, or, while it runs,
use `sqlite3 djv-sync.db ".backup backup.db"`.

Earlier versions of the server kept each session in a JSON file,
`DATA_DIR/<session>.json`. On start, any such file whose session is not yet
in the database is brought in, and renamed to `<session>.json.imported`.
