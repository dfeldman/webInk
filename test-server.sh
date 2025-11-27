#!/usr/bin/env bash
set -euo pipefail

# Run a basic integration test against the webInk server.
# This mirrors the GitHub Actions test-server job:
#   - Sync server environment with uv
#   - Start webInk.py on port 8000
#   - Wait for the server to become ready
#   - Run server/test_client.py in --basic mode
#   - Shut the server down

ROOT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$ROOT_DIR"

if ! command -v uv >/dev/null 2>&1; then
  echo "Error: 'uv' is not installed or not on PATH. See https://docs.astral.sh/uv/ for installation instructions." >&2
  exit 1
fi

if ! command -v curl >/dev/null 2>&1; then
  echo "Error: 'curl' is required for readiness checks." >&2
  exit 1
fi

echo "Using uv version: $(uv --version)"

# Ensure server environment is up to date
(
  cd server
  echo "Syncing server environment with uv (using pyproject.toml and uv.lock)..."
  uv sync
)

(
  cd server
  echo "Starting webInk server on http://127.0.0.1:8000 ..."
  uv run webInk.py &
  SERVER_PID=$!

  cleanup() {
    echo "Stopping webInk server (pid=$SERVER_PID)..."
    kill "$SERVER_PID" >/dev/null 2>&1 || true
    wait "$SERVER_PID" 2>/dev/null || true
  }
  trap cleanup EXIT

  echo "Waiting for server to become ready..."
  READY=0
  for i in {1..300}; do
    if curl -sSf http://127.0.0.1:8000/api/config >/dev/null 2>&1; then
      READY=1
      break
    fi
    sleep 1
  done

  if [ "$READY" -ne 1 ]; then
    echo "Server did not become ready in time" >&2
    exit 1
  fi

  echo "Server is ready. Running basic integration test..."
  uv run test_client.py \
    --server http://127.0.0.1:8000 \
    --api-key myapikey \
    --device test_device \
    --mode 800x480x1xB \
    --tile-size 200 \
    --no-sleep \
    --basic
)
