#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Correct the inherited Linux bind target; no installation or container lifecycle.
set -euo pipefail
path=/workspace/praesidium-humanitatis
if [[ -L "$path" ]]; then
  [[ "$(readlink -- "$path")" == /workspace/ad-humanitatem ]] || {
    echo 'Unexpected Linux bind-target symlink; preserved.' >&2; exit 1;
  }
  unlink -- "$path"
  mkdir -- "$path"
  printf 'restart-required\n'
else
  [[ -d "$path" ]] || { echo 'Linux bind target is missing.' >&2; exit 1; }
  printf 'ready\n'
fi
