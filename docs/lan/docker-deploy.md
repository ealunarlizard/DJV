# Deploying the DJV sync server with Docker

This guide installs the DJV sync server on the studio's dev server, a Windows
machine at **192.168.1.40** running Docker Desktop. The server listens on port
**8877**. Install it on your own PC first to check that everything works; the
steps are the same there, at `http://localhost:8877`.

How the pieces fit together, from DJV's side, is in the manual's *Studio
network* page (`docs/lan-setup.html`). How the media folder is shared is in
[share-setup.md](share-setup.md).

## What the package holds

`server\build-package.ps1` in the DJV source builds the package as
`dist\djv-sync-server-<version>.zip`:

| File | What |
| --- | --- |
| `djv-sync-server-<version>.tar` | The Docker image (Node 24 on Alpine, with the server). `start.ps1` loads it. |
| `docker-compose.yml` | The container: port 8877, the `data` folder, the two secrets, restarted with Docker. |
| `.env.example` | The port, the image version, the time zone, and the database locking mode. `start.ps1` copies it to `.env`. |
| `new-secrets.ps1` | Makes the passwords in `secrets\`. |
| `share-setup.ps1` | Makes the `djv` account and the read-only media share, and opens the firewall. |
| `start.ps1`, `stop.ps1` | Start and stop the server. |
| `backup.ps1` | Saves a copy of the database while the server runs. |
| `README.md`, `docs\` | This guide, and the share guide. |

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
   - `secrets\lldjv_password.txt`: 24 random characters, for the
     administrator `lldjv`.
   - `secrets\share_password.txt`: 16 random letters and digits, for the
     media share's `djv` account. Give `-SharePassword '...'` to use the
     studio's standard password instead.

   Running it again keeps the existing files, and `-Force` replaces them.
   These files are the only copy of the passwords: keep the folder private,
   and back it up somewhere safe.
4. **Share the media.** In a PowerShell started *as an administrator*, run
   `.\share-setup.ps1 -Path D:\Media`. [share-setup.md](share-setup.md)
   explains what it does, and how to do it by hand.
5. **Start the server** with `.\start.ps1`. It:
   1. loads the image from the `.tar` when Docker does not have it yet;
   2. starts the container;
   3. waits for the server to answer;
   4. prints the admin page's address.
6. **Configure it.** Open `http://192.168.1.40:8877/admin` and sign in as
   **lldjv**, with the password from `secrets\lldjv_password.txt`.
   1. Under **Server settings**, set:
      - **Media share**: `\\192.168.1.40\media`;
      - **Share user**: `djv`;
      - **Expected DJV version**: the version the studio runs.
   2. Under **Users**, add each reviewer with a password.
7. **Set up the PCs**, as the manual's *Studio network* page says. The default
   server address in DJV is already `http://192.168.1.40:8877`, so a reviewer
   only types their user name and password into the **Sync Server** settings.

## Where everything is

| Path | What |
| --- | --- |
| `data\djv-sync.db` | The sessions, their notes, and the sign-ins. This is the bind-mounted SQLite file. |
| `data\config.json` | The server settings and the users. The passwords in it are hashed with scrypt. |
| `secrets\lldjv_password.txt` | The administrator's password. `lldjv` is made, or given this password, at every start. |
| `secrets\share_password.txt` | The `djv` account's password. Signed-in DJVs map the share with it. |
| `backups\` | What `backup.ps1` saves. |
| `.env` | The deployment's settings. |

Inside the container, these are `/data` and `/run/secrets/...`. The secrets
reach the container as Docker secrets: they are in neither the image nor its
environment.

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
| Change the lldjv password | Put the new one in `secrets\lldjv_password.txt`, then restart. Earlier admin sign-ins end. |
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

## Testing on your own PC first

1. Build the package, unzip it into a scratch folder, and run
   `.\new-secrets.ps1` and `.\start.ps1` there.
2. Open `http://localhost:8877/admin`. Sign in as lldjv, and add a user
   `test`.
3. In DJV, set the server to `http://localhost:8877` and sign in as `test`.
   Connect two DJVs to the same session and write notes.
4. Check that `data\djv-sync.db` and `data\config.json` appear in the folder,
   that `.\stop.ps1` and `.\start.ps1` keep the users and the notes, and that
   `.\backup.ps1` gives a file that opens.
5. To try the media drive, share a test folder on your PC with
   `share-setup.ps1`, as [share-setup.md](share-setup.md) says, and set the
   media share on the admin page to `\\<your PC's address>\media`.
6. Remove the test with `.\stop.ps1`, `share-setup.ps1 -Path <folder>
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
