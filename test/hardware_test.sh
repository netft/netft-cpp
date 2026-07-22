#!/usr/bin/env bash

set -euo pipefail

if [[ -z "${NETFT_SENSOR_HOST:-}" ]]; then
  printf 'NETFT_SENSOR_HOST must be set to the operator-approved sensor host\n' >&2
  exit 2
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
temp_parent="$(cd "${TMPDIR:-/tmp}" && pwd -P)"
if [[ "${temp_parent}" == / ]]; then
  printf 'refusing to create hardware-test files directly under /\n' >&2
  exit 2
fi

test_root="$(mktemp -d "${temp_parent}/netft-hardware-test.XXXXXX")"
case "${test_root}" in
  "${temp_parent}"/netft-hardware-test.??????) ;;
  *)
    printf 'mktemp returned an unexpected path; refusing cleanup: %s\n' "${test_root}" >&2
    exit 2
    ;;
esac

cleanup() {
  case "${test_root}" in
    "${temp_parent}"/netft-hardware-test.??????)
      if [[ -d "${test_root}" ]]; then
        rm -rf -- "${test_root}"
      fi
      ;;
    *)
      printf 'refusing to clean an unvalidated path: %s\n' "${test_root}" >&2
      ;;
  esac
}
trap cleanup EXIT

build_dir="${test_root}/build"
prefix="${test_root}/prefix"

cmake -S "${repo_root}" -B "${build_dir}" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DBUILD_TESTING=OFF \
  -DCMAKE_INSTALL_PREFIX="${prefix}"
cmake --build "${build_dir}"
cmake --install "${build_dir}"

cli="${prefix}/bin/netft"
if [[ ! -x "${cli}" ]]; then
  printf 'installed netft CLI is missing or not executable: %s\n' "${cli}" >&2
  exit 2
fi

validate_json() {
  local kind="$1"
  local payload="$2"
  NETFT_HARDWARE_JSON="${payload}" python - "${kind}" <<'PY'
import json
import math
import os
import sys


def fail(message):
    raise SystemExit(f"hardware test validation failed: {message}")


def require_object(value):
    if not isinstance(value, dict):
        fail("top-level JSON value must be an object")
    return value


def require_text(document, key):
    value = document.get(key)
    if not isinstance(value, str) or not value:
        fail(f"{key} must be a nonempty string")
    return value


def require_number(document, key, minimum=None, exact=None):
    value = document.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        fail(f"{key} must be a finite number")
    if minimum is not None and value < minimum:
        fail(f"{key} must be at least {minimum}, observed {value}")
    if exact is not None and value != exact:
        fail(f"{key} must equal {exact}, observed {value}")
    return value


def require_integer(document, key, minimum=None, exact=None):
    value = document.get(key)
    if isinstance(value, bool) or not isinstance(value, int):
        fail(f"{key} must be an integer")
    if minimum is not None and value < minimum:
        fail(f"{key} must be at least {minimum}, observed {value}")
    if exact is not None and value != exact:
        fail(f"{key} must equal {exact}, observed {value}")
    return value


try:
    document = require_object(json.loads(os.environ["NETFT_HARDWARE_JSON"]))
except (json.JSONDecodeError, UnicodeDecodeError) as error:
    fail(f"invalid JSON: {error}")

kind = sys.argv[1]
product = require_text(document, "product")
source = require_text(document, "configuration_source")
if source != "sensor":
    fail(f"configuration_source must be sensor, observed {source}")

force_unit = require_text(document, "force_unit")
known_force_units = {"lbf", "N", "klbf", "kN", "kgf"}
if force_unit not in known_force_units:
    fail(f"force_unit is not recognized: {force_unit}")

torque_unit = require_text(document, "torque_unit")
known_torque_units = {"lbf-in", "lbf-ft", "N-m", "N-mm", "kgf-cm", "kN-m"}
if torque_unit not in known_torque_units:
    fail(f"torque_unit is not recognized: {torque_unit}")

force_scale = require_number(document, "counts_per_force_unit", minimum=0)
torque_scale = require_number(document, "counts_per_torque_unit", minimum=0)
if force_scale <= 0 or torque_scale <= 0:
    fail("calibration scales must be positive")

if kind == "info":
    print("Observed sensor configuration:")
    print(f"  product={product}")
    print(f"  force={force_scale} counts/{force_unit}")
    print(f"  torque={torque_scale} counts/{torque_unit}")
elif kind == "monitor":
    sample_count = require_integer(document, "sample_count", minimum=1)
    received_count = require_integer(document, "received_count", minimum=1000)
    delivered_count = require_integer(document, "delivered_count", minimum=1)
    if sample_count != delivered_count:
        fail(
            f"sample_count ({sample_count}) must equal delivered_count ({delivered_count})"
        )
    if delivered_count > received_count:
        fail(
            f"delivered_count ({delivered_count}) exceeds received_count ({received_count})"
        )
    malformed_count = require_integer(document, "malformed_count", exact=0)
    device_error_count = require_integer(document, "device_error_count", exact=0)
    fault_code = require_text(document, "fault_code")
    if fault_code != "none":
        fail(f"fault_code must be none, observed {fault_code}")
    receive_rate = require_number(document, "receive_rate_hz", minimum=0)
    delivery_rate = require_number(document, "delivery_rate_hz", minimum=0)
    if receive_rate <= 0 or delivery_rate <= 0:
        fail("receive and delivery rates must be positive")
    print("Observed monitor summary:")
    print(
        f"  received={received_count} delivered={delivered_count} "
        f"sample_count={sample_count}"
    )
    print(
        f"  receive_rate_hz={receive_rate} delivery_rate_hz={delivery_rate} "
        f"malformed={malformed_count} device_errors={device_error_count}"
    )
    print(f"  fault_code={fault_code}; monitor exited successfully and requested stop")
elif kind == "bias":
    if document.get("bias_applied") is not True:
        fail("bias_applied must be true")
    print("Observed authorized bias result: bias_applied=true")
else:
    fail(f"unknown validation kind: {kind}")
PY
}

info_json="$("${cli}" info --host "${NETFT_SENSOR_HOST}" --json)"
validate_json info "${info_json}"

monitor_json="$("${cli}" monitor --host "${NETFT_SENSOR_HOST}" --duration 2 --json)"
validate_json monitor "${monitor_json}"

if [[ "${NETFT_ALLOW_BIAS:-0}" == 1 ]]; then
  bias_json="$("${cli}" bias --host "${NETFT_SENSOR_HOST}" --json)"
  validate_json bias "${bias_json}"
else
  printf 'Software bias skipped (NETFT_ALLOW_BIAS is not exactly 1).\n'
fi
