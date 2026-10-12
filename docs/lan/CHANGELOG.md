# Changelog: DJV review on a studio LAN

What is implemented, release by release, for running DJV reviews on a
studio network. Each line cites its change request in
[CHANGE_REQUESTS.md](CHANGE_REQUESTS.md).

The format follows [Keep a Changelog](https://keepachangelog.com/). The
versions are this work's own (`lan-X.Y.Z`): the DJV version it is built on
is given with each one, and the user-facing DJV changes are also in the
root `ChangeLog.md`, under the DJV version.

## [lan-0.3.0] - Unreleased

Built on DJV 3.7.5-dev, with sync server 2.1.0 (`server/package.json`).
Branch `feature/lan-server`. A DJV of lan-0.2.0 still syncs notes with this
server; the review links and two-video sessions need this DJV.

### Added

- The review portal, at the server's address, used with the same accounts
  as DJV (`server/portal.html`, `docs/lan/portal.md`). (CR-020)
- Uploading videos to the media share from the portal, in resumable 32 MB
  pieces; the list of videos is the shared folder's. Only lldjv deletes
  videos. (CR-018)
- `MEDIA_DIR`, the shared folder mounted read-write into the container as
  `/media` from `DJV_MEDIA_DIR` in `.env`, which `share-setup.ps1` sets.
  (CR-018)
- Sessions with a date, a description, a creator and the people they are
  for; made on the portal with a name the server chooses, edited or deleted
  by the creator and lldjv. (CR-019)
- Sessions of two videos: the media's `compare` file, opened in DJV as B,
  side by side. (CR-021)
- Review links, `djv://session/<session>?server=<server>`: DJV registers
  itself for them, a running DJV takes them from a new one, and the link
  opens the session with its videos. The command `Review/SyncOpenLink`, and
  **Review links** in the Sync Server settings. (CR-022)

### Changed

- A session is seen and joined only by its people, its creator and lldjv
  (403 `not-member`). A name first opened from DJV is that user's session.
  Sessions of server 2.0 stay open to everyone. (CR-019)
- `GET /api/sessions` lists the user's sessions, newest first, with their
  date, description, people, videos and who is connected. (CR-019)
- The database layout is version 3: `session_info` and `uploads`. (CR-018,
  CR-019)
- A deleted session, or one a user was taken out of, ends its streams with
  a `removed` event; DJV stops sending to it. (CR-019)

## [lan-0.2.0] - Unreleased

Built on DJV 3.7.5-dev, with sync server 2.0.0 (`server/package.json`).
Branch `feature/lan-server`. The server's API changed: DJV and the server
must both be this version.

### Added

- A Docker package for the sync server: an image built from
  `server/Dockerfile`, and `dist/djv-sync-server-<version>.zip` from
  `server/build-package.ps1`. The zip holds the image, the compose file and
  the start, stop, backup, secrets and share scripts. The server listens on
  8877, with its SQLite database and configuration in a `data` folder of the
  host. (CR-013)
- The admin page's **Download a backup**, and `backup.ps1`: a consistent
  copy of the database while the server runs. (CR-013)
- Users with passwords. People sign in with `POST /api/login` and get a
  session token for every other request. Passwords are hashed with scrypt.
  Failed sign-ins are throttled. (CR-014)
- The administrator `lldjv`, made from a password file at every start. Only
  `lldjv` uses the admin page, which now signs in and manages users. (CR-014)
- **User**, **Password**, **Sign in** and **Sign out** in DJV's Sync Server
  settings. The password is kept in the Windows Credential Manager, and DJV
  signs in by itself at start. (CR-014)
- The server gives signed-in DJVs the media share's account, and DJV maps
  the drive with it and keeps it for the server in the Credential Manager.
  (CR-015)
- `share-setup.ps1` and `docs/lan/share-setup.md`, for sharing the media
  folder read-only to a `djv` account with a standard password. (CR-016)
- `docs/lan/docker-deploy.md`, the deployment guide. (CR-013)

### Changed

- DJV's default sync server is `http://192.168.1.40:8877`, and the server's
  default port is 8877. (CR-013)
- The client ID is only the PC's name, shown on the admin page; the client
  registry, the waiting list and requireRegistration are gone. (CR-014,
  supersedes CR-004's registry)
- The admin token (`ADMIN_TOKEN`) and the shared access token
  (`DJV_SYNC_TOKEN`, and DJV's **Token** setting) are gone; sign-ins replace
  both. (CR-014)
- Events carry the `user` who made them, in place of the `client`. (CR-014)

## [lan-0.1.0] - 2026-10-08

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
