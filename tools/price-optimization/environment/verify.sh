#!/usr/bin/env bash
# SPDX-License-Identifier: MIT AND LicenseRef-Praesidium-Humanitatis-Worker-Protection-1.0
# Copyright (c) 2026 Waajacu
# Extracted 2026-10-05 from environment/container.ps1; see NOTICE.md and LICENSE-MIT-TFT-LAUNCHER.
# Environment verification only; no dependency installation or container lifecycle.
set -euo pipefail
[[ $# -eq 0 ]] || { echo 'Usage: bash tools/price-optimization/environment/verify.sh (no arguments)' >&2; exit 1; }
source /etc/os-release
[[ "$ID" == debian && "$VERSION_ID" == 12 && "$(dpkg --print-architecture)" == amd64 &&
   "$(pwd)" == /workspace/praesidium-humanitatis && -d tools/price-optimization ]] || {
  echo 'The managed CPU environment must be Debian 12 amd64 at /workspace/praesidium-humanitatis.' >&2
  exit 1
}
