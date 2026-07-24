#!/usr/bin/env bash

set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${repo_root}/build/gcc-10"

pixi exec \
  --spec "gxx_linux-64=10.4.*" \
  --spec cmake \
  --spec ninja \
  --spec gtest \
  --spec libcurl \
  bash -c '
    set -euo pipefail
    repo_root="$1"
    build_dir="$2"
    cmake -S "${repo_root}" -B "${build_dir}" -G Ninja \
      -DCMAKE_CXX_COMPILER=x86_64-conda-linux-gnu-c++ \
      -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_SHARED_LIBS=ON \
      -DBUILD_TESTING=ON
    cmake --build "${build_dir}"
    ctest --test-dir "${build_dir}" --output-on-failure
  ' bash "${repo_root}" "${build_dir}"
