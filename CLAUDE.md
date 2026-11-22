# QNotes

A Qt 6 (Widgets, C++17) desktop clone of Windows Sticky Notes, developed on Ubuntu/GNOME (X11).
Two note kinds: **Text** (rich text) and **KeyValue** (key = value fields, values can be secret).

## Build & run

```bash
cmake -S . -B build && cmake --build build -j
./build/qnotes
```

- Requires Qt 6 (6.4 on this machine) and `libpam0g-dev` (for `HAVE_PAM`; without it secrets can't be revealed).
- `build/` caches absolute paths: delete it if the project folder is moved/renamed.
- Restarting the app to test: stop the running instance first. Notes autosave 500 ms after each change, so killing it loses nothing.

## Files

- `main.cpp` — app setup (name `qnotes`, display name `QNotes`), draws the app icon.
- `notemanager.*` — owns all notes and the list window; JSON load/save; creating/deleting notes.
- `notewindow.*` — one frameless note window: title bar (+ / ⋯ / ✕), colour menu, per-kind content + bottom toolbar.
- `keyvalueeditor.*` — KeyValue note content: rows of key | value | 👁 ⧉ 🔒 ✕ (`KeyValueRow` is defined in the .cpp).
- `systemauth.*` — password dialog; verifies the user's system password via PAM service `sudo`.
- `noteslistwindow.*` — "QNotes" list window: search + painted note cards (`NoteCardDelegate`).
- `notecolors.h` — the 7 Windows Sticky Notes colours (title/body/text).

## Data

- Saved to `~/.local/share/qnotes/notes.json` (`QStandardPaths::AppDataLocation`), written atomically with `QSaveFile`.
- Each note: `id, type ("text"|"keyvalue"), color, x, y, w, h, open, modified` plus `html` (text) or `fields: [{key, value, secret}]`.
- Legacy `"type": "table"` notes (a removed spreadsheet feature) are converted to text notes on load (`legacyTableToHtml` in notemanager.cpp).

## Design decisions / gotchas

- **Secrets are only hidden in the UI.** Values are stored in plaintext in notes.json. Moving secrets to the GNOME Keyring
  (libsecret, needs `libsecret-1-dev`) is the planned fix. Don't claim secrets are encrypted.
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
