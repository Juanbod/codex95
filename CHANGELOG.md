# Changelog

Codex95 is experimental. Release numbers describe development milestones, not
production-readiness guarantees.

## v0.4.0 - 2026-09-07

### Added

- GPT-6 Astra, GPT-5.6 Sol, Terra, Luna, and Chat Latest model choices.
- A versioned bridge manifest with client size, SHA-256, model list, and default
  model.
- Optional startup checks that offer **Update now** and **Later** when the bridge
  carries a newer Windows 95 client.
- Version information in the client title and bridge health endpoint.
- A dark owner-drawn style for command buttons.

### Fixed

- Russian and Japanese responses are no longer replaced with question marks by
  the bridge.
- Windows 95 text conversion no longer depends on the unsupported UTF-8 system
  codepage; it now converts through the installed ANSI codepage such as CP932 or
  CP1251.
- The GUI inherits the localized Windows system font instead of forcing MS Sans
  Serif.
- Downloads now handle HTTP headers split across network packets and reject
  truncated executables.
- The updater keeps `CODEX95W.OLD`, retries a locked executable only a bounded
  number of times, and restores the backup after a failed copy.
- Project roots ending in a slash and overlong resolved paths are handled
  correctly.
- Expired bridge conversation IDs are discarded and retried automatically.
- Completed one-step bridge sessions are released immediately.

### Changed

- New installations default to the cost-sensitive `gpt-5.6-luna`; an existing
  model choice in `CODEX95.INI` is preserved.
- Bridge replies may carry UTF-8 tool paths and commands while the line protocol
  remains simple enough for Windows 95.
