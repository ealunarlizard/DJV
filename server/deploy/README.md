# DJV sync server: Docker package

This folder runs the DJV sync server in Docker on the studio's server.
`docs/docker-deploy.md` has the full guide. `docs/share-setup.md` explains
how to share the media folder.

## First install

From a PowerShell in this folder, on the server:

```powershell
.\new-secrets.ps1                        # makes secrets\lldjv_password.txt and secrets\share_password.txt
.\share-setup.ps1 -Path D:\Media         # as an administrator: the djv account, the share, the firewall
.\start.ps1                              # loads the image, starts the container
```

Then open `http://<server>:8877/admin` and sign in as **lldjv** with the
password from `secrets\lldjv_password.txt`.

1. Under **Server settings**, set the media share, for example
   `\\192.168.1.40\media`.
2. Under **Users**, add the people who review.

## What is where

| Path | What |
| --- | --- |
| `data\djv-sync.db` | The sessions, their notes, and the sign-ins (SQLite). |
| `data\config.json` | The server settings and the users. The passwords in it are hashed. |
| `secrets\` | The passwords, one per file. This is the only copy, so keep the folder private. |
| `backups\` | What `backup.ps1` saves. |
| `.env` | The port, the image version, and the time zone. It is made from `.env.example`. |

## Day to day

- `.\backup.ps1` saves a copy of the database while the server runs.
- `.\stop.ps1` and then `.\start.ps1` restart the server.
- `docker compose logs -f` follows the server's log.

To update, unzip the new package over this folder. Your `data`, `secrets`
and `.env` are not in the package and stay as they are. Then:

1. Set `DJV_SYNC_VERSION` in `.env` to the new version.
2. Run `.\start.ps1`.
