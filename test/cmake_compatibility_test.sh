#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
cmake_version="$(cmake --version | sed -n '1s/^cmake version //p')"

case "$cmake_version" in
  3.16.*) ;;
  *)
    echo "expected CMake 3.16.x, got ${cmake_version:-unknown}" >&2
    exit 1
    ;;
esac

bash "$repo_root/test/install_test.sh" shared
bash "$repo_root/test/install_test.sh" static
