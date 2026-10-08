# Changelog: DJV review on a studio LAN

What is implemented, release by release, for running DJV reviews on a
studio network. Each line cites its change request in
[CHANGE_REQUESTS.md](CHANGE_REQUESTS.md).

The format follows [Keep a Changelog](https://keepachangelog.com/). The
versions are this work's own (`lan-X.Y.Z`): the DJV version it is built on
is given with each one, and the user-facing DJV changes are also in the
root `ChangeLog.md`, under the DJV version.

## [lan-0.1.0] - Unreleased

Built on DJV 3.7.5-dev, with sync server 1.1.0 (`server/package.json`).
Branch `feature/lan-server`.

### Added

- Live sharing of review markers between DJVs through the sync server,
  with the session saved in the review. (CR-001)
- The server keeps its sessions in SQLite, writing each change before
  answering it. The last 1000 changes are kept, so clients catch up after
  a restart. (CR-002)
- Sessions kept as JSON files by the first server are imported once at
  start. (CR-002)
- A session remembers its media. A DJV that joins with nothing open loads
  it; Share and Load are in the Review tool's Sync section. (CR-003)
- Each PC has a client ID, set in the new **Sync Server** settings and
  sent with every request. (CR-004)
- The server keeps a registry of client IDs in `server/config.json`. With
  `requireRegistration`, only registered PCs may use the sessions; a refused
  PC waits for approval and joins once approved. (CR-004)
- `GET /api/hello` tells DJV the server's name, the media share, the
  expected DJV version, and whether this PC is registered. (CR-004)
- An admin page at `/admin` for approving and managing clients, seeing
  who is connected and the sessions, and editing the server settings.
  (CR-005)
- On Windows, DJV maps the server's media share to a drive letter chosen
  on each PC, at start and on demand. The share is kept, so the drive is
  mapped even when the server is down. (CR-006)
- Media shared from a mapped drive carries its UNC path, so PCs with other
  letters find it. (CR-006)
- `Review/SyncTestServer` and `Review/SyncMapDrive` commands, in the
  Review menu as **Check Sync Server** and **Map Media Drive**.
  (CR-004, CR-006)
- A warning when a PC runs another DJV version than the studio's, and the
  versions shown on the admin page. (CR-007)
- The *Studio network* page of the manual (`docs/lan-setup.html`), this
  changelog, and the change request log. (CR-008)

### Changed

- The sync server, the token and the media folder move from the Review
  settings to the Sync Server settings (`/Sync.1`), and are moved across
  on the first start. (CR-004)
- A settings reset keeps the server, the token, the client ID and the
  media drive. (CR-004)
- The sync server needs Node.js 22.13 or newer. (CR-002)

### Fixed

- The server took a `config.json` kept in its data folder for a session
  from the first server, and renamed it. It now imports only files that
  are sessions. (CR-004)
