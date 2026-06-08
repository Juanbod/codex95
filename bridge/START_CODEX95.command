#!/bin/bash
set -u

cd "$(dirname "$0")"

if ! command -v node >/dev/null 2>&1; then
  echo "Codex95 requires Node.js."
  echo "Install it from https://nodejs.org or run: brew install node"
  read -r -p "Press Return to close..."
  exit 1
fi

(sleep 2; open "http://127.0.0.1:${CODEX95_PORT:-8787}/setup") &
echo "Starting Codex95 bridge. Keep this window open."
echo "Press Control-C to stop it."
node server.mjs

status=$?
echo
echo "Codex95 bridge stopped with status $status."
read -r -p "Press Return to close..."
exit "$status"
