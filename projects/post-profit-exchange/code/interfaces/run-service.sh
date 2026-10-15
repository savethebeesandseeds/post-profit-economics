#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Application task runner; dependency provisioning is an explicit separate step.
set -euo pipefail
[[ $# == 1 && "$1" == /operation/exchange.cfg ]] || {
  echo 'Usage: run-service.sh /operation/exchange.cfg' >&2; exit 2;
}
repo=/workspace/exchange-source
operation=/operation
config=$1
key=/run/secrets/exchange.key
[[ -f "$config" && ! -L "$config" && -f "$key" && ! -L "$key" && -s "$key" ]] || {
  echo 'Configuration and key must be existing ordinary mounted files.' >&2; exit 1;
}
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)
[[ "$(cd -- "$script_dir/../../../.." && pwd -P)" == "$repo" &&
   -d "$operation" && ! -L "$operation" ]] || {
  echo 'Run through the per-environment container with /operation mounted.' >&2; exit 1;
}
cd -- "$repo"
setup=projects/post-profit-exchange/code/setup.sh
expected_setup=$(sha256sum "$setup" | cut -d ' ' -f 1)
marker=/opt/post-profit-exchange/environment/dependencies.ready
if [[ ! -f "$marker" || "$(cat "$marker")" != "$expected_setup" ]]; then
  echo 'Waiting for explicit provisioning: use application-container.ps1 -Action provision.'
  trap 'exit 0' TERM INT
  until [[ -f "$marker" && "$(cat "$marker")" == "$expected_setup" ]]; do sleep 2; done
  trap - TERM INT
fi
build=/opt/post-profit-exchange/native
[[ "$(realpath -m -- "$build")" == "$build" ]] || {
  echo 'Build products must remain in the container filesystem.' >&2; exit 1;
}
cmake -S projects/post-profit-exchange -B "$build" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DPH_EXCHANGE_BUILD_ENVIRONMENT=ON
cmake --build "$build" --target exchange_server exchange_environment --parallel 2
# Configuration supplies identity and port; the key override is a mounted file
# path only. Key bytes are never passed through arguments or environment variables.
# Startup never initializes, resets or migrates stored records.
exec "$build/exchange_server" \
  --config "$config" --key-file "$key" \
  --page projects/post-profit-exchange/web/operations/transactions.html \
  --bind 0.0.0.0
