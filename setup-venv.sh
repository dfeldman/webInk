#!/usr/bin/env bash
set -euo pipefail

# Create a Python virtual environment for the webInk project and
# install ESPHome plus the server/dashboard dependencies.

ROOT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$ROOT_DIR"

if ! command -v uv >/dev/null 2>&1; then
  echo "Error: 'uv' is not installed or not on PATH. See https://docs.astral.sh/uv/ for installation instructions." >&2
  exit 1
fi

echo "Using uv version: $(uv --version)"

# Use uv to sync the server project environment from pyproject.toml + uv.lock.
(
  cd server
  echo "Syncing server environment with uv (using pyproject.toml and uv.lock)..."
  uv sync
)

echo
echo "Environment setup complete. To use it, either:"
echo "  1) cd server && uv run <command>"
echo "  2) source server/.venv/bin/activate"
