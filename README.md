# Codex95

> [!WARNING]
> **Early development / experimental software.** Codex95 can execute commands
> and modify or delete files on a Windows 95 computer. Keep backups and use it
> only on a trusted private network.

> [!NOTE]
> This project is being developed with substantial AI assistance using an
> exploratory "vibe coding" workflow. The code has been built and smoke-tested,
> but it has not yet received a complete independent security audit or broad
> testing on real Windows 95 hardware.

Codex95 is a thin coding-agent client for Windows 95. The Libretto runs a small
Win32/Winsock client; a modern computer on the LAN handles the OpenAI API.

The project is currently tailored for a Toshiba Libretto 70CT with 24 MB RAM,
but its client is intended to remain useful on other Windows 95 computers.

The current client can:

- accept natural-language tasks in a native Windows 95 GUI;
- list and read project files;
- create directories and write files automatically inside the selected project;
- switch, create, and permanently delete projects from the GUI;
- optionally access absolute paths across the target computer;
- discover installed compilers and build tools;
- run Windows 95 build commands with a two-minute timeout;
- launch completed GUI programs without blocking Codex95;
- use a persistent dark interface;
- remember follow-up requests for each project while the bridge is running;
- keep the OpenAI API key off the retro computer.

## Layout

- `client/codex95.c`: dependency-free Windows 95 console client.
- `client/codex95_gui.c`: native Windows 95 GUI client.
- `bridge/server.mjs`: dependency-free bridge for a modern Node.js computer.
- `bridge/test.mjs`: protocol smoke test using mock mode.

## Run The Bridge

Install a current Node.js release on the modern computer, then:

```powershell
$env:OPENAI_API_KEY="..."
$env:OPENAI_MODEL="gpt-5.4-mini"
node bridge/server.mjs
```

You can also double-click `bridge\START_CODEX95.CMD` after setting the API key.

For an offline protocol test without an API key:

```powershell
$env:CODEX95_MOCK="1"
node bridge/server.mjs
```

Allow inbound TCP port `8787` only from the trusted local network. Do not expose
the bridge or the Windows 95 computer directly to the internet.

## Build The Windows 95 Client

With Visual C++ 6.0:

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

The MinGW build produces:

- `build\CODEX95.EXE`: console fallback.
- `build\CODEX95W.EXE`: native GUI client.

Run `CODEX95W.EXE /smoke` only for an automated GUI protocol test.

The MinGW binaries import `MSVCRT.DLL`. If the Libretto does not already have a
compatible version, use the Visual C++ 6 `/MT` build instead; that is the
preferred final Windows 95 release build.

Copy `CODEX95W.EXE` to the Libretto and open it. On the first run:

1. Leave the bridge address as `auto`, or enter a local hostname such as
   `bridge-pc.local:8787` if LAN discovery is blocked.
2. Select a project folder.
3. Describe the program you want and click **Build it**.

The left sidebar lists sibling project folders next to the active project.
Select a project to switch instantly, click **New** to create the next available
`PROJECT01`, `PROJECT02`, and so on, or click **Refresh** after changing folders
outside Codex95. **Delete project** permanently removes the selected project
and all files inside it after two confirmations.

The GUI remembers its settings in `CODEX95.INI`. Automatic actions are enabled
by default. Every file operation remains restricted to the selected project
folder. Disable **Automatic actions** to confirm writes and commands manually.
Automatic bridge discovery uses UDP port `8788`; project traffic uses TCP port
`8787`.

Connection settings, the device name, automatic action approval, and access
mode are available under **Options > Settings**. The optional **Full computer
access** mode permits absolute paths and lets Codex run commands or modify and
delete files outside the selected project. Keep project-only access enabled
unless a task genuinely needs the rest of the Libretto.

The same settings window includes an optional dark interface designed for the
classic Windows 95 controls used by Codex95.

At the start of every task, the client sends an automatically collected target
device profile to the bridge. It includes the Windows version, CPU type, RAM,
screen size and color depth, codepages, project disk space, and detected build
tools. Codex treats these as real design constraints. The friendly hardware
name defaults to `Toshiba Libretto 70CT` and can be changed with `DeviceName`
in `CODEX95.INI`.

The model is also explicitly told that it is connected through a bridge on a
separate modern computer, while every provided file and command tool executes
on the Libretto. It therefore must not assume that software installed on the
bridge is also available on the Windows 95 machine.

The console client remains available as a fallback:

```bat
CODEX95.EXE bridge-pc.local 8787 C:\DEV\CLOCK -y
```

## Current Limits

- Text files and action results are currently limited to roughly 30 KB per operation.
- The bridge uses plain HTTP because Windows 95 cannot handle modern TLS.
- File access is restricted to the selected project root.
- `run_command` executes through `COMMAND.COM`.
- Automatic mode can execute commands proposed by the model with the project
  directory as the working directory. Commands are not sandboxed on Windows 95,
  so use the bridge only on a trusted private network.
- Service messages use plain ASCII so they remain readable on Japanese Windows
  95. Non-ASCII project files still require deliberate codepage handling.
