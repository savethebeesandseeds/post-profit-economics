#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Application task runner; dependency provisioning is an explicit separate step.
set -euo pipefail
[[ $# == 3 && "$1" == /operation ]] || {
  echo 'Usage: run-service.sh /operation ENVIRONMENT_ID PORT' >&2; exit 2;
}
repo=/workspace/exchange-source
operation=$1
expected_id=$2
port=$3
[[ "$expected_id" =~ ^[a-z0-9]+(-[a-z0-9]+)*$ && ${#expected_id} -le 48 &&
   "$port" =~ ^[0-9]{4,5}$ && "$port" -ge 1024 && "$port" -le 65535 ]] || {
  echo 'Invalid environment identity or service port.' >&2; exit 2;
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
python3 - "$operation/exchange.cfg" "$expected_id" <<'PY'
import json, pathlib, sys
path = pathlib.Path(sys.argv[1])
if path.is_symlink():
    raise ValueError('Environment configuration cannot be a symlink.')
config = json.loads(path.read_text())
if config.get('schema_version') != 'exchange.environment.v1' or config.get('environment_id') != sys.argv[2]:
    raise ValueError('Environment configuration differs from the container identity.')
PY
build=/opt/post-profit-exchange/native
[[ "$(realpath -m -- "$build")" == "$build" ]] || {
  echo 'Build products must remain in the container filesystem.' >&2; exit 1;
}
cmake -S projects/post-profit-exchange -B "$build" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF -DPH_EXCHANGE_BUILD_ENVIRONMENT=ON
cmake --build "$build" --target exchange_environment --parallel 2
binary="$build/exchange_environment"
if ! "$binary" inspect "$operation" > /dev/null; then
  echo 'Existing environment could not be opened. Launch never initializes, resets or migrates its database.' >&2
  exit 1
fi
exec python3 projects/post-profit-exchange/code/interfaces/serve.py \
  --binary "$binary" --environment "$operation" \
  --page projects/post-profit-exchange/web/operations/transactions.html \
  --bind 0.0.0.0 --port "$port"
