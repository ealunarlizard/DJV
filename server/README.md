# DJV sync server

Shares the markers of a DJV review between the people who have it open, as
they are written. Everyone who joins the same *session* sees the same
markers; an addition, an edit or a deletion made by one of them reaches the
others within a moment. A session also remembers the media file its markers
are about, so someone who joins with nothing open gets that file loaded.
Drawings are not shared.

It is one file with no dependencies beyond Node.js 22.13 or newer, which has
SQLite built in.

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
| `DJV_SYNC_TOKEN` | (none)                    | An access token every client must give. Set it whenever `HOST` is not this machine alone. |

For example, for a team on the local network:

```
HOST=0.0.0.0 DJV_SYNC_TOKEN=some-long-secret node server/server.js
```

In DJV, the server address, the token and the network media folder go in the
**Review** section of the **Settings** tool; the session is chosen in the
**Sync** section of the **Review** tool (see the Review page of the
documentation).

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

All under `/api`. With a token set, every request but `health` needs
`Authorization: Bearer <token>` (or `?token=<token>`, for an EventSource):
401 without one, 403 with the wrong one.

| Request | Body | Answer |
|---|---|---|
| `GET /health` | | `{ "ok": true }` |
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

`clientId` is the client that made the change, so a client can tell its own
changes coming back from other people's. `reset` means the changes after
`since` are no longer kept: read the session again, and follow from there.
A comment is written every 15 seconds to keep the stream open.

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
