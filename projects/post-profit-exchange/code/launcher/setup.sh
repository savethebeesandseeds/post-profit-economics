#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Reproducible launcher dependencies only; build and lifecycle use separate runners.
set -euo pipefail
[[ $# == 0 ]] || { echo 'setup.sh accepts no arguments.' >&2; exit 2; }
source /etc/os-release
[[ "$ID" == debian && "$VERSION_ID" == 12 && "$(dpkg --print-architecture)" == amd64 &&
   $EUID == 0 && -f /.dockerenv ]] || { echo 'Use the managed Debian 12 amd64 container as root.' >&2; exit 1; }
pins=(
  g++-mingw-w64-x86-64-posix=12.2.0-14+deb12u1+25.2+b1
  gcc-mingw-w64-x86-64-posix=12.2.0-14+deb12u1+25.2+b1
  gcc-mingw-w64-x86-64-posix-runtime=12.2.0-14+deb12u1+25.2+b1
  gcc-mingw-w64-base=12.2.0-14+deb12u1+25.2+b1
  mingw-w64-x86-64-dev=10.0.0-3
  mingw-w64-common=10.0.0-3
  binutils-mingw-w64-x86-64=2.40-2+10.4
)
missing=()
for pin in "${pins[@]}"; do
  package=${pin%%=*}
  expected=${pin#*=}
  status=$(dpkg-query -W -f='${db:Status-Status}' "$package" 2>/dev/null || true)
  installed=$(dpkg-query -W -f='${Version}' "$package" 2>/dev/null || true)
  if [[ "$status" != installed ]]; then missing+=("$pin")
  elif [[ "$installed" != "$expected" ]]; then
    printf 'Package mismatch: %s expected %s, found %s. No automatic replacement.\n' "$package" "$expected" "$installed" >&2
    exit 1
  fi
done
prefix=/opt/post-profit-exchange/launcher
[[ "$(realpath -m -- "$prefix")" == "$prefix" ]] || { echo 'Launcher dependencies must stay inside the container.' >&2; exit 1; }
cache="$prefix/cache/apt"
mkdir -p "$cache/archives/partial" "$cache/lists/partial" "$prefix/environment"
if (( ${#missing[@]} )); then
  apt=(apt-get -o "Dir::Cache=$cache" -o "Dir::State::lists=$cache/lists"
       -o Acquire::Retries=3 -o Acquire::https::Timeout=30 -o Acquire::http::Timeout=30)
  "${apt[@]}" update
  DEBIAN_FRONTEND=noninteractive "${apt[@]}" install -y --no-install-recommends "${missing[@]}"
fi
for pin in "${pins[@]}"; do
  [[ "$(dpkg-query -W -f='${db:Status-Status}' "${pin%%=*}")" == installed &&
     "$(dpkg-query -W -f='${Version}' "${pin%%=*}")" == "${pin#*=}" ]] || exit 1
done
[[ "$(x86_64-w64-mingw32-g++-posix -dumpfullversion)" == 12-posix ||
   "$(x86_64-w64-mingw32-g++-posix -dumpfullversion)" == 12 ]] || {
  echo 'Unexpected MinGW executable version.' >&2; exit 1;
}
printf '%s\n' "${pins[@]}" > "$prefix/environment/dependencies.txt"
printf 'Launcher dependencies verified; %s missing packages installed. No build or container action performed.\n' "${#missing[@]}"
