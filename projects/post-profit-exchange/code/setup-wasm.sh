#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Dependency installation only; no container lifecycle or application tasks.
set -euo pipefail
[[ $# == 0 ]] || { echo 'setup-wasm.sh accepts no arguments' >&2; exit 2; }
container_path() {
  [[ "$1" == /* ]] || { echo "Requires an absolute container path: $1" >&2; return 2; }
  local resolved
  resolved=$(realpath -m -- "$1")
  case "$resolved" in /workspace|/workspace/*)
    echo "Toolchain and cache paths cannot use the /workspace bind mount: $resolved" >&2; return 2 ;;
  esac
  printf '%s\n' "$resolved"
}
sdk=$(container_path "${EXCHANGE_EMSDK:-/opt/post-profit-exchange/emsdk-4.0.15}")
cache=$(container_path /opt/post-profit-exchange/cache)
provenance=$(container_path /opt/post-profit-exchange/environment)
archive="$cache/emsdk-4.0.15.tar.gz"
export EM_CACHE="$cache/emscripten"
expected=35be7626493e3bd22860ee2177147f9bca3b6ff871edeab27c5b061a9ed9d23d
source /etc/os-release
[[ "$ID" == debian && "$VERSION_ID" == 12 ]] || { echo 'Requires Debian 12' >&2; exit 2; }
[[ $(dpkg --print-architecture) == amd64 ]] || { echo 'Requires Linux amd64' >&2; exit 2; }
if [[ $(dpkg-query -W -f='${Version}' python3 2>/dev/null || true) != 3.11.2-1+b1 ]]; then
  apt-get update
  DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends python3=3.11.2-1+b1
fi
mkdir -p "$cache" "$EM_CACHE" "$(dirname "$sdk")"
if [[ ! -f "$archive" ]]; then
  python3 - "$archive" <<'PY'
import sys, urllib.request
urllib.request.urlretrieve('https://github.com/emscripten-core/emsdk/archive/refs/tags/4.0.15.tar.gz', sys.argv[1])
PY
fi
printf '%s  %s\n' "$expected" "$archive" | sha256sum -c -
if [[ ! -d "$sdk" ]]; then
  mkdir "$sdk"
  tar -xzf "$archive" --strip-components=1 -C "$sdk"
fi
[[ -f "$sdk/emsdk" ]] || { echo 'Incomplete SDK staging; preserved for inspection.' >&2; exit 1; }
"$sdk/emsdk" install 4.0.15
"$sdk/emsdk" activate 4.0.15
mkdir -p "$provenance"
dpkg-query -W > "$provenance/debian-packages.txt"
"$sdk/upstream/emscripten/em++" --version
