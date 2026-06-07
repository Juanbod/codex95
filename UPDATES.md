# Codex95 Update Design

One-click updates are planned for both the Windows 95 client and the modern
bridge. They must not silently replace executable files or trust an unsigned
download.

## Intended User Experience

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

The update mechanism is intentionally not enabled until package signing,
rollback, and compatibility checks are implemented and tested.
