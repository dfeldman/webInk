#!/usr/bin/env bash
set -euo pipefail

# Build ESPHome firmware images from YAML configs.
# This script is intended to be run from anywhere; it will cd to the repo root.

ROOT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
cd "$ROOT_DIR"
# Dedicated ESPHome environment directory (relative to repo root).
ESPHOME_ENV_DIR="${ESPHOME_ENV_DIR:-client/esphome}"
ESPHOME_VENV_DIR="$ROOT_DIR/$ESPHOME_ENV_DIR/.venv"
PYTHON_BIN="${PYTHON_BIN:-python3}"

ensure_esphome_venv() {
  # If the venv already has esphome installed, reuse it.
  if [[ -x "$ESPHOME_VENV_DIR/bin/esphome" ]]; then
    return 0
  fi

  if command -v uv >/dev/null 2>&1; then
    (
      cd "$ESPHOME_ENV_DIR"
      if [[ ! -d ".venv" ]]; then
        echo "Creating ESPHome virtual environment in '$ESPHOME_ENV_DIR/.venv' with uv..."
        uv venv
      else
        echo "Using existing ESPHome virtual environment in '$ESPHOME_ENV_DIR/.venv'..."
      fi
      echo "Installing/upgrading ESPHome in ESPHome environment..."
      uv pip install --upgrade esphome
    )
  else
    if ! command -v "$PYTHON_BIN" >/dev/null 2>&1; then
      echo "Error: neither 'uv' nor '$PYTHON_BIN' found; cannot create ESPHome virtual environment." >&2
      exit 1
    fi

    if [[ ! -d "$ESPHOME_VENV_DIR" ]]; then
      echo "Creating ESPHome virtual environment in '$ESPHOME_VENV_DIR'..."
      "$PYTHON_BIN" -m venv "$ESPHOME_VENV_DIR"
    else
      echo "Using existing ESPHome virtual environment in '$ESPHOME_VENV_DIR'..."
    fi

    "$ESPHOME_VENV_DIR/bin/pip" install --upgrade pip
    "$ESPHOME_VENV_DIR/bin/pip" install --upgrade esphome
  fi

  if [[ ! -x "$ESPHOME_VENV_DIR/bin/esphome" ]]; then
    echo "Error: 'esphome' CLI not found in ESPHome virtual environment at '$ESPHOME_VENV_DIR'." >&2
    exit 1
  fi
}

# Map of YAML config -> output .bin path (relative to repo root).
# Add additional entries here as more devices/configs are introduced.
TARGETS=(
  "client/esphome/webInk.yaml:client/build/webInk-esp32-c3.bin"
)

build_target() {
  local yaml_path="$1"
  local output_path="$2"

  if [[ ! -f "$yaml_path" ]]; then
    echo "Error: YAML config '$yaml_path' does not exist." >&2
    exit 1
  fi

  echo "==> Building ESPHome firmware from $yaml_path"

  ensure_esphome_venv

  local marker
  marker="$(mktemp)"
  touch "$marker"

  "$ESPHOME_VENV_DIR/bin/esphome" compile "$yaml_path"

  # Find the newest firmware*.bin generated after the marker timestamp.
  # ESPHome creates a .esphome directory adjacent to the YAML config.
  local yaml_dir
  yaml_dir="$(dirname "$yaml_path")"
  local bin_path
  bin_path="$(
    find "$yaml_dir/.esphome/build" -type f -name 'firmware*.bin' -newer "$marker" -print0 2>/dev/null \
      | xargs -0 ls -t 2>/dev/null \
      | head -n1 || true
  )"

  rm -f "$marker"

  if [[ -z "${bin_path:-}" ]]; then
    echo "Error: No new firmware .bin produced for '$yaml_path'." >&2
    exit 1
  fi

  mkdir -p "$(dirname "$output_path")"
  cp "$bin_path" "$output_path"

  # Also copy next to the YAML under a local build directory for convenience,
  # e.g. client/esphome/build/webInk-esp32-c3.bin
  local local_build_dir
  local_build_dir="$yaml_dir/build"
  mkdir -p "$local_build_dir"
  cp "$bin_path" "$local_build_dir/$(basename "$output_path")"

  echo "Saved firmware to $output_path and $local_build_dir/$(basename "$output_path") (source: $bin_path)"
}

for entry in "${TARGETS[@]}"; do
  IFS=":" read -r yaml_path output_path <<<"$entry"
  build_target "$yaml_path" "$output_path"
  echo
done

echo "All ESPHome firmware builds completed successfully."
