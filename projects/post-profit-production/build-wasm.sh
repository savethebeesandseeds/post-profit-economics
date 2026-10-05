#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Build only inside the project's documented managed Linux environment.
set -euo pipefail
runtime_only=false
if [[ $# -gt 0 ]]; then
  [[ $# == 1 && $1 == --runtime-only ]] || { echo 'Usage: build-wasm.sh [--runtime-only]' >&2; exit 2; }
  runtime_only=true
fi
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
project="$root/projects/post-profit-production"
build="$root/.build/post-profit-production/wasm"
sdk="${PRODUCTION_EMSDK:-$root/.build/deps/emsdk-4.0.15}"
[[ -f "$sdk/emsdk_env.sh" ]] || { echo 'Requires the documented shared Emscripten 4.0.15 SDK.' >&2; exit 1; }
source "$sdk/emsdk_env.sh" >/dev/null 2>&1
em++ --version | head -1 | grep -F '4.0.15' >/dev/null || { echo 'Requires pinned Emscripten 4.0.15.' >&2; exit 1; }
mkdir -p "$build"
python3 "$project/simulation/embed-config.py" "$build/generated/default_config.hpp"
em++ -std=c++17 -O2 -fexceptions -Wall -Wextra -Wpedantic \
  -I"$project/simulation" -I"$project/experiments" -I"$build/generated" \
  -isystem "$root/tools/price-optimization/third_party" \
  "$project/simulation/simulation.cpp" \
  "$project/experiments/experiment.cpp" \
  "$project/simulation/wasm.cpp" \
  --no-entry -sMODULARIZE=1 -sEXPORT_NAME=createProductionModule \
  -sSINGLE_FILE=1 -sENVIRONMENT=web,worker,node -sALLOW_MEMORY_GROWTH=1 \
  -sMAXIMUM_MEMORY=536870912 -sSTACK_SIZE=8388608 -sFILESYSTEM=0 \
  -sDYNAMIC_EXECUTION=0 -sDISABLE_EXCEPTION_CATCHING=0 -sEMIT_EMSCRIPTEN_LICENSE=1 \
  -sEXPORTED_FUNCTIONS='["_production_run","_production_experiment_run","_malloc","_free"]' \
  -sEXPORTED_RUNTIME_METHODS='["ccall"]' -o "$build/production-runtime.js"
if [[ "$runtime_only" == false ]]; then
  python3 "$project/package-html.py" "$build/production-runtime.js" "$sdk"
else
  printf 'Production runtime: %s\n' "$build/production-runtime.js"
fi
