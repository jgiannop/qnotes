# QNotes

A sticky-notes desktop app for Linux, modelled on Windows Sticky Notes. Built with Qt 6 Widgets and C++17.

## Features

- **Text notes** with rich text: bold, italic, underline, strikethrough and bullet lists.
- **Key-value notes** made of `key = value` fields, handy for things like account details.
- **Secret values**: a field can be marked secret. It's shown as dots, can't be edited or copied while locked, and
  needs your system password to reveal, copy or un-mark.
  - After you authenticate, there's a 60-second grace period, like `sudo`.
  - Revealed values hide again after 30 seconds.
  - Copied secrets are cleared from the clipboard after 30 seconds.
- **Notes list** with search. Secret values are masked in the list and in search results.
- 7 note colours, frameless windows you can move and resize, and autosave.

> **Note:** secrets are only hidden in the UI. They are stored **in plaintext** in `notes.json`. The plan is to move
> them to the GNOME Keyring (libsecret).

## Requirements

- Qt 6 (tested with 6.4)
- CMake ≥ 3.16 and a C++17 compiler
- `libpam0g-dev` (optional). Without it, secret values can't be revealed.

On Ubuntu/Debian:

```bash
sudo apt install build-essential cmake qt6-base-dev libpam0g-dev
```

## Build & run

```bash
cmake -S . -B build && cmake --build build -j
./build/qnotes
```

## Keyboard shortcuts

| Shortcut         | Action                  |
|------------------|-------------------------|
| Ctrl+N           | New note                |
| Ctrl+Shift+K     | New key-value note      |
| Ctrl+D           | Delete note             |
| Ctrl+B / I / U   | Bold / italic / underline |
| Ctrl+T           | Strikethrough           |
| Ctrl+Shift+L     | Bullet list             |
| Ctrl+Enter       | Add field (key-value note) |

## Data

Notes are saved to `~/.local/share/qnotes/notes.json`. Changes are saved automatically 500 ms after each edit, and the
file is written atomically.
