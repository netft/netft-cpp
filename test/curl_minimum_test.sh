#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
test_root="$(mktemp -d)"
trap 'rm -rf -- "${test_root}"' EXIT

make_curl_fixture() {
  local version="$1"
  local fixture="$2"

  mkdir -p "${fixture}/include/curl" "${fixture}/lib"
  printf '#define LIBCURL_VERSION "%s"\n' "${version}" \
    >"${fixture}/include/curl/curlver.h"
  : >"${fixture}/include/curl/curl.h"
  : >"${fixture}/lib/libcurl.a"
}

configure_with_curl() {
  local fixture="$1"
  local build_dir="$2"

  cmake -S "${repo_root}" -B "${build_dir}" -G Ninja \
    -DBUILD_TESTING=OFF \
    -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON \
    -DCURL_NO_CURL_CMAKE=ON \
    -DCURL_INCLUDE_DIR="${fixture}/include" \
    -DCURL_LIBRARY="${fixture}/lib/libcurl.a"
}

make_curl_fixture 7.62.0 "${test_root}/curl-7.62"
if configure_with_curl \
  "${test_root}/curl-7.62" "${test_root}/build-7.62" \
  >"${test_root}/curl-7.62.log" 2>&1; then
  printf '%s\n' 'libcurl 7.62.0 unexpectedly satisfied the project requirement' >&2
  exit 1
fi

make_curl_fixture 7.63.0 "${test_root}/curl-7.63"
if ! configure_with_curl \
  "${test_root}/curl-7.63" "${test_root}/build-7.63" \
  >"${test_root}/curl-7.63.log" 2>&1; then
  printf '%s\n' 'libcurl 7.63.0 did not satisfy the project requirement' >&2
  sed 's/^/  /' "${test_root}/curl-7.63.log" >&2
  exit 1
fi

grep -Eq '^find_dependency\(CURL 7\.63(\.0)?\)$' \
  "${repo_root}/cmake/netftConfig.cmake.in"
grep -Eq '^[[:space:]]*libcurl = ">=7\.63\.0"$' \
  "${repo_root}/pixi.toml"
grep -Eq '^[[:space:]]*SOVERSION 1$' "${repo_root}/CMakeLists.txt"
