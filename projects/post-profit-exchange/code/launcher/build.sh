#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
[[ $# == 0 && -f /.dockerenv ]] || { echo 'Run build.sh without arguments in the managed Debian container.' >&2; exit 2; }
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
output=/opt/post-profit-exchange/launcher
[[ "$(realpath -m -- "$output")" == "$output" ]] || { echo 'Build output must remain inside the container.' >&2; exit 1; }
mkdir -p "$output"
g++ -std=c++17 -Wall -Wextra -Werror "$source_dir/arguments_test.cpp" -o "$output/arguments_test"
"$output/arguments_test"
x86_64-w64-mingw32-g++-posix -std=c++17 -O2 -Wall -Wextra -Werror -municode \
  -static -static-libgcc -static-libstdc++ -Wl,--no-insert-timestamp \
  "$source_dir/main.cpp" -o "$output/post-profit-exchange.exe"
x86_64-w64-mingw32-objdump -p "$output/post-profit-exchange.exe" | sed -n '/DLL Name:/p'
sha256sum "$output/post-profit-exchange.exe"
