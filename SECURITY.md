# Security

Codex95 is experimental software that can execute commands and modify files on
a Windows 95 computer.

- Use it only on a trusted private LAN.
- Never expose the bridge or Windows 95 machine directly to the internet.
- Keep project-only access enabled unless full-computer access is necessary.
- Review the source and keep backups before enabling automatic actions.
- Project-only mode validates paths used by file tools, but Windows 95
  `COMMAND.COM` is not a sandbox. Review build commands when automatic actions
  are disabled, because a shell command can still address files elsewhere.
- The local HTTP protocol is intentionally unauthenticated and unencrypted.
  Any device on the same LAN may be able to reach the bridge and consume its API
  quota, so use a private network and host firewall rules.
- Never commit API keys, disk images, personal files, or machine-specific INI
  files.

Security reports can be opened as GitHub issues while the project is in early
development.
