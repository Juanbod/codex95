# Codex95

**A lightweight coding-agent client for Windows 95, powered through a modern
Windows, macOS, or Linux bridge computer.**

[Русская инструкция](README_RU.md) · [Security](SECURITY.md) ·
[Update design](UPDATES.md)

> [!WARNING]
> Codex95 is experimental software. It can execute commands and modify or
> permanently delete files on the Windows 95 computer. Keep backups and use it
> only on a trusted private network.

Codex95 gives a Windows 95 computer a simple Codex-like interface. Describe a
program or change in ordinary language, and the client can create files, use
locally installed compilers, run builds, and launch the result on the retro
computer.

The project was created for a Toshiba Libretto 70CT with 24 MB RAM. It is being
developed with substantial AI assistance using an exploratory "vibe coding"
workflow and remains under active development.

## Start Here

Codex95 always uses **two computers**:

| Computer | Runs | Purpose |
| --- | --- | --- |
| Windows 95 PC | `CODEX95W.EXE` | Shows the chat and performs file/build actions |
| Modern Windows, macOS, or Linux computer | Node.js bridge | Securely connects to the OpenAI API |

The API key stays on the modern computer. The two computers communicate over
your trusted local network.

```text
Windows 95 computer  <-- local HTTP -->  Modern bridge  <-- HTTPS -->  OpenAI
```

macOS support refers to the **modern bridge side**. The lightweight client
itself is built for Windows 95.

## Five-Minute Setup

### 1. Start The Bridge

Install a current [Node.js](https://nodejs.org) release on the modern computer,
download this repository, then double-click the launcher in the repository
root:

| Modern computer | Normal launch | Free test without API |
| --- | --- | --- |
| Windows | `START_CODEX95_WINDOWS.cmd` | `START_MOCK_WINDOWS.cmd` |
| macOS | `START_CODEX95_MAC.command` | `START_MOCK_MAC.command` |
| Linux | `node bridge/server.mjs` | `CODEX95_MOCK=1 node bridge/server.mjs` |

The normal launcher opens:

```text
http://127.0.0.1:8787/setup
```

Enter the OpenAI API key on that page. It is held only in bridge memory and is
forgotten when the bridge stops.

Keep the bridge window open while using Codex95. If the operating-system
firewall asks, allow Node.js on private networks only.

### 2. Prepare The Windows 95 Computer

Copy these two files to the same folder on the Windows 95 computer:

```text
build\CODEX95W.EXE
client\CODEX95.INI.example
```

Rename `CODEX95.INI.example` to `CODEX95.INI`, then open `CODEX95W.EXE`.

The default `Host=auto` setting normally discovers the bridge automatically.
If discovery fails, open **Options > Settings** and enter the bridge computer's
local address, such as `192.168.1.50:8787`.

### 3. Build Something

1. Create or select a project in the left sidebar.
2. Enter a task such as `Create a small window with a button that plays a sound`.
3. Click **Build it**.

Start with project-only access. Enable full-computer access only for tasks that
genuinely need it.

## What It Can Do

- accept natural-language coding tasks in a native Windows 95 GUI;
- create, read, edit, and delete project files;
- create, rename, switch, and delete projects;
- discover installed compilers and build tools;
- run build commands and launch completed programs on Windows 95;
- report the target computer's Windows version, CPU, RAM, screen, codepages,
  disk space, and available compilers to the model;
- keep separate saved chats and model context for each project;
- select the OpenAI model from the client;
- use a lightweight dark interface;
- change the client text size for small retro screens;
- update the Windows 95 GUI client from the bridge over Ethernet;
- optionally allow full access to the Windows 95 computer.

## What It Does Not Do

- It does not run modern OpenAI HTTPS directly from Windows 95.
- It does not provide a compiler that is not installed on the Windows 95
  computer.
- It is not a sandbox. Automatic actions can modify real files.
- It is not ready to be exposed directly to the internet.
- Bridge self-updates and signed release manifests are not implemented yet.

## Bridge Launch Options

The root launchers are the easiest entry point. More specific launchers live in
the `bridge` folder:

| File | Purpose |
| --- | --- |
| `bridge/START_CODEX95.CMD` | Windows bridge plus browser-based key setup |
| `bridge/START_SECURE_REAL.ps1` | Windows bridge with hidden API-key input |
| `bridge/START_MOCK.CMD` | Windows offline mock mode |
| `bridge/START_CODEX95.command` | macOS bridge plus browser-based key setup |
| `bridge/START_SECURE_REAL.command` | macOS bridge with hidden API-key input |
| `bridge/START_MOCK.command` | macOS offline mock mode |

On macOS, if Finder refuses to open a downloaded `.command` file, run this once
from Terminal:

```bash
chmod +x *.command bridge/*.command
```

To find the Mac's Wi-Fi address for manual client configuration:

```bash
ipconfig getifaddr en0
```

## Client Settings

Open **Options > Settings** in `CODEX95W.EXE`:

| Setting | Meaning |
| --- | --- |
| Bridge address | `auto`, a hostname, or an address such as `192.168.1.50:8787` |
| Device name | Friendly target-computer name sent with its hardware profile |
| Model | OpenAI API model used for new tasks |
| Run actions automatically | Allows proposed file operations and commands without approval |
| Full computer access | Allows absolute paths and work outside the selected project |
| Dark interface | Enables the lightweight dark theme |
| Text size | Scales the main client UI text |

Visible chat logs are stored in the `CHATS` folder beside `CODEX95W.EXE`.
Bridge-side conversation state is stored locally in the ignored
`bridge/.codex95-state.json` file. Neither history file contains the API key.

## Updating The Windows 95 Client

The bridge can serve the current prebuilt client from `build\CODEX95W.EXE`.
This is meant for trusted local Ethernet use while developing and testing.

1. Rebuild or update `build\CODEX95W.EXE` on the modern bridge computer.
2. Start the bridge normally and keep it open.
3. On the Windows 95 computer, open `CODEX95W.EXE`.
4. Choose **Options > Update Codex95...**.
5. Confirm the prompt. The client downloads `CODEX95W.NEW`, starts
   `APPLYUPD.BAT`, closes, replaces `CODEX95W.EXE`, and starts again.

The updater preserves `CODEX95.INI`, chats, projects, and bridge state. It only
replaces the GUI executable. If the old client cannot reach the bridge, copy the
new `build\CODEX95W.EXE` manually as a recovery path.

## Network And Security

The bridge listens on TCP port `8787` and advertises itself on UDP port `8788`.
Plain HTTP is used only between the Windows 95 client and bridge because
Windows 95 cannot handle modern TLS. The bridge uses modern HTTPS for OpenAI.

Use Codex95 only on a trusted private LAN:

- never forward bridge ports to the internet;
- keep backups of the Windows 95 disk;
- prefer project-only access;
- review [SECURITY.md](SECURITY.md) before enabling automatic full access.

## Build The Windows 95 Client

Prebuilt experimental clients are included in `build`.

Visual C++ 6.0 with `/MT` is preferred for real Windows 95 hardware because it
avoids depending on a separately installed modern `MSVCRT.DLL`:

```bat
cl /O1 /G5 /GF /W3 /MT client\codex95.c wsock32.lib gdi32.lib /Fe:CODEX95.EXE /link /OPT:REF
cl /O1 /G5 /GF /W3 /MT client\codex95_gui.c wsock32.lib shell32.lib gdi32.lib /Fe:CODEX95W.EXE /link /subsystem:windows /OPT:REF
```

With 32-bit MinGW:

```bat
cd client
set CC=C:\path\to\mingw32\bin\gcc.exe
BUILD_MINGW.BAT
```

The MinGW build imports `MSVCRT.DLL`; the Visual C++ 6 `/MT` build is the
preferred final Windows 95 release build.

## Test And Develop

Test the bridge protocol without an API key:

```text
node bridge/test.mjs
```

Run the GUI protocol smoke test only with a mock bridge:

```text
CODEX95W.EXE /smoke
```

Project layout:

| Path | Contents |
| --- | --- |
| `client/codex95_gui.c` | Native Windows 95 GUI client |
| `client/codex95.c` | Console fallback client |
| `bridge/server.mjs` | Dependency-free modern bridge |
| `bridge/test.mjs` | Bridge protocol smoke test |
| `build/` | Prebuilt experimental Windows 95 clients |

## Troubleshooting

- **Cannot find bridge automatically:** enter the bridge computer's local IP
  manually and allow UDP `8788`.
- **Cannot connect to bridge:** allow inbound TCP `8787` on the private network.
- **Bridge says API key is missing:** open `http://127.0.0.1:8787/setup` on the
  modern computer and enter it again.
- **Quota exceeded:** OpenAI API billing is separate from a ChatGPT
  subscription.
- **Client fails because of MSVCRT:** use a Visual C++ 6 `/MT` build.
- **Build command fails:** install a Windows 95-compatible compiler on the
  target computer.
- **Japanese Windows 95 text problems:** prefer ASCII filenames and deliberately
  handle the target codepage for non-ASCII files.
- **Russian or Japanese chat text looks broken:** Codex95 converts bridge UTF-8
  messages to the Windows 95 system ANSI codepage before showing them. Japanese
  text needs a Japanese/CP932 system or matching fonts. Russian text needs a
  Russian/CP1251 system or matching fonts. A single ANSI Windows 95 install
  usually cannot display both scripts perfectly at the same time.

## Current Limits

- Text files and action results are limited to roughly 30 KB per operation.
- `run_command` executes through `COMMAND.COM`.
- Commands are not sandboxed on Windows 95.
- Service messages use plain ASCII for compatibility with Windows 95.
- Chat prompts, replies, and command results are converted between UTF-8 and
  the local Windows ANSI codepage when the operating system supports it.

Codex95 is licensed under the [MIT License](LICENSE).
