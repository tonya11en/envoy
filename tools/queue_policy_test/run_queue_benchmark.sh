#!/usr/bin/env bash
# Repeatable end-to-end HTTP queue policy benchmark runner.
#
# Usage:
#   ./tools/queue_policy_test/run_queue_benchmark.sh [--plot-only] [--config path/to/scenario.yaml]

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../.." && pwd)"
CONFIG_PATH="${SCRIPT_DIR}/scenario.yaml"
OUTPUT_DIR="${SCRIPT_DIR}/results"

# Set up Python virtual environment with matplotlib, numpy, and pyyaml.
VENV_DIR="${SCRIPT_DIR}/.venv"
if [[ ! -d "${VENV_DIR}" ]]; then
  echo "[INFO] Creating Python virtual environment in ${VENV_DIR}..."
  python3 -m venv --system-site-packages "${VENV_DIR}"
  "${VENV_DIR}/bin/pip" install --quiet matplotlib numpy pyyaml 2>/dev/null || true
fi
PYTHON_BIN="${VENV_DIR}/bin/python3"

# Locate Envoy binary.
if [[ -z "${ENVOY_BIN:-}" ]]; then
  if [[ -x "${REPO_ROOT}/bazel-bin/source/exe/envoy-static" ]]; then
    ENVOY_BIN="${REPO_ROOT}/bazel-bin/source/exe/envoy-static"
  elif command -v envoy >/dev/null 2>&1; then
    ENVOY_BIN="$(command -v envoy)"
  else
    echo "ERROR: Envoy binary not found. Build it with: bazel build //source/exe:envoy-static" >&2
    exit 1
  fi
fi

# Locate Nighthawk client binary or fallback to Docker image.
if [[ -z "${NIGHTHAWK_CLIENT_BIN:-}" ]]; then
  if command -v nighthawk_client >/dev/null 2>&1; then
    NIGHTHAWK_CLIENT_BIN="nighthawk_client"
  else
    NIGHTHAWK_CLIENT_BIN="docker run --rm --network host --entrypoint /usr/local/bin/nighthawk_client envoyproxy/nighthawk-dev:latest"
  fi
fi

# Locate Nighthawk test server binary or fallback to Docker image.
if [[ -z "${NIGHTHAWK_SERVER_BIN:-}" ]]; then
  if command -v nighthawk_test_server >/dev/null 2>&1; then
    NIGHTHAWK_SERVER_BIN="nighthawk_test_server"
  else
    NIGHTHAWK_SERVER_BIN="docker run --rm --network host --entrypoint /usr/local/bin/nighthawk_test_server envoyproxy/nighthawk-dev:latest"
  fi
fi

"${PYTHON_BIN}" "${SCRIPT_DIR}/run_benchmark.py" \
  --config "${CONFIG_PATH}" \
  --output-dir "${OUTPUT_DIR}" \
  --envoy-bin "${ENVOY_BIN}" \
  --nighthawk-client-bin "${NIGHTHAWK_CLIENT_BIN}" \
  --nighthawk-server-bin "${NIGHTHAWK_SERVER_BIN}" \
  "$@"
