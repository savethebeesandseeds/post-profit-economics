#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
project="$root/projects/post-profit-exchange"

# Toolchains and intermediate files must stay in the Linux container.
container_path() {
  [[ "$1" == /* ]] || { echo "Requires an absolute container path: $1" >&2; return 2; }
  local resolved
  resolved=$(realpath -m -- "$1")
  case "$resolved" in /workspace|/workspace/*)
    echo "Toolchain and build paths cannot use the /workspace bind mount: $resolved" >&2; return 2 ;;
  esac
  printf '%s\n' "$resolved"
}
build=$(container_path "${EXCHANGE_WASM_BUILD_DIR:-/tmp/post-profit-exchange/wasm}")
sdk=$(container_path "${EXCHANGE_EMSDK:-/opt/post-profit-exchange/emsdk-4.0.15}")
cache=$(container_path /opt/post-profit-exchange/cache/emscripten)
[[ -f "$sdk/emsdk_env.sh" ]] || { echo 'Run code/setup-wasm.sh inside the managed Debian container first.' >&2; exit 1; }
source "$sdk/emsdk_env.sh" >/dev/null 2>&1
export EM_CACHE="$cache"
em++ --version | head -1 | grep -F '4.0.15' >/dev/null || { echo 'Requires pinned Emscripten 4.0.15.' >&2; exit 1; }
mkdir -p "$build" "$EM_CACHE"
python3 "$project/code/embed-config.py" "$build/generated/default_config.hpp"
em++ -std=c++17 -O2 -fexceptions -Wall -Wextra -Wpedantic \
  -I"$root/tools/price-optimization/include" \
  -I"$root/tools/exponential-smoothing/include" \
  -I"$build/generated" \
  -isystem "$root/tools/price-optimization/third_party" \
  "$project/code/simulation.cpp" \
  "$project/code/models.cpp" \
  "$root/tools/exponential-smoothing/src/ewma.cpp" \
  "$root/tools/price-optimization/src/engine.cpp" \
  "$root/tools/price-optimization/src/enumeration.cpp" \
  --no-entry -sMODULARIZE=1 -sEXPORT_NAME=createExchangeModule \
  -sSINGLE_FILE=1 -sENVIRONMENT=web,worker,node -sALLOW_MEMORY_GROWTH=1 \
  -sMAXIMUM_MEMORY=268435456 -sSTACK_SIZE=1048576 -sFILESYSTEM=0 \
  -sDYNAMIC_EXECUTION=0 -sDISABLE_EXCEPTION_CATCHING=0 -sEMIT_EMSCRIPTEN_LICENSE=1 \
  -sEXPORTED_FUNCTIONS='["_exchange_run","_malloc","_free"]' \
  -sEXPORTED_RUNTIME_METHODS='["ccall"]' -o "$build/exchange-runtime.js"
python3 "$project/web/package-html.py" "$build/exchange-runtime.js" "$sdk"
