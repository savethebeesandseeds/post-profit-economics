#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Adapt the preserved profiles to the current bind; no installation or lifecycle.
set -euo pipefail
[[ $EUID == 0 && "$PWD" == /workspace/praesidium-humanitatis ]]
cat > /etc/profile.d/praesidium-humanitatis.sh <<'PROFILE'
export CUDA_VERSION=12.4
export CUDNN_VERSION=9
case ":$PATH:" in
  *:/usr/local/cuda-12.4/bin:*) ;;
  *) export PATH="/usr/local/cuda-12.4/bin:$PATH" ;;
esac
case "${LD_LIBRARY_PATH:-}" in
  /workspace/praesidium-humanitatis/.build/deps/libtorch/lib:/usr/local/cuda-12.4/lib64*) ;;
  *) export LD_LIBRARY_PATH="/workspace/praesidium-humanitatis/.build/deps/libtorch/lib:/usr/local/cuda-12.4/lib64${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ;;
esac
PROFILE
if [[ -f /etc/profile.d/ad-humanitatem.sh ]]; then
  printf '%s\n' 'source /etc/profile.d/praesidium-humanitatis.sh' > /etc/profile.d/ad-humanitatem.sh
fi
profile_line='source /etc/profile.d/praesidium-humanitatis.sh'
grep -Fqx "$profile_line" /root/.bashrc || printf '\n%s\n' "$profile_line" >> /root/.bashrc
cat > /etc/profile.d/praesidium-price-optimization.sh <<'PROFILE'
export AMPLAPI_ROOT=/workspace/praesidium-humanitatis/.build/deps/amplapi
export PH_AMPL_BINARY_DIR=/workspace/praesidium-humanitatis/.build/deps/ampl
PROFILE
