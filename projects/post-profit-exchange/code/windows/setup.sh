#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Dependencies only; builds, tests and packaging have separate entry points.
set -euo pipefail
[[ $# == 0 && -f /.dockerenv ]] || { echo 'Use the managed Debian development container, without arguments.' >&2; exit 2; }
source_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
bash "$source_dir/../launcher/setup.sh"
source /etc/os-release
[[ "$ID" == debian && "$VERSION_ID" == 12 && "$EUID" == 0 ]] || exit 1
pins=(wine64=8.0~repack-4 libwine=8.0~repack-4)
missing=()
for pin in "${pins[@]}"; do
  package=${pin%%=*}
  expected=${pin#*=}
  status=$(dpkg-query -W -f='${db:Status-Status}' "$package" 2>/dev/null || true)
  installed=$(dpkg-query -W -f='${Version}' "$package" 2>/dev/null || true)
  if [[ "$status" != installed ]]; then missing+=("$pin")
  elif [[ "$installed" != "$expected" ]]; then
    printf 'Package mismatch: %s expected %s, found %s. No replacement performed.\n' "$package" "$expected" "$installed" >&2
    exit 1
  fi
done
prefix=/opt/post-profit-exchange/windows
[[ "$(realpath -m -- "$prefix")" == "$prefix" ]] || { echo 'Dependency files must stay inside the container.' >&2; exit 1; }
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
printf '%s\n' "${pins[@]}" > "$prefix/environment/test-dependencies.txt"
echo 'Windows cross compiler and Wine dependencies verified. No build or application start performed.'
