# The review portal

The sync server has a web page, the **portal**, at its address:
`http://192.168.1.40:8877`. Everyone signs in to it with the same user name
and password they use in DJV. In the portal you can:

- upload videos to the media share;
- see the review sessions you are in, with their date, description and
  people;
- make a session of one video, or of two to compare;
- open a session in DJV with its **review** link.

The administrator, `lldjv`, sees every session. The admin page, for users and
server settings, is still at `/admin`.

## Videos

The **Videos** tab lists every video in the media share, as DJV sees it:
`.mov`, `.mp4`, `.m4v`, `.mxf`, `.mkv`, `.avi`, `.webm`, `.mpg`, `.mpeg`,
`.ts`, `.y4m` and `.wav`. The list is read from the folder itself, so a file
copied onto the dev server by hand appears in it too.

### Uploading

1. Under **Into the folder**, pick an existing folder, or type a new one,
   such as `shots/sh010`. Left empty, the file goes to the top of the share.
2. Drop the files on the box, or click **choose files**.

Each file goes up in pieces of 32 MB, three files at a time.

- **Pause** stops a file and **Resume** carries on.
- A piece that fails is sent again by itself, after a short wait that grows
  each time. A dropped network connection, or a restart of the server,
  therefore costs one piece rather than the file.
- A file is listed only once all of it has arrived. Until then it waits in a
  hidden folder of the share, `.djv-uploads`.
- If the page is closed or reloaded, the portal lists the file as
  **unfinished**. Click **Carry on…** and choose the same file again; the
  upload carries on from where it stopped. Unfinished uploads are dropped
  after a week.
- A file never replaces another file of the same name, whatever the case of
  the letters. To upload a new version, give it a new name, such as
  `sh010_v002.mov`.

Names must be valid on Windows:

- none of `< > : " | ? * \`;
- not ending in a dot or a space;
- not starting with a dot;
- not a name Windows reserves, such as `CON` or `LPT1`.

### Deleting

Only `lldjv` deletes videos, with **Delete** in the list. If a session still
uses the file, the portal names the session and asks again first.

## Sessions

The **Sessions** tab lists the sessions you are in, newest first. Each one
shows:

- its date and description;
- its videos, A and B;
- its people, with a ★ beside the person who made it, and in green whoever
  has it open in DJV now;
- the number of notes.

The filter box finds a session by any of these.

### Making one

Under **New session**:

1. Write a **Description**, such as *SH010 lighting, v1 against v2*.
2. Set the **Date**. It is today unless changed.
3. Tick the **People** to invite. You are always one of them.
4. Pick video **A**, and if you want to compare, video **B**. The search box
   narrows both lists.
5. Click **Create session**.

The server names the session from the date, the description and four random
characters, for example `20261012-sh010-lighting-v1-against-v2-k7qm`. Two
sessions therefore never share a name.

### Who sees a session

Only the people invited, whoever made it, and `lldjv` see the session or can
open it in DJV. Anyone else gets *not one you were asked to* from DJV.

A session someone starts in DJV by typing a new name belongs to them alone.
It shows on their portal without a description; they can **Edit** it there
to add one, set its date and videos, and invite people.

Sessions made before server 2.1 are open to everyone, and are marked
*everyone (an older session)*. `lldjv` can edit one to limit it.

### Editing and deleting

The person who made a session, and `lldjv`, see **Edit** and **Delete**.

- **Edit** changes the description, the date, the people or the videos.
  - A DJV in the session follows a change of videos.
  - Someone no longer invited is taken out of it.
- **Delete** removes the session and all its notes. Each DJV in it is told,
  and leaves. The videos stay in the share.

## Review links

**review** opens the session in DJV on the PC you click it on:

- DJV starts if it is not running. If it is, the running DJV takes the link
  and comes to the front.
- If the review open in DJV has unsaved changes, DJV asks whether to save
  them first.
- DJV joins the session and opens its video, or its two videos side by side
  as A and B.

**Copy link** copies the link, to send to someone in a message. It works
only for people invited to the session.

### What it needs

- **DJV registered for review links on that PC.** The installer does this.
  A DJV that was only unzipped or built does it the first time it starts,
  so start it once by hand.
  - **Settings → Sync Server → Review links** says which DJV the links open.
  - **Use this DJV** makes it this one.
- **The same server address in the browser as in DJV.** If DJV is set to
  `http://192.168.1.40:8877`, open the portal at that address, not through
  another name or `localhost`. DJV refuses a link for another server, so
  that a web page cannot send it, and the password it keeps, somewhere else.
- **A sign-in.** DJV signs in by itself with the password it keeps. If it
  has none, it opens the **Sync Server** settings, and joins the session as
  soon as you sign in.
- **The media share mapped**, or a media folder set, so that DJV can find
  the videos (see [share-setup.md](share-setup.md)).

The first time, the browser asks whether to open DJV. Tick *Always allow*
for the portal's address, and it will not ask again.

## Troubleshooting

| What happens | Why, and what to do |
| --- | --- |
| Clicking **review** does nothing, or the browser says it cannot open the link | DJV is not registered for review links on this PC. Start DJV once, or click **Use this DJV** in its Sync Server settings. |
| DJV says *the review link is for the sync server …, but this DJV uses …* | The portal was opened at another address than DJV's server setting. Open it at the address in DJV's Sync Server settings. |
| DJV says the session *is not one you were asked to* | You are not one of the session's people. Ask whoever made it to add you. |
| DJV says the session *was deleted on the server* | Someone deleted it, or took you out of it. Leave it in the Review tool. |
| The session opens, but not the video | The media drive is not mapped yet, or the file was moved. See the **Drive** line in the Sync Server settings. |
| An upload stays at *retrying* | The server cannot be reached. It carries on when the server is back; **Pause** it meanwhile if you like. |
| *There is already a file …* | Give the file another name, or ask `lldjv` to delete the old one. |
| *The media folder has … GB free* | The share's drive is full. Ask whoever runs the dev server. |
| **Videos** says *the server has no media folder* | The server was started without `DJV_MEDIA_DIR`. See [docker-deploy.md](docker-deploy.md). |
