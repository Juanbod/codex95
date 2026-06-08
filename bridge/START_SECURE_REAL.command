#!/bin/bash
set -u

cd "$(dirname "$0")"

if ! command -v node >/dev/null 2>&1; then
  echo "Codex95 requires Node.js."
  echo "Install it from https://nodejs.org or run: brew install node"
  read -r -p "Press Return to close..."
  exit 1
fi

read -r -s -p "Paste OpenAI API key (input is hidden): " OPENAI_API_KEY
echo
export OPENAI_API_KEY

cleanup() {
  unset OPENAI_API_KEY
}
trap cleanup EXIT INT TERM

echo "Starting Codex95 bridge. Keep this window open."
echo "Press Control-C to stop it."
node server.mjs

status=$?
echo
echo "Codex95 bridge stopped with status $status."
read -r -p "Press Return to close..."
exit "$status"
