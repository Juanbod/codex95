#!/bin/bash
set -u

cd "$(dirname "$0")"

if ! command -v node >/dev/null 2>&1; then
  echo "Codex95 requires Node.js."
  echo "Install it from https://nodejs.org or run: brew install node"
  read -r -p "Press Return to close..."
  exit 1
fi

export CODEX95_MOCK=1
echo "Starting Codex95 bridge in free mock mode. No API key is used."
echo "Press Control-C to stop it."
node server.mjs

status=$?
echo
echo "Codex95 bridge stopped with status $status."
read -r -p "Press Return to close..."
exit "$status"
