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
- `client/CODEX95.ICO` and `client/CODEX95.RC`: classic 256-color application
  icon and Windows resource file.
- `bridge/server.mjs`: dependency-free bridge for a modern Node.js computer.
- `bridge/test.mjs`: protocol smoke test using mock mode.

## How It Works

Codex95 has two parts:

1. `CODEX95W.EXE` runs on the Windows 95 computer and performs file operations,
   builds, tests, and program launches.
2. `bridge/server.mjs` runs on a modern computer and securely calls the OpenAI
   API over modern HTTPS.

The Windows 95 client talks to the bridge over plain HTTP on the trusted local
network. The OpenAI API key never needs to be stored on the retro computer.

Example network:

```text
Toshiba Libretto             Modern bridge PC             OpenAI API
192.168.1.70  -- LAN -->     192.168.1.50:8787  -- TLS --> Internet
```

The addresses above are examples. Use the actual local addresses assigned by
your router.

## Quick Start

### 1. Prepare The Modern Bridge PC

Install a current Node.js release on the modern computer, then:

```powershell
cd C:\path\to\codex95
$env:OPENAI_API_KEY="..."
$env:OPENAI_MODEL="gpt-5.4-mini"
node bridge/server.mjs
```

`OPENAI_MODEL` is only the fallback for older clients or clients with an empty
model setting. Current Codex95 clients select the model for each task.

The bridge listens on TCP port `8787` and advertises itself using UDP port
`8788`. If Windows Firewall asks for permission, allow Node.js only on private
networks.

The easiest setup is to double-click `bridge\START_CODEX95.CMD`. It starts the
bridge and opens the local setup page:

```text
http://127.0.0.1:8787/setup
```

Enter the API key there. The setup page is available only on the modern bridge
PC. The key stays in bridge memory, is never sent to Windows 95, and is
forgotten when the bridge stops.

To save the API key for future PowerShell sessions:

```powershell
setx OPENAI_API_KEY "your-api-key"
```

Open a new terminal after using `setx`. You can then double-click
`bridge\START_CODEX95.CMD`.

To avoid saving the API key permanently, run:

```powershell
powershell -ExecutionPolicy Bypass -File bridge\START_SECURE_REAL.ps1
```

This asks for the key using hidden input and keeps it only in the bridge
process environment until that window is closed.

### 2. Test The Bridge Without An API Key

For an offline protocol test without an API key:

```powershell
$env:CODEX95_MOCK="1"
node bridge/server.mjs
```

Or double-click `bridge\START_MOCK.CMD`. Mock mode accepts requests but does not
contact OpenAI. It is the safest way to confirm networking and client setup.

### 3. Copy The Client To Windows 95

Copy these files to a folder on the Windows 95 computer using a CF card, SD
adapter, network share, or other removable media:

```text
build\CODEX95W.EXE
client\CODEX95.INI.example
```

Rename `CODEX95.INI.example` to `CODEX95.INI` and place it beside
`CODEX95W.EXE`. The default configuration uses automatic bridge discovery:

```ini
[Codex95]
Host=auto
Port=8787
Project=C:\DEV\CODEX95
Automatic=1
DeviceName=Toshiba Libretto 70CT
Model=gpt-5.4-mini
FullAccess=0
DarkMode=0
```

If discovery does not work, replace `Host=auto` with the bridge PC's local IP:

```ini
Host=192.168.1.50
```

### 4. Start Codex95

1. Start the bridge on the modern computer.
2. Open `CODEX95W.EXE` on Windows 95.
3. Choose or create a project.
4. Enter a task and click **Build it**.

The status area shows `[FULL ACCESS]` when unrestricted access is enabled.
Start with project-only access and keep backups of the Windows 95 disk.

## Settings

Open **Options > Settings** inside the GUI:

- **Bridge address**: `auto`, a hostname, or an address such as
  `192.168.1.50:8787`.
- **Device name**: friendly name included in the hardware profile sent to the
  model.
- **Model**: editable model selector. Use `gpt-5.4-nano` for inexpensive tasks,
  `gpt-5.4-mini` for stronger coding work, or enter another API model name.
- **Run actions automatically**: allows file writes and commands without an
  approval prompt.
- **Full computer access**: permits absolute paths and operations outside the
  selected project.
- **Dark interface**: uses the built-in lightweight dark color scheme.

Each project and model combination keeps separate conversation history. The
API key always remains on the modern bridge computer.

The client automatically reports the target Windows version, CPU, RAM, screen,
codepages, free disk space, active project, and detected compilers. This helps
the model produce software appropriate for the real target computer.

## Build From Source

Prebuilt experimental clients are included in `build`. Building with Visual
C++ 6.0 using `/MT` is preferred for real Windows 95 hardware because it avoids
depending on a separately installed modern `MSVCRT.DLL`.

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

## Using Projects

The left sidebar lists sibling project folders next to the active project.
Select a project to switch instantly, click **New** to create the next available
`PROJECT01`, `PROJECT02`, and so on, or click **Refresh** after changing folders
outside Codex95. **Delete project** permanently removes the selected project
and all files inside it after two confirmations. When the active project is
deleted, Codex95 switches to another sibling project or creates one with a
different name.

The console client remains available as a fallback:

```bat
CODEX95.EXE 192.168.1.50 8787 C:\DEV\CLOCK -y
```

Add `-full` only when the task genuinely requires full-computer access.
Set the console client's model with `set CODEX95_MODEL=gpt-5.4-nano`.

## Troubleshooting

- **Cannot find bridge automatically**: enter the bridge PC address manually,
  confirm both computers are on the same LAN, and allow UDP `8788`.
- **Cannot connect to bridge**: allow inbound TCP `8787` on the bridge PC's
  private-network firewall profile.
- **Bridge says API key is missing**: set `OPENAI_API_KEY` and restart the
  bridge terminal.
- **OpenAI reports that the quota was exceeded**: API billing and ChatGPT
  subscriptions are separate. Add API billing or credits to the OpenAI
  Platform account associated with the key, then retry the task.
- **Client fails to start because of MSVCRT**: use a Visual C++ 6 `/MT` build.
- **Build commands fail on Windows 95**: install a compatible compiler on the
  target computer; tools installed on the bridge PC are not available there.
- **Japanese Windows 95 text problems**: prefer ASCII source filenames and
  explicitly handle the target codepage when non-ASCII text is required.

## Current Limits

- Text files and action results are currently limited to roughly 30 KB per operation.
- The bridge uses plain HTTP because Windows 95 cannot handle modern TLS.
- File access is restricted to the selected project root unless full-computer
  access is explicitly enabled.
- `run_command` executes through `COMMAND.COM`.
- Automatic mode can execute commands proposed by the model with the project
  directory as the working directory. Commands are not sandboxed on Windows 95,
  so use the bridge only on a trusted private network.
- Service messages use plain ASCII so they remain readable on Japanese Windows
  95. Non-ASCII project files still require deliberate codepage handling.
