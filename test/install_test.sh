#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"

if [[ $# -eq 0 ]]; then
  bash "$repo_root/test/install_test.sh" shared
  bash "$repo_root/test/install_test.sh" static
  exit 0
fi

if [[ $# -ne 1 || ( "$1" != "shared" && "$1" != "static" ) ]]; then
  echo "usage: $0 [shared|static]" >&2
  exit 2
fi

mode="$1"
if [[ "$mode" == "shared" ]]; then
  build_shared=ON
  library_pattern='libnetft.so*'
else
  build_shared=OFF
  library_pattern='libnetft.a'
fi

test_root="$(mktemp -d)"
cleanup() {
  rm -rf -- "$test_root"
}
trap cleanup EXIT

package_build="$test_root/package-build"
prefix="$test_root/prefix"
consumer_build="$test_root/consumer-build"
incompatible_consumer_source="$test_root/incompatible-consumer"
incompatible_consumer_build="$test_root/incompatible-consumer-build"
install_include_dir="netft-headers"
project_version="$(
  sed -nE \
    's/^project\(netft VERSION ([0-9]+\.[0-9]+\.[0-9]+).*$/\1/p' \
    "$repo_root/CMakeLists.txt"
)"
test -n "$project_version"

cmake_compiler_args=()
if [[ -n "${NETFT_CXX_COMPILER:-}" ]]; then
  cmake_compiler_args+=("-DCMAKE_CXX_COMPILER=$NETFT_CXX_COMPILER")
fi

cmake -S "$repo_root" -B "$package_build" -G Ninja \
  "${cmake_compiler_args[@]}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS="$build_shared" \
  -DBUILD_TESTING=OFF \
  -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DCMAKE_INSTALL_INCLUDEDIR="$install_include_dir" \
  -DCMAKE_INSTALL_LIBDIR=lib
cmake --build "$package_build"
cmake --install "$package_build"

test -f "$prefix/$install_include_dir/netft/client.hpp"
test -f "$prefix/$install_include_dir/netft/discovery.hpp"
test -f "$prefix/$install_include_dir/netft/export.hpp"
test -f "$prefix/$install_include_dir/netft/status.hpp"
test -f "$prefix/$install_include_dir/netft/types.hpp"
test ! -e "$prefix/bin/netft"
find "$prefix/lib" -maxdepth 1 -name "$library_pattern" -print -quit \
  | grep -q .
test -f "$prefix/lib/cmake/netft/netftConfig.cmake"
test -f "$prefix/lib/cmake/netft/netftConfigVersion.cmake"
test -f "$prefix/lib/cmake/netft/netftTargets.cmake"

mkdir -p "$incompatible_consumer_source"
printf '%s\n' \
  'cmake_minimum_required(VERSION 3.16)' \
  'project(netft_incompatible_consumer LANGUAGES CXX)' \
  'if(NOT DEFINED NETFT_PROBE_PREFIX OR NETFT_PROBE_PREFIX STREQUAL "")' \
  '  message(FATAL_ERROR "NETFT_PROBE_PREFIX must be defined and nonempty")' \
  'endif()' \
  'find_package(netft 0.1 CONFIG QUIET' \
  '  PATHS "${NETFT_PROBE_PREFIX}" NO_DEFAULT_PATH)' \
  'if(netft_FOUND)' \
  '  message(FATAL_ERROR "netft package accepted incompatible 0.1 request")' \
  'endif()' \
  >"$incompatible_consumer_source/CMakeLists.txt"
cmake -S "$incompatible_consumer_source" \
  -B "$incompatible_consumer_build" -G Ninja \
  "${cmake_compiler_args[@]}" \
  -DNETFT_PROBE_PREFIX="$prefix"

for targets_file in "$prefix"/lib/cmake/netft/netftTargets*.cmake; do
  if grep -Fq "$repo_root" "$targets_file" || \
      grep -Fq "$package_build" "$targets_file"; then
    echo "exported targets contain a source or build path: $targets_file" >&2
    exit 1
  fi
done

if [[ "$mode" == "shared" ]]; then
  shared_library="$(find "$prefix/lib" -maxdepth 1 -name 'libnetft.so.*' \
    -type f -print -quit)"
  test -n "$shared_library"
  nm_tool="$(sed -n 's/^CMAKE_NM:FILEPATH=//p' \
    "$package_build/CMakeCache.txt")"
  test -x "$nm_tool"
  dynamic_symbols="$($nm_tool -D --defined-only --demangle "$shared_library")"
  if grep -Eq 'netft::Client::Impl|netft::detail::' <<<"$dynamic_symbols"; then
    echo "private netft implementation symbol exported by shared library" >&2
    grep -E 'netft::Client::Impl|netft::detail::' <<<"$dynamic_symbols" >&2
    exit 1
  fi
  grep -Fq 'typeinfo for netft::NotConnectedError' <<<"$dynamic_symbols"
  grep -Fq 'typeinfo name for netft::NotConnectedError' <<<"$dynamic_symbols"

  unexpected_symbols=""
  while read -r _address _type symbol; do
    if [[ "$_type" == "W" && \
          ( "$symbol" == *std::* || "$symbol" == __gnu_cxx::* ) ]]; then
      continue
    fi
    case "$symbol" in
      'netft::Client::Client(netft::Config)' | \
      'netft::Client::~Client()' | \
      'netft::Client::start('* | \
      'netft::Client::stop()' | \
      'netft::Client::bias()' | \
      'netft::Client::wait_for_first_sample('* | \
      'netft::Client::faulted() const' | \
      'netft::Client::fault_code() const' | \
      'netft::Client::health() const' | \
      'netft::Client::latest_sample() const' | \
      'netft::validate('* | \
      'netft::to_string('* | \
      'netft::force_unit_from_string('* | \
      'netft::torque_unit_from_string('* | \
      'netft::classify_status('* | \
      'netft::decode_status'* | \
      'netft::discover_sensor('* | \
      'netft::NotConnectedError::'* | \
      'netft::DiscoveryError::'* | \
      'typeinfo for netft::NotConnectedError' | \
      'typeinfo name for netft::NotConnectedError' | \
      'vtable for netft::NotConnectedError' | \
      'typeinfo for netft::DiscoveryError' | \
      'typeinfo name for netft::DiscoveryError' | \
      'vtable for netft::DiscoveryError') ;;
      *) unexpected_symbols+="$symbol"$'\n' ;;
    esac
  done <<<"$dynamic_symbols"
  if [[ -n "$unexpected_symbols" ]]; then
    echo "shared library exports symbols outside the public allowlist:" >&2
    printf '%s' "$unexpected_symbols" >&2
    exit 1
  fi
fi

cmake -S "$repo_root/test/consumer" -B "$consumer_build" -G Ninja \
  "${cmake_compiler_args[@]}" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$prefix" \
  -DNETFT_REQUIRED_VERSION="$project_version" \
  -DNETFT_EXPECTED_INCLUDE_DIR="$prefix/$install_include_dir"
cmake --build "$consumer_build"
"$consumer_build/netft_consumer"

echo "netft $mode install/consumer test passed"
