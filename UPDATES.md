# Codex95 Update Design

Codex95 currently has a simple local-network updater for the Windows 95 GUI
client. The modern bridge serves `build/CODEX95W.EXE` at
`/client/CODEX95W.EXE`, and the Windows 95 client can download it from
**Options > Update Codex95...**.

This is a development convenience for trusted Ethernet/LAN use. It is not yet a
full signed release-update system.

## Current Update Flow

1. The user starts the bridge on the modern computer.
2. The user chooses **Options > Update Codex95...** on Windows 95.
3. The client downloads `/client/CODEX95W.EXE` to `CODEX95W.NEW`.
4. The client writes `APPLYUPD.BAT`, exits, and lets the batch file replace the
   old `CODEX95W.EXE`.
5. The batch file starts the new `CODEX95W.EXE`.

The current updater preserves `CODEX95.INI`, chats, projects, and bridge state.
It only replaces the GUI client executable.

## Intended Release User Experience

1. Codex95 checks a small signed release manifest through the modern bridge.
2. When a newer compatible release exists, the client shows its version and
   release notes with **Update now** and **Later** buttons.
3. **Update now** asks the bridge to download and verify the release.
4. The bridge updates itself with a backup and rollback path.
5. The Windows 95 client downloads the verified client package from the bridge,
   exits, and lets a tiny updater replace the old executable.
6. If startup verification fails, the updater restores the previous version.

## Security Requirements

- Release manifests and packages must be authenticated and include SHA-256
  hashes.
- The bridge downloads updates using modern HTTPS; Windows 95 only receives the
  already verified package over the trusted local network.
- Updates require an explicit click and never run during an active task.
- Existing `CODEX95.INI`, chats, projects, and bridge conversation state are
  preserved.
- Every release remains manually downloadable as a recovery option.

The current local updater is intentionally explicit and manual until package
signing, rollback, and compatibility checks are implemented and tested.
