# Deploying the DJV sync server with Docker

This guide installs the DJV sync server on the studio's dev server, a Windows
machine at **192.168.0.100** running Docker Desktop. The server listens on port
**8877**. Install it on your own PC first to check that everything works; the
steps are the same there, at `http://localhost:8877`.

How the pieces fit together, from DJV's side, is in the manual's *Studio
network* page (`docs/lan-setup.html`). How the media folder is shared is in
[share-setup.md](share-setup.md), and how people use the server's review
portal is in [portal.md](portal.md).

## What the package holds

`server\build-package.ps1` in the DJV source builds the package as
`dist\djv-sync-server-<version>.zip`:

| File | What |
| --- | --- |
| `djv-sync-server-<version>.tar` | The Docker image (Node 24 on Alpine, with the server). `start.ps1` loads it. |
| `docker-compose.yml` | The container: port 8877, the `data` folder, the media folder, the two secrets, restarted with Docker. |
| `.env.example` | The port, the image version, the media folder, the time zone, and the database locking mode. `start.ps1` copies it to `.env`. |
| `new-secrets.ps1` | Makes the passwords in `secrets\`. |
| `share-setup.ps1` | Makes the `djv` account and the read-only media share, opens the firewall, and sets the media folder in `.env`. |
| `start.ps1`, `stop.ps1` | Start and stop the server. |
| `backup.ps1` | Saves a copy of the database while the server runs. |
| `README.md`, `docs\` | This guide, the share guide and the portal guide. |

The package never holds secrets, data or backups.

## Building the package

On a machine with the DJV source and Docker Desktop:

```powershell
cd <djv source>\server
.\build-package.ps1
```

This builds the image `djv-sync-server:<version>`, tagged `latest` too, and
writes `..\dist\djv-sync-server-<version>.zip`. The version comes from
`server\package.json`.

### Pushing the image to a private registry

The image can also go to a private registry, GitHub's for example, so that
the dev server pulls it rather than loading it from the package. Then an
update needs no new package; only a new version in `.env`.

1. **Make a token.** GitHub's registry takes a *classic* personal access
   token, not a fine-grained one: `write:packages` to push, and, for the dev
   server, a second one with only `read:packages`. If the organization uses
   single sign-on, authorize each token for it (*Configure SSO*).
2. **Sign in** on the machine that builds:
   `docker login ghcr.io -u <user>`, with the first token as the password.
   Docker keeps it in the Windows Credential Manager.
3. **Build and push:**

   ```powershell
   .\build-package.ps1 -Push ghcr.io/<organization>/djv-sync-server
   ```

   This pushes `<version>` and `latest`, and the package's `.env.example`
   names the repository as `DJV_SYNC_IMAGE`. The repository must be in lower
   case.

The first push makes the package, private. Who may pull it is set on the
package's page, under *Package settings → Manage access*. If the push is
refused, an owner of the organization may have to allow members to make
packages (the organization's *Settings → Packages*).

To have the dev server pull it:

1. Sign in there once, as the Windows user that runs `start.ps1`:
   `docker login ghcr.io -u <user>`, with the read-only token.
2. Set `DJV_SYNC_IMAGE=ghcr.io/<organization>/djv-sync-server` in its `.env`.
   A `.env` made from an earlier package has no such line: add it.
3. Run `.\start.ps1`.

`start.ps1` pulls the image at every start. When the registry cannot be
reached, it starts the copy Docker already has, or else loads the package's
`.tar`.

## First install

1. **Install Docker Desktop** on the dev server. Under its settings, turn on
   *Start Docker Desktop when you sign in to your computer*.
   - The container restarts with Docker.
   - Docker Desktop starts when a user signs in, so set the dev server to
     sign in automatically after a restart, or sign in after each one.
2. **Unzip the package** into a folder of its own, for example
   `C:\djv-sync`. Open a PowerShell in that folder.
   - If Windows blocks the scripts, run
     `Set-ExecutionPolicy -Scope CurrentUser RemoteSigned`.
   - Or unblock the files with `Get-ChildItem | Unblock-File`.
3. **Make the passwords** with `.\new-secrets.ps1`. It writes two files:
   - `secrets\admin_password.txt`: 24 random characters, for the
     administrator. Its user name is `ADMIN_USER` in `.env`, `admin` unless
     you set another before the first start.
   - `secrets\share_password.txt`: 16 random letters and digits, for the
     media share's `djv` account. Give `-SharePassword '...'` to use the
     studio's standard password instead.

   Running it again keeps the existing files, and `-Force` replaces them.
   These files are the only copy of the passwords: keep the folder private,
   and back it up somewhere safe.
4. **Share the media.** In a PowerShell started *as an administrator*, run
   `.\share-setup.ps1 -Path D:\Media`. [share-setup.md](share-setup.md)
   explains what it does, and how to do it by hand. It also writes
   `DJV_MEDIA_DIR=D:\Media` into `.env`, so that the server's portal can
   upload videos into that folder (see *The media folder* below).
5. **Start the server** with `.\start.ps1`. It:
   1. pulls the image from the registry when `DJV_SYNC_IMAGE` is set (see
      *Pushing the image to a private registry* above), and otherwise loads
      it from the `.tar` when Docker does not have it yet;
   2. starts the container;
   3. waits for the server to answer;
   4. prints the addresses of the portal and the admin page.
6. **Configure it.** Open `http://192.168.0.100:8877/admin` and sign in as
   the administrator (**admin**, or your `ADMIN_USER`), with the password
   from `secrets\admin_password.txt`.
   1. Under **Server settings**, set:
      - **Media share**: `\\192.168.0.100\media`;
      - **Share user**: `djv`;
      - **Expected DJV version**: the version the studio runs.
   2. Under **Users**, add each reviewer with a password.
7. **Set up the PCs**, as the manual's *Studio network* page says. DJV's
   default server address is the one it was built with: `DJV_SYNC_SERVER` in
   the `.env` at the top of the DJV source (see `.env.example`). With the
   studio's address there, a reviewer only types their user name and
   password into the **Sync Server** settings.
8. **Tell the reviewers about the portal**, `http://192.168.0.100:8877`, where
   they upload videos and make sessions with the same sign-in
   ([portal.md](portal.md)).

## Where everything is

| Path | What |
| --- | --- |
| `data\djv-sync.db` | The sessions, their notes, and the sign-ins. This is the bind-mounted SQLite file. |
| `data\config.json` | The server settings and the users. The passwords in it are hashed with scrypt. |
| `secrets\admin_password.txt` | The administrator's password. The administrator (`ADMIN_USER`) is made, or given this password, at every start. |
| `secrets\share_password.txt` | The `djv` account's password. Signed-in DJVs map the share with it. |
| `DJV_MEDIA_DIR` (in `.env`), for example `D:\Media` | The shared folder of videos. The portal lists it and uploads to it; unfinished uploads wait in its hidden `.djv-uploads` folder. |
| `backups\` | What `backup.ps1` saves. |
| `.env` | The deployment's settings, among them `ADMIN_USER`. |

Inside the container, these are `/data`, `/media` and `/run/secrets/...`.
The secrets reach the container as Docker secrets: they are in neither the
image nor its environment.

### The media folder

The server needs to write to the shared folder for the portal's uploads, so
the compose file mounts `DJV_MEDIA_DIR` into the container read-write, as
`/media`.

- Docker Desktop writes to it with the rights of the Windows user it runs
  as, which must be allowed to change the folder. An administrator who made
  the share is.
- The SMB share stays **read-only** for the `djv` account. The PCs only
  read; only the server writes.
- The folder must be on a drive of the dev server, not on another machine's
  share.
- `start.ps1` stops if `DJV_MEDIA_DIR` names a folder that is not there.
  When it is empty, uploads go to a `media` folder beside the compose file,
  which nobody else sees; `start.ps1` warns about that.

To use another folder, run `share-setup.ps1 -Path <folder>` again, or edit
`DJV_MEDIA_DIR` in `.env`, then restart the server.

### The SQLite file on a Windows host

A folder of a Windows host, mounted into a Linux container, cannot share
memory between processes. SQLite's write-ahead log needs that for its
`-shm` file. The compose file therefore sets `DB_EXCLUSIVE=1`, and the server
then opens the database with exclusive locking:

- The index is kept in the server's own memory.
- The database is safe however the server stops.
- No other program can open `djv-sync.db` while the server runs.

To look inside the database, use one of these copies:

- `.\backup.ps1`, or **Download a backup** on the admin page. Either gives a
  consistent copy, taken while the server runs, which opens in
  [DB Browser for SQLite](https://sqlitebrowser.org/) or `sqlite3`.
- Stop the server with `.\stop.ps1` and copy `data\djv-sync.db`.

On a Linux host, set `DB_EXCLUSIVE=0` in `.env`.

## Day to day

| Task | How |
| --- | --- |
| See whether it runs | `docker ps` shows `djv-sync`, *healthy*. |
| Follow its log | `docker compose logs -f` |
| Back up | `.\backup.ps1`, or `.\backup.ps1 -Keep 30` to keep the last 30. A scheduled task can run it nightly. Also keep a copy of `secrets\` and `data\config.json`. |
| Restart | `.\stop.ps1`, then `.\start.ps1` |
| Change the admin password | Put the new one in `secrets\admin_password.txt`, then restart. Earlier admin sign-ins end. |
| Change the share password | Put the new one in `secrets\share_password.txt`, run `.\share-setup.ps1 -Path D:\Media` as an administrator, then restart. The PCs get it the next time they sign in. |
| Add or remove a reviewer | On the admin page, under **Users**. |

## Restoring a backup

1. Run `.\stop.ps1`.
2. Copy the backup over `data\djv-sync.db`.
3. Delete `data\djv-sync.db-wal` and `data\djv-sync.db-shm` if they are
   there.
4. Restore `data\config.json` from the same day, if the users changed since.
5. Run `.\start.ps1`.

## Updating

1. Build or get the new package, and unzip it over the folder. `data`,
   `secrets`, `backups` and `.env` are not in the package, so they stay as
   they are.
2. Set `DJV_SYNC_VERSION` in `.env` to the new version, or to `latest`.
3. Run `.\stop.ps1`, then `.\start.ps1`. The new image is loaded, and the
   server carries on from the same data.

With `DJV_SYNC_IMAGE` set, step 1 is not needed unless the scripts
changed: set the new version and run `.\start.ps1`, which pulls it.

### From 2.1 to 2.2

Version 2.2 takes the administrator's name from `ADMIN_USER` in `.env`, and
its password from `secrets\admin_password.txt`. `start.ps1` moves an older
deployment over by itself: it renames the old password file, which was
named after the administrator, and writes that name into `.env` as
`ADMIN_USER`. The administrator, its password and the users stay as they
were.

### From 2.0 to 2.1

Version 2.1 adds the review portal.

1. Set the media folder before starting: run
   `.\share-setup.ps1 -Path D:\Media` again as an administrator, or add
   `DJV_MEDIA_DIR=D:\Media` to `.env` by hand.
2. Sessions made with 2.0 have no list of people, so every user sees them.
   The administrator can **Edit** one on the portal to limit it.
3. Review links and two-video sessions need the DJV built with it, on every
   PC. A DJV from before still syncs notes.

## Testing on your own PC first

1. Build the package, unzip it into a scratch folder, and run
   `.\new-secrets.ps1` and `.\start.ps1` there.
2. Open `http://localhost:8877/admin`. Sign in as admin, and add a user
   `test`. Set `DJV_MEDIA_DIR` in `.env` to a test folder of videos, such as
   `C:\djv-media-test`, before starting, so that the portal has something to
   list and somewhere to upload.
3. In DJV, set the server to `http://localhost:8877` and sign in as `test`.
   Connect two DJVs to the same session and write notes.
4. Check that `data\djv-sync.db` and `data\config.json` appear in the folder,
   that `.\stop.ps1` and `.\start.ps1` keep the users and the notes, and that
   `.\backup.ps1` gives a file that opens.
5. Open the portal at `http://localhost:8877` and sign in as `test`.
   Upload a video, make a session of it, and click **review**. For the link
   to be taken, DJV's server must be `http://localhost:8877` too.
6. To try the media drive, share a test folder on your PC with
   `share-setup.ps1`, as [share-setup.md](share-setup.md) says, and set the
   media share on the admin page to `\\<your PC's address>\media`.
7. Remove the test with `.\stop.ps1`, `share-setup.ps1 -Path <folder>
   -Remove`, and by deleting the scratch folder.

The automated checks are in the DJV source:

- `cd server; npm test` runs the server's tests.
- `djv-test SyncModel`, with these variables set, runs DJV against a running
  server:
  - `DJV_SYNC_TEST_SERVER=http://127.0.0.1:8877`
  - `DJV_SYNC_TEST_USER=test`
  - `DJV_SYNC_TEST_PASSWORD=<its password>`

## Security notes

- Plain HTTP carries the passwords from DJV and the admin page across the
  LAN at sign-in. After that, a random session token is sent instead.
  Encryption is a follow-up (CR-010).
- The server hands the share's password only to signed-in users. Each PC
  keeps it in the Windows Credential Manager for the server, as `cmdkey`
  would.
- After 10 failed sign-ins from one address, the server stops accepting
  sign-ins from that address for a few minutes.
- A session token lasts 30 days from its last use. Giving a user a new
  password, or removing them, ends theirs at once.
- The portal keeps its sign-in for the browser tab only.
- DJV takes a review link only for the server in its settings, and only
  from programs of the same Windows user.
