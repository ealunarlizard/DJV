# Sharing the media folder for DJV

The review media lives in a folder on the dev server (192.168.0.100). DJV
reads it over the network as a read-only share, `\\192.168.0.100\media`,
signing in to it as a Windows account called **djv** that has a standard
password. This guide sets that up.

## How it works

- The **djv** account is a local account on the dev server, used only for
  reading the media. Nobody signs in to Windows with it.
- Its password is the studio's *standard password*. It lives in one local
  file, `secrets\share_password.txt`, beside the sync server's package, and
  is never committed or sent anywhere else.
- The sync server reads that file when it starts. It gives the share's
  address, account and password to a DJV only after its user has signed in
  with their own user name and password.
- DJV maps the share to the drive letter chosen on that PC, signing in to it
  as `192.168.0.100\djv`. Windows then keeps the account for the server in the
  Credential Manager, so the drive can be mapped again even while the sync
  server is down.

Reviewers never type or see the share's password.

## 1. Choose the standard password

In the sync server's folder on the dev server:

```powershell
.\new-secrets.ps1
```

This writes a random 16-character password, letters and digits only, into
`secrets\share_password.txt`. To use a password the studio already has:

```powershell
.\new-secrets.ps1 -SharePassword 'Studio-Media-2026'
```

The file holds the password and nothing else: no quotes and no line break.
Keep the `secrets` folder private.

If the dev server enforces password complexity, as Windows Server does by
default, the password must mix at least three of these: upper case letters,
lower case letters, digits, and symbols.

## 2. Share the folder

### With the script

In a PowerShell started **as an administrator**, in the same folder:

```powershell
.\share-setup.ps1 -Path D:\Media
```

It:

1. Makes the local account `djv` with the password from the file, and sets
   it so the password never expires and the account cannot change it. If the
   account is already there, it gets that password.
2. Gives `djv` read access to `D:\Media` and everything in it (NTFS: *Read &
   execute*).
3. Shares `D:\Media` as `media`: read-only for `djv`, and full control for
   the administrators, so media can still be added from the server itself.
4. Turns on Windows' *File and Printer Sharing* firewall rules, and opens TCP
   8877 for the sync server. Both apply to private and domain networks only.
5. Writes `DJV_MEDIA_DIR=D:\Media` into the sync server's `.env`, so that its
   portal uploads videos into the folder. The share stays read-only: the
   server writes to the folder from its container, not through the share.
   Restart the server for it to take effect.

At the end it prints the UNC path to put on the admin page.

| Option | |
| --- | --- |
| `-ShareName video` | Another share name than `media`. |
| `-User djv-media` | Another account name than `djv`. |
| `-WhatIf` | Show what would be done, and do nothing. |
| `-Remove` | Remove the share, the account and the firewall rule, leaving the folder and its files as they are. |

The dev server's network must be **Private** (or a domain network). Windows
does not share files on a Public one: see *Settings > Network & internet >
Ethernet > Network profile type*.

### By hand

The same steps without the script, on the dev server:

1. **Make the account.**
   1. Run `lusrmgr.msc`, then go to *Users*, and choose *Action > New User*.
   2. Set the user name to `djv` and the password to the standard password.
   3. Untick *User must change password at next logon*.
   4. Tick *User cannot change password* and *Password never expires*.

   From a command line instead:

   ```
   net user djv <password> /add /passwordchg:no /expires:never
   wmic useraccount where name='djv' set PasswordExpires=false
   ```
2. **Let it read the folder.**
   1. Open the folder's *Properties > Security*, choose *Edit > Add*, and add
      `djv`.
   2. Leave it *Read & execute*, *List folder contents* and *Read*, and
      nothing more.
3. **Share the folder.**
   1. Open *Properties > Sharing > Advanced Sharing*, tick *Share this
      folder*, and name it `media`.
   2. Under *Permissions*, remove *Everyone*, then add `djv` with *Read* and
      *Administrators* with *Full Control*.
4. **Open the firewall.** Run `wf.msc` and enable the *File and Printer
   Sharing (SMB-In)* rule for the private profile. Then add an inbound rule
   for TCP port 8877, also for the private profile.

## 3. Tell the sync server

1. Sign in to `http://192.168.0.100:8877/admin` as `admin`.
2. Under **Server settings**, set:
   - **Media share**: `\\192.168.0.100\media`;
   - **Share user**: `djv`.
3. Click **Save settings**.

**Share password** on that page must say *loaded*. If it says *not set*, the
server was started without `secrets\share_password.txt`: create the file and
restart the server with `.\stop.ps1`, then `.\start.ps1`.

If the share user has no domain, DJV signs in as `<server>\<user>`, for
example `192.168.0.100\djv`. That is the dev server's own local account, even
on a PC that belongs to a domain. To use a domain account, give it as
`DOMAIN\user`.

## 4. Check it from a PC

From any PC on the LAN, without DJV:

```
net use \\192.168.0.100\media /user:192.168.0.100\djv
dir \\192.168.0.100\media
net use \\192.168.0.100\media /delete
```

`net use` asks for the password. If `dir` lists the media, the share works.

Then in DJV:

1. Choose a **Media drive** in the **Sync Server** settings.
2. Sign in.
3. The **Drive** line should say `S: is \\192.168.0.100\media`.

## Changing the standard password

1. Put the new password in `secrets\share_password.txt`. To make a random
   one, run `.\new-secrets.ps1 -Force`, which also replaces the admin
   password.
2. Run `.\share-setup.ps1 -Path D:\Media` again as an administrator. It gives
   the account the new password.
3. Restart the sync server: `.\stop.ps1`, then `.\start.ps1`.

A PC that already mapped the drive keeps it until it is disconnected or the
PC restarts. At the next start, DJV signs in, receives the new password,
maps the drive with it, and stores it in place of the old one.

## Troubleshooting

| DJV says | Cause, and what to do |
| --- | --- |
| *Windows was not let in to \\\\192.168.0.100\\media as 192.168.0.100\\djv* | The account or its password does not match the file. Run `share-setup.ps1` again, then restart the server. |
| *…already connected to the server … under another user name* | The PC already has a connection to 192.168.0.100 with another account, for example a drive mapped by hand. Windows allows one account per server: run `net use`, delete the other connections to 192.168.0.100, then **Check** in DJV. |
| *Cannot reach \\\\192.168.0.100\\media* | The dev server is off, the share does not exist, or the firewall blocks SMB (TCP 445). Check *File and Printer Sharing* and the network profile. |
| The drive maps but is empty | `djv` can open the share but not read the folder. Check the NTFS permissions (step 2). |
