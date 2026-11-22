# QNotes

A Qt 6 (Widgets, C++17) desktop clone of Windows Sticky Notes, developed on Ubuntu/GNOME (X11).
Two note kinds: **Text** (rich text) and **KeyValue** (key = value fields, values can be secret).

## Build & run

```bash
cmake -S . -B build && cmake --build build -j
./build/qnotes
```

- Requires Qt 6 (6.4 on this machine), `qtkeychain-qt6-dev` (else CMake FetchContent builds QtKeychain 0.15 statically
  — that's what Windows CI does; on Linux that path would need `libsecret-1-dev`), `libpam0g-dev` (for `HAVE_PAM`;
  without it secrets can't be revealed) and `libxcb1-dev`.
- `build/` caches absolute paths: delete it if the project folder is moved/renamed.
- Restarting the app to test: stop the running instance first. Notes autosave 500 ms after each change, so killing it loses nothing.

## Files

- `main.cpp` — app setup (name `qnotes`, display name `QNotes`, desktop file name `qnotes`), loads the app icon from resources.
- `notemanager.*` — owns all notes, the list window and the tray icon; JSON load/save; creating/deleting notes.
- `singleinstance.*` — one QNotes per user via `QLocalServer` (`$XDG_RUNTIME_DIR/qnotes.sock`, Windows: pipe
  `qnotes-<user>`). A second launch sends `show` to the running one and exits; a stale socket is removed.
- `notewindow.*` — one frameless note window: title bar (+ / ⋯ / ✕), colour menu, per-kind content + bottom toolbar.
- `keyvalueeditor.*` — KeyValue note content: rows of key | value | 👁 ⧉ 🔒 ✕ (`KeyValueRow` is defined in the .cpp).
  Every row has a stable `id` (UUID, saved in the JSON); a secret's keychain entry is stored under it.
- `secretstore.*` — synchronous wrapper around QtKeychain (service `QNotes`, key = field id).
- `preferencesdialog.*` — start on login, theme (System/Light/Dark), Export…/Import… (calls NoteManager).
- `autostart.*` — `~/.config/autostart/qnotes.desktop` (Exec=`qnotes` if that's us on PATH, else the full path) /
  Windows `HKCU\...\Run` value `QNotes`.
- `theme.*` — light/dark preference (`appearance/theme`); system detection: Qt ≥ 6.5 `colorScheme()`, else
  `gsettings get org.gnome.desktop.interface color-scheme` (DBus portal isn't usable, see tray hack), Windows
  registry `AppsUseLightTheme`, else palette lightness. `NotesListWindow::applyTheme()` rebuilds its style sheet;
  it's re-checked on every `showNear()`.
- `backup.*` + `third_party/tweetnacl/` — encrypted backup files: PBKDF2-HMAC-SHA256 (`QPasswordDigestor`, 600k
  iterations, ~2 s → run on a worker thread) + TweetNaCl `crypto_secretbox`. `randombytes()` is implemented in
  backup.cpp with `QRandomGenerator::system()`. Export payload = `{"notes": [noteToJson...]}` incl. secret values.
  Import adds notes whose id doesn't exist yet (closed), then `save()` moves their secrets into the keychain.
- `systemauth.*` — password dialog; verifies the user's system password via PAM service `sudo` (Linux) or `LogonUserW` (Windows).
- `noteslistwindow.*` — "QNotes" list window: pill search + round ✚/🔑 buttons, painted note cards (`NoteCardDelegate`:
  shadow, left colour accent, first line = bold title). Fixed light palette via one style sheet using `#objectName`
  selectors (so the context menu / message boxes don't inherit it); the scrollbar has its own style sheet.
  List or grid view (toggle button, saved in `QSettings("qnotes","qnotes")` key `list/grid`): same `QListWidget`,
  grid = `LeftToRight` flow + wrapping; `updateCardSize()` sets the delegate's `cardSize` from the viewport width.
  A normal window; geometry saved in QSettings `list/geometry` on hide/destroy and restored in `showNear()`
  (first ever show: next to the tray icon).
- `notecolors.h` — the 7 Windows Sticky Notes colours (title/body/text).
- `icons/` — `qnotes.svg` is the master icon; the PNGs (embedded via `qt_add_resources` and installed to hicolor) and
  `qnotes.ico` (Windows exe) are rendered from it. ImageMagick renders this SVG wrongly; use Qt's `QSvgRenderer`.
- `packaging/linux/qnotes.desktop`, `packaging/windows/qnotes.rc.in` (exe icon + version info, configured by CMake).
- `.github/workflows/release.yml` — on a `v*` tag builds the .deb (ubuntu-24.04) and Windows zip + NSIS installer
  (MSVC, Qt 6.8) and publishes a GitHub Release. Tag must equal the `project(... VERSION)`.

## Packaging

- `cd build && cpack` → `.deb` on Linux (deps via `dpkg-shlibdeps` + `qt6-qpa-plugins`, which isn't a library dep but
  is needed to start). On Windows `install()` runs `qt_generate_deploy_app_script` (windeployqt), CPack makes ZIP + NSIS.
- Windows builds can't be done on this machine; they only run in GitHub Actions.

## Data

- Saved to `~/.local/share/qnotes/notes.json` (`QStandardPaths::AppDataLocation`), written atomically with `QSaveFile`.
- Each note: `id, type ("text"|"keyvalue"), color, x, y, w, h, open, modified` plus `html` (text) or
  `fields: [{id, key, value, secret}]`. Secret fields have no `value` when it's in the keychain.
- Legacy `"type": "table"` notes (a removed spreadsheet feature) are converted to text notes on load (`legacyTableToHtml` in notemanager.cpp).

## Design decisions / gotchas

- Settings (not notes) live in `QSettings("qnotes", "qnotes")` → `~/.config/qnotes/qnotes.conf`.
- **Tray:** if `QSystemTrayIcon::isSystemTrayAvailable()`, the list isn't shown at start; left-click on the tray
  shows the list window (first time next to the icon, or at the mouse if the icon position is unknown) or hides it
  if it's the active window; right-click has a menu with Quit. `quitOnLastWindowClosed` is off then, so closing the
  list just hides it. Without a tray the list is shown at start as before.
- **XEmbed tray hack (main.cpp):** Ubuntu's AppIndicator extension opens a DBus/SNI tray item's menu on left click
  and only sends double-clicks to the app. So on X11 with an XEmbed tray (`_NET_SYSTEM_TRAY_S<n>` owned, i.e. the
  extension's legacy tray), `main()` sets `DBUS_SESSION_BUS_ADDRESS=disabled:` *before* `QApplication` (something
  touches the bus during startup, so later is too late) and restores it after the tray exists. Qt then uses XEmbed,
  which gets real clicks. Cost: no session bus in-process (no AT-SPI accessibility). Check with
  `gdbus ... StatusNotifierWatcher RegisteredStatusNotifierItems`: qnotes must not be listed. Needs `libxcb1-dev`.

- **Secrets live in the system keychain** (NoteManager `loadSecrets`/`storeSecrets`): on save, changed secret values
  are written to the keychain and stripped from the JSON; entries no longer referenced are deleted after the JSON was
  written. Plaintext secrets (older versions) are migrated on start. Ids that couldn't be read go to
  `m_unreadableSecrets` and are never overwritten with an empty value. Without a keychain (`QKeychain::isAvailable()`),
  or if a write fails, the value stays in the JSON in plaintext and the user is warned once (QSettings flag).
  The tray DBus hack is GNOME-only: QtKeychain's KWallet backend uses QtDBus; on GNOME libsecret uses its own (GDBus)
  connection made after the env var is restored (verified).
- Secret behaviour: shown as dots, read-only and not copyable while locked. Reveal / copy / un-mark requires system auth.
  Grace period 60 s after successful auth (sudo-like), 3 attempts max, revealed values auto-hide after 30 s and when the
  note is hidden, copied secrets are cleared from the clipboard after 30 s. Notes list/search mask secret values.
- PAM runs on a worker thread (`QThread::create` + local event loop): a wrong password takes ~2 s.
- Note windows are `Qt::FramelessWindowHint`; move/resize use `QWindow::startSystemMove/startSystemResize` (works on
  Wayland too). The 6 px side/bottom layout margins are the resize handles.
- Note styling is one style sheet in `NoteWindow::applyColor()` built with `.arg()` placeholders `%1`–`%10`; keep the
  numbering consistent when editing. Popup menus are created without a parent so they don't inherit it.
- All note buttons use `Qt::NoFocus` so they never steal focus from the editor.
- Windows-like UI: title-bar buttons show only when the note is active/hovered; the bottom toolbar only when active.
- Shortcuts: Ctrl+N new note, Ctrl+Shift+K new key-value note, Ctrl+D delete note, Ctrl+B/I/U/T and Ctrl+Shift+L for text
  formatting, Ctrl+Enter add field.

## Testing

No test suite. To check UI changes without touching the user's real notes, build a small harness in the scratchpad
that links the needed .cpp files, run it with `QT_QPA_PLATFORM=offscreen`, and save `widget->grab()` to PNG. When running
the real app headless, set `XDG_DATA_HOME` to a scratch dir.
