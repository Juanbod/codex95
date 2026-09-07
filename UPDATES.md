# Codex95 Update Design

Codex95 has a version-aware local-network updater for the Windows 95 GUI client.
The modern bridge serves a manifest at `/client/manifest` and
`build/CODEX95W.EXE` at `/client/CODEX95W.EXE`.

This is a development convenience for trusted Ethernet/LAN use. It is not yet a
full signed release-update system.

## Current Update Flow

1. The user updates and starts the bridge on the modern computer.
2. Codex95 checks the manifest at startup unless `CheckUpdates=0` is set.
3. A newer version produces an **Update now** or **Later** prompt. A manual
   check is always available at **Options > Update Codex95...**.
4. The client validates the advertised file size and downloads the executable
   to `CODEX95W.NEW`.
5. It rejects a truncated download or a file without the DOS `MZ` executable
   header.
6. `APPLYUPD.BAT` keeps the current executable as `CODEX95W.OLD`, closes the
   client, installs the new executable, and starts it.
7. Locked-file retries are bounded. A failed install restores the old copy.

The updater preserves `CODEX95.INI`, chats, projects, and bridge state. It only
replaces the GUI client executable. The manifest also publishes a SHA-256 hash
for modern tooling, but the tiny Windows 95 client currently verifies size and
the executable header rather than implementing its own SHA-256 routine.

## Future Work

1. Sign release manifests and verify signatures on the modern bridge.
2. Let the bridge download and verify GitHub releases over modern HTTPS.
3. Add a guarded bridge self-update with backup and rollback.
4. Display release notes in the Windows 95 update prompt.
5. Confirm successful startup before deleting an older recovery copy.

## Security Requirements

- Future remote release manifests and packages must be authenticated and
  include SHA-256 hashes.
- The bridge downloads updates using modern HTTPS; Windows 95 only receives the
  already verified package over the trusted local network.
- Updates require an explicit click and never run during an active task.
- Existing `CODEX95.INI`, chats, projects, and bridge conversation state are
  preserved.
- Every release remains manually downloadable as a recovery option.

The current updater still requires an explicit confirmation. Its HTTP endpoint
must be reachable only on a trusted private LAN; it is not an internet-facing
software distribution service.
