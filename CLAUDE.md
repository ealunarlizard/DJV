# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

DJV is a C++20 media player for professional image review. It is built on two
libraries that come in through the `deps/tlRender` git submodule:
**tlRender** (timeline, I/O, rendering; `tl::`, `<tlRender/...>`) and
**feather-tk** (core, UI toolkit, settings, observables; `ftk::`, `<ftk/...>`),
which lives at `deps/tlRender/deps/ftk`.

## Building

The super build scripts build feather-tk's dependencies, then tlRender's, then
DJV. They take the **source directory as their first argument** and put every
directory **in the current working directory**, so run them from the parent of
the checkout:

```
sh <src>/sbuild-linux.sh <src> [Debug|Release] [config]
sh <src>/sbuild-macos.sh <src> [Debug|Release] [config]
<src>\sbuild-win.bat <src> [Debug|Release] [config]
```

This produces `ftk-<Type>/`, `tl-<Type>/`, `build-<Type>/` (DJV itself) and
`install-<Type>/` beside the checkout, and ends with an install. The
dependencies are built once. After that, rebuild DJV alone:

```
cmake --build build-Debug --config Debug
cmake --build build-Debug --config Debug --target install
```

- `config` names a file in `etc/Config/*.cmake`, passed to every stage with
  `cmake -C`. `default.cmake` is the base; `ci-*.cmake` and `package-*.cmake`
  layer on top of it. Personal overrides go in the untracked
  `etc/Config/local.cmake`, which is read first and wins.
- `DJV_PYTHON` turns on `TLRENDER_PYTHON` and `ftk_PYTHON` as well, so setting
  it alone is enough.
- The scripts initialise the submodule only when it is missing, and never reset
  it. To work against a local tlRender change, edit `deps/tlRender` directly.
- `DJV_TLRENDER_PACKAGE=ON` uses an installed tlRender instead of the submodule.
  `etc/Linux/build-system.sh` builds against the distribution's packages (see
  `docs/building.html`).
- The version comes from `lib/djv/Models/Version.h`, and CMake parses it with
  regexes. Keep the macro spelling intact.
- The Python wheel (`pyproject.toml`, scikit-build-core) pins exact `tlRender`,
  `feather-tk` and `nanobind` versions. Change them together.

## Tests

Tests only build with `DJV_TESTS=ON`. The `ci-*` configs turn it on, or you can
set it in `local.cmake`.

```
cd build-Debug
ctest -C Debug --output-on-failure          # all (djv-test, plus djv-python-test when DJV_PYTHON)
ctest -C Debug -R djv-test                  # C++ only
```

Running `djv-test` directly takes test names as positional arguments. Each one
is a case-insensitive substring and runs **every** test whose name contains it,
for example `djv-test SettingsModel` or `djv-test -log Playlist`.

- C++ tests live in `lib/djv/ModelsTest/` (the `djvModelsTest` library) and are
  registered by hand in `tests/djv-test/djv-test.cpp`. A new test class needs
  an entry there. Use `ModelsTestUtil.h`'s `createTestSettings()` for in-memory
  settings, so a test never touches the user's real settings.
- Python tests are in `tests/djvPyTest/` (unittest) and run through
  `tests/run-python-tests.py`, which registers the DLL directories that Windows
  needs. Tests that need OpenGL skip themselves when `DJV_TESTS_NO_GL` is set.
- On headless Linux, CI runs the tests under `xvfb-run`.
- The sync server (`server/`, Node.js, no dependencies) has its own tests:
  `cd server && npm test`. `SyncModelTest` also has a live part that runs
  only when `DJV_SYNC_TEST_SERVER` (for example `http://127.0.0.1:8787`)
  points at a running server.
- On Windows, a `djv.exe` run from `build-<Type>` (including the
  `djvDocsHelpCheck` target) needs `install-<Type>in` on `PATH` for the
  FFmpeg DLLs.

## Documentation checks

`docs/` is the user manual: hand-written HTML, published as is. CI runs
`cmake --build build-Debug --target djvDocsHelpCheck`, which fails if
`docs/command-line.html` no longer matches `djv -help`. After you add or change
a command-line option, regenerate the page with the `djvDocsHelp` target. If
you add or rename a page, update `docs/nav.txt` and run `djvDocsNav`, which
rewrites the navigation and the table of contents in every page. Don't edit
those blocks by hand; `djvDocsNavCheck` reports any that were.

User-visible changes go in `ChangeLog.md` under the current version, with
"Changes:" and "Fixes:" lists. The docs build generates the changelog page
from that file.

## Architecture

The libraries under `lib/djv/` depend on each other strictly in one direction.
Each one has its own `Export.h` and `DJV_<LIB>_API` macros, because they can be
built shared (on Windows, a symbol missing its macro is a link error):

- **Models** (`djv::models`, `djvModels`): the application state, free of UI.
  It holds settings, files, playlist, color/OCIO, viewport, audio, tools,
  markers, annotations, drawing, review documents, the commands registry, and
  the time units. State is exposed through `ftk::IObservable*` types that the
  UI subscribes to. Persistence is in `SettingsModel` (on top of
  `ftk::Settings`).
- **UI** (`djv::ui`, `djvUI`): reusable widgets and dialogs that depend only
  on Models, such as `Viewport`, the color, view and export widgets, and the
  popups.
- **App** (`djv::app`, `djvApp`): the application itself. `App` (an
  `ftk::App`) owns every model and opens files into tlRender `Player`s.
  `MainWindow` assembles the menus, tool bars, tab bar, status bar and the tool
  dock.
- **Resource**: icons and other embedded resources.
- `bin/djv/main.cpp` is a thin `main` that creates the context and an
  `app::App`.

### Actions, commands, shortcuts

Each UI area follows the same pattern: `XxxActions` → `XxxMenu` /
`XxxToolBar`. An actions class derives from `IActions` and registers:

- **commands** (`_addCommand` / `_addCheckCommand`), named `"<Group>/<Name>"`,
  in `CommandsModel`. A command receives `nlohmann::json` arguments. Menu
  items, shortcuts and automation (`-command` / `-listCommands` on the command
  line, screenshot capture) all run through this registry. New functionality
  should be a command, with the action calling it (`_command(name)`), rather
  than a direct callback.
- **shortcuts** (`_addShortcut`): default key bindings, stored in
  `SettingsModel` and overridden by the user's saved bindings.

### Tools

The dock panels (Files, Color, View, Info, Export, Review, ...) are
`IToolWidget` subclasses. `ToolWidgetFactory` creates them, and `ToolsModel`
tracks the registered tools and which ones are visible.

### Review sync

The review markers (`MarkersModel`) can be shared live through
`server/server.js`:

- `models::SyncModel` runs two threads that use the vendored single-header
  `deps/cpp-httplib/httplib.h`, included only in `SyncModel.cpp`. One thread
  reads the session snapshot and then follows the server-sent event stream.
  The other sends PUT and DELETE requests in order, retrying until the server
  accepts them. Incoming data reaches the main thread through an
  `ftk::Timer`.
- `models::SyncMerger` holds the merge logic without any networking.
  - It diffs the model against what the server was last told, and pushes the
    difference.
  - The server's ordering decides conflicts: the last write it receives wins.
  - Remote changes to a marker that still has a local change in flight are
    skipped.
- The server keeps every session in one SQLite file (`node:sqlite`, Node
  22.13 or newer, still no npm dependencies). Each change is written in a
  transaction before it is answered, and the last 1000 events are kept so
  that clients can catch up across a restart. Old `<session>.json` files are
  imported once at startup.
- A session also holds its media (`SyncMedia`). The path is stored relative
  to the client's media root, with the absolute path as a fallback; a path
  on a mapped drive is stored as its UNC form (`toUncPath`). `SyncMerger`
  gives a session that has no media the client's current file when the
  snapshot arrives. `App::_syncMediaUpdate` opens the session's media only
  when no file is open.
- Studio LAN (`docs/lan-setup.html`; the requests and history are in
  `docs/lan/CHANGE_REQUESTS.md` and `docs/lan/CHANGELOG.md`, so add to
  both when this area changes):
  - People sign in: `POST /api/login` gives a session token, and every
    other `/api/` request needs it as `Authorization: Bearer`, except
    `health` and `hello`. Passwords are hashed with scrypt
    (`server/users.js`), and only the tokens' SHA-256 is stored, in the
    database's `tokens` table.
  - `lldjv` is the only admin. Its password comes from
    `ROOT_PASSWORD_FILE` at every start, and the server refuses to start
    without one or an existing `lldjv`. Tests use `server/test/helpers.js`,
    whose `start()` signs in as `lldjv` and whose `fetch` sends the token.
  - `server/config.js` handles `config.json`, which the admin page
    (`server/admin.html`, `/api/admin/*`) also writes. It holds the users,
    the media share and its account (`storage.unc`, `storage.user`), and
    the expected DJV version. The share's password comes from
    `SHARE_PASSWORD_FILE`, is never written to the file, and is given in
    `hello` to signed-in users only.
  - Docker: `server/Dockerfile`, `server/deploy/` (compose, PowerShell
    scripts) and `server/build-package.ps1`. On a Windows host the data
    folder is a bind mount, so the compose file sets `DB_EXCLUSIVE=1` (WAL
    with exclusive locking, no `-shm`). The secrets live in
    `server/deploy/secrets/`, which is gitignored and must stay that way.
  - Every request DJV makes carries `X-DJV-Client` (the PC's name, for the
    admin page only) and `X-DJV-Version`. The per-connection `clientId` in
    change bodies is separate, and still drives echo detection: two DJVs
    can share a user.
  - The settings are `SyncSettings` (`/Sync.1`): server (by default
    `http://192.168.1.40:8877`), user, client ID, drive letter, cached
    share, media folder. The password is in the Windows Credential Manager
    (`Credentials.cpp`, advapi32), never in the settings.
    `readSyncSettings()` moves the old `SyncServer`/`SyncMediaRoot` keys out
    of the Review section. `SettingsModel` needs UI systems in the context,
    so tests use the free functions and `LanModel`'s observable constructor
    instead.
  - `models::LanModel` runs the sign-in and hello (`syncLogin`,
    `fetchSyncServerInfo`, in `SyncModel.cpp` with httplib) and the drive
    mapping (`NetworkDrive.cpp`, `WNetAddConnection2` with the share's
    account, linked with `mpr`) on detached threads. It publishes the auth
    state with the session token, and the effective media root: the mapped
    drive, or else `SyncSettings::mediaRoot`. `App` connects with the token,
    connects again when it changes, and signs in again once when the server
    refuses a token.
  - `ui::PasswordEdit` is the masked field: ftk's `LineEdit` has no
    password mode, and keeps undo history and copy.
  - The review portal (`server/portal.html`, served at `/`) uses the same
    sign-in. Videos come from `MEDIA_DIR` (`server/media.js`, which keeps
    paths valid on Windows), and are uploaded in pieces through
    `/api/uploads` (the `uploads` table). Sessions have a `session_info`
    row (schema 3): description, date, creator, members. Only those, and
    `lldjv`, may see or join one (403 `not-member`); a name with no row is
    claimed by its first user, and one from before 2.1 is open to all.
    Deleting a session sends `removed` on its streams, and DJV stops.
  - `SyncMedia::compare` is a session's second video, opened as B by
    `App::loadSyncMedia`.
  - Review links, `djv://session/<s>?server=<url>`: `models::DeepLink`
    parses them, registers DJV for them in HKCU (the installer in HKLM),
    and passes them between DJVs on a named pipe for the Windows user
    (`bin/djv/main.cpp` hands a link on and exits). `App::openDeepLink`
    waits for the sign-in, then follows a link for another address only
    when that address's `hello` gives the same `serverId` (server 2.1.1,
    its `meta` table), and always joins on the settings' server. It closes
    the review (`confirmClose`) before joining, so that its markers do not
    go into the session; `autoConnect=false` only fills the Review tool in
    (`observeSyncProposal`). Every step is logged as
    `djv::app::ReviewLink`, and the connection as `djv::app::Sync`.
    `ftk::Format` cannot fill a placeholder used twice: pass the value
    again under a new number.
- The `.djvr` `sync` section (`ReviewSync`) stores the server and session.
  `App` must disconnect before replacing or clearing the markers (see
  `_applyReview` and `_closeReview`). Otherwise the server would receive
  deletions for every marker.
- `ui::Viewport::setMarkersModel()` draws the notes over the video and fades
  them out during playback. The `ReviewSettings` settings control whether
  they are shown and for how long.

### Python bindings

When `DJV_PYTHON` is on, `ModelsPy/` and `UIPy/` bind Models and UI with
nanobind, and `djvPy/` compiles them into the `_djv` module of the `djv`
package (`__main__.py` runs the application). The module shares types with
`feather_tk` and `tlrender`, so all three must be built with the same nanobind
and the same stable-ABI setting (`ftk_PYTHON_STABLE_ABI`). When you add a
public model or widget API, bind it in the matching `*Py/` file.
`examples/python/` is a full reimplementation of the application in Python on
top of these bindings, so it shows how to use the API.

## Conventions

- Every source file starts with the `SPDX-License-Identifier: BSD-3-Clause`
  and `Copyright Contributors to the DJV project.` header.
- Classes follow the ftk pattern: a protected `_init()` and constructor, a
  public static `create()` that returns `std::shared_ptr`, `FTK_NON_COPYABLE`,
  and private state in a `Private` struct accessed through `FTK_P()`.
- Comments explain *why* in full prose sentences, often at length, and so do
  CMake comments. Match that style instead of writing terse what-comments.
  Headers document their API with `//!`.
- Commits that only move the submodule pointer are titled "Bump tlRender" or
  "Update tlRender".
