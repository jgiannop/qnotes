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
- **Notes list** with search, as a list or a grid. Secret values are masked in the list and in search results.
  Right-click a key-value note → **Copy value** to copy a field without opening the note (secrets ask for your
  password first and are cleared from the clipboard after 30 s).
- Key-value notes have an optional title, shown as the card title in the notes list.
- **Tray icon:** left-click shows or hides the notes list; right-click opens a menu with New note, Preferences
  and Quit.
  QNotes keeps running in the tray when all windows are closed. Only one copy runs at a time: starting it again
  brings up the notes list.
- 7 note colours, frameless windows you can move and resize, and autosave.
- **Preferences** (⚙ in the notes list, or the tray menu):
  - Start QNotes when you log in.
  - Light or dark notes list, or the same as the system (default).
  - **Backup:** export all notes, including secret values, to one `.qnotes` file encrypted with a password you
    choose (XSalsa20-Poly1305, key derived with PBKDF2-HMAC-SHA256). Importing adds the notes that aren't there
    yet; secrets go into the keychain. A wrong password or a modified file is detected.
- The notes list remembers its size and position.

- Secret values are stored in the **system keychain**: GNOME Keyring or KWallet on Linux, Credential Manager on
  Windows. `notes.json` only keeps a reference to them. Secrets from older versions are moved there automatically.
  Without a keychain (e.g. a bare window manager without one), they are saved in `notes.json` unencrypted, and
  QNotes tells you so once.

## Install

Download the latest package from the [Releases](https://github.com/jgiannop/qnotes/releases) page.

- **Ubuntu / Debian** (Ubuntu 24.04+, Debian 13+): `sudo apt install ./qnotes_<version>_amd64.deb`
- **Windows 10 / 11** (64-bit): run `qnotes-<version>-windows-x64.exe` (installer), or unzip the `.zip` and run `qnotes.exe`.
  To reveal secrets, enter your Windows account password. For a Microsoft account, that's the online account
  password, not your PIN.

## Building from source

### Requirements

- Qt 6 (tested with 6.4)
- QtKeychain for Qt 6 (`qtkeychain-qt6-dev`). If it isn't installed, CMake downloads and builds it.
- CMake ≥ 3.16 and a C++17 compiler
- `libpam0g-dev` (optional). Without it, secret values can't be revealed.

On Ubuntu/Debian:

```bash
sudo apt install build-essential cmake qt6-base-dev qtkeychain-qt6-dev libpam0g-dev libxcb1-dev
```

### Build & run

```bash
cmake -S . -B build && cmake --build build -j
./build/qnotes
```

### Packaging

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
cd build && cpack        # Linux: qnotes_<version>_amd64.deb; Windows: .zip + NSIS installer
```

### Releasing

1. Bump `VERSION` in the `project()` line of `CMakeLists.txt` and commit.
2. Tag and push: `git tag v1.2.3 && git push origin main v1.2.3`.

The [Release workflow](.github/workflows/release.yml) then builds the `.deb` and the Windows packages, and publishes
them as a GitHub Release. It fails if the tag doesn't match the CMake version.

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
