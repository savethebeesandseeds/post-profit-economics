#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
[[ -f /.dockerenv ]] || { echo 'Build inside the managed development container.' >&2; exit 2; }
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
build=/opt/post-profit-exchange/windows/build
package=
if (( $# )); then
  [[ $# == 2 && "$1" == --idydb-package ]] || { echo 'Usage: build.sh [--idydb-package /absolute/container/path]' >&2; exit 2; }
  package=$(realpath -e -- "$2")
  [[ "$package" == /* ]] || exit 2
fi
[[ "$(realpath -m -- "$build")" == "$build" ]] || { echo 'Build output must remain inside the container.' >&2; exit 1; }
[[ -x /usr/lib/wine/wine64 ]] || { echo "Run setup.sh: Wine is required for Windows executable tests." >&2; exit 1; }
cmake -S "$source_dir" -B "$build" \
  -DCMAKE_TOOLCHAIN_FILE="$source_dir/toolchain-mingw.cmake" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DPH_EXCHANGE_IDYDB_PACKAGE="$package"
cmake --build "$build" --parallel 2
ctest --test-dir "$build" --output-on-failure
if [[ -z "$package" ]]; then
  echo 'Exchange-owned Windows objects and independent tests built. No database library supplied, so no application executable was linked.'
fi
