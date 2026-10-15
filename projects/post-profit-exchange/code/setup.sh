#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Dependencies and environment only. Container lifecycle belongs to the launcher.
set -euo pipefail
[[ $# == 0 ]] || { echo 'setup.sh accepts no arguments or project actions.' >&2; exit 2; }
source /etc/os-release
[[ "$ID" == debian && "$VERSION_ID" == 12 && "$(dpkg --print-architecture)" == amd64 ]] || {
  echo 'Requires Debian 12 amd64.' >&2; exit 1;
}
[[ $EUID == 0 && -f /.dockerenv ]] || { echo 'Run inside the managed Linux container as root.' >&2; exit 1; }
# These installed versions were checked against the recovered image toolchain
# and the existing TFT/price-optimization dependency locks, not chosen afresh.
pins=(
  build-essential=12.9 gcc=4:12.2.0-3 gcc-12=12.2.0-14+deb12u1
  g++=4:12.2.0-3 g++-12=12.2.0-14+deb12u1 make=4.3-4.1
  cmake=3.25.1-1 cmake-data=3.25.1-1
)
missing=()
for pin in "${pins[@]}"; do
  package=${pin%%=*}
  expected=${pin#*=}
  status=$(dpkg-query -W -f='${db:Status-Status}' "$package" 2>/dev/null || true)
  installed=$(dpkg-query -W -f='${Version}' "$package" 2>/dev/null || true)
  if [[ "$status" != installed ]]; then
    missing+=("$pin")
  elif [[ "$installed" != "$expected" ]]; then
    printf 'Installed package mismatch: %s expected %s, found %s. No automatic upgrade or downgrade.\n' \
      "$package" "$expected" "$installed" >&2
    exit 1
  fi
done
prefix=/opt/post-profit-exchange
[[ "$(realpath -m -- "$prefix")" == "$prefix" ]] || {
  echo 'Dependency inventory/cache must remain in the container filesystem.' >&2; exit 1;
}
inventory="$prefix/environment"
cache="$prefix/cache/apt"
for location in "$inventory" "$cache"; do
  [[ "$(realpath -m -- "$location")" == "$location" ]] || {
    echo 'Dependency paths cannot use symlinks outside their container-local directories.' >&2; exit 1;
  }
done
mkdir -p "$inventory" "$cache/archives/partial" "$cache/lists/partial"
if (( ${#missing[@]} )); then
  apt=(apt-get -o "Dir::Cache=$cache" -o "Dir::State::lists=$cache/lists"
       -o Acquire::Retries=3 -o Acquire::https::Timeout=30 -o Acquire::http::Timeout=30)
  "${apt[@]}" update
  DEBIAN_FRONTEND=noninteractive "${apt[@]}" install -y --no-install-recommends "${missing[@]}"
fi
for pin in "${pins[@]}"; do
  [[ "$(dpkg-query -W -f='${db:Status-Status}' "${pin%%=*}")" == installed &&
     "$(dpkg-query -W -f='${Version}' "${pin%%=*}")" == "${pin#*=}" ]] || {
    echo "Required dependency was not verified: $pin" >&2; exit 1;
  }
done
[[ "$(g++ -dumpfullversion)" == 12.2.0 &&
   "$(cmake --version | head -n 1)" == 'cmake version 3.25.1' ]] || {
  echo 'Executable versions do not match the dependency pins.' >&2; exit 1;
}
dpkg-query -W -f='${Package}=${Version}\n' > "$inventory/debian-packages.txt"
printf '%s\n' "${pins[@]}" > "$inventory/application-dependencies.txt"
temporary=$(mktemp "$inventory/dependencies-ready.XXXXXX")
sha256sum "${BASH_SOURCE[0]}" | cut -d ' ' -f 1 > "$temporary"
mv -T -- "$temporary" "$inventory/dependencies.ready"
printf 'Dependencies verified; %s missing packages installed. No build, database or container action performed.\n' "${#missing[@]}"
