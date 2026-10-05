<!-- SPDX-License-Identifier: MIT -->
# Production simulation development environment

Production uses the shared [GPU Debian development container](../../../docs/environment.md),
`post-profit-economics-gpu`, immutable ID
`d790ed5301151c20b084a703acf6f04472234deb7cd6fd2c8c5d0b218f46b40e`.
The native production engine remains CPU code; sharing this environment adds
no model dependency on CUDA or LibTorch and creates no separate container.

The canonical Windows checkout `C:\Work\post-profit-economics` binds directly
to `/workspace/praesidium-humanitatis`, preserving existing build-cache paths.
The container runs `/bin/bash` as root with NVIDIA GPU access, 1 GiB shared memory,
no named volumes or published ports, restart policy `no`, default bridge and no
privileged mode. Its local image preserves the installed TFT backup environment
on the pinned Debian 12 base; the authoritative guide records its image ID and
provenance. The CPU container and former Windows junction have been removed.

The optimizer launcher used below forwards to the shared TFT launcher. Both
verify the same immutable identity, image, labels, mount and GPU configuration,
reuse matching stopped containers and preserve mismatches. Build and execution
remain inside this container; do not fall back to Windows toolchains.

From repository-root PowerShell:

```powershell
.\tools\price-optimization\environment\container.ps1 status
.\tools\price-optimization\environment\container.ps1 up
.\tools\price-optimization\environment\container.ps1 -Action exec -Command @('cmake','-S','projects/post-profit-production','-B','.build/post-profit-production/native','-DCMAKE_BUILD_TYPE=Release')
.\tools\price-optimization\environment\container.ps1 -Action exec -Command @('cmake','--build','.build/post-profit-production/native','--parallel','2')
.\tools\price-optimization\environment\container.ps1 -Action exec -Command @('ctest','--test-dir','.build/post-profit-production/native','--output-on-failure','--no-tests=error')
.\tools\price-optimization\environment\container.ps1 -Action exec -Command @('.build/post-profit-production/native/production_simulation','--config','projects/post-profit-production/configs/research.cfg')
.\tools\price-optimization\environment\container.ps1 -Action exec -Command @('.build/post-profit-production/native/production_experiment','--plan','projects/post-profit-production/configs/scheduling-comparison.cfg','--format','markdown')
```

No `setup.sh` is added: the existing pinned GCC/CMake environment suffices.
The native project references the existing MIT `nlohmann/json` header without
linking any optimizer code. The native build needs no AMPL, solver, Python,
Node, LibTorch or CUDA.
Build artifacts remain under ignored `.build/post-profit-production/`.

For a root integration check inside that same environment, configure a separate
build with `-DPH_BUILD_PRODUCTION=ON`. The production target is optional and
disabled by default. The simulator performs no file writes; redirect stdout
explicitly if a local research result is to be retained. No resumable run,
automatic persistence or tamper-evident record service is implemented.

The [experiment runner](../experiments/README.md) uses this same build and
environment with no additional dependency. Its default output is a full JSON
report; `--format markdown` supplies an inspection view. Both write only to
stdout. A failed verification or consistency report exits with status `2`.
Verification-phase failures skip exploratory cases; a later consistency failure
retains the exploratory observations. Argument or input errors exit with status
`1`. The configuration-driven
plan does not permit shell commands, load plugins or create another container.

## Offline WebAssembly view

The web build uses the same managed container, with the already staged official
Emscripten **4.0.15** SDK at `.build/deps/emsdk-4.0.15`. Its compiler commit is
`09f52557f0d48b65b8c724853ed8f4e8bf80e669`, bundled Node is **22.16.0**, and the
container's Python is **3.11.2**. SDK provenance and dependency-only provisioning
are recorded in the [exchange environment guide](../../post-profit-exchange/environment/README.md).
Production adds no setup script, dependency installation or service. If the SDK
is absent, follow that documented procedure in the same container.

From repository-root PowerShell:

```powershell
.\tools\price-optimization\environment\container.ps1 -Action exec -Command @('bash','projects/post-profit-production/build-wasm.sh')
.\tools\price-optimization\environment\container.ps1 -Action exec -Command @('bash','--noprofile','--norc','-c','source .build/deps/emsdk-4.0.15/emsdk_env.sh >/dev/null 2>&1; node projects/post-profit-production/web/wasm_test.cjs .build/post-profit-production/wasm/production-runtime.js .build/post-profit-production/native/production_simulation .build/post-profit-production/native/production_experiment')
.\tools\price-optimization\environment\container.ps1 -Action exec -Command @('bash','--noprofile','--norc','-c','source .build/deps/emsdk-4.0.15/emsdk_env.sh >/dev/null 2>&1; node projects/post-profit-production/web/standalone_test.cjs projects/post-profit-production/dist/post-profit-production.html .build/post-profit-production/native/production_simulation .build/post-profit-production/native/production_experiment')
```

For runtime-only development, `build-wasm.sh --runtime-only` uses the same
compiler inputs and flags but omits packaging. After UI or documentation edits,
run `python3 projects/post-profit-production/package-html.py .build/post-profit-production/wasm/production-runtime.js .build/deps/emsdk-4.0.15`
inside the same container to refresh the HTML and its source archive.

The build compiles only the production simulation, experiment runner, thin C
exports and MIT JSON dependency. It packages the resulting runtime, UI, examples,
applicable notices and corresponding source into
`projects/post-profit-production/dist/post-profit-production.html`. WASM stack
size is 8 MiB and memory may grow to 512 MiB; browser memory constraints remain
separate from the model's configured bounds. The build does not run a web server
or publish a port. The completed page works as a local HTML file in a browser
that permits local files and WebAssembly workers.

The ignored `.build/post-profit-production/web-review/post-profit-production.html`
is a separate `noindex` local review copy. Its only CSP exception permits inline
style **elements** for Codex's review selector. Both entries prohibit inline
style attributes and script attributes, JavaScript eval and network fetches.
The release permits only its hashed app script and style, blob workers and WASM
compilation. Never export the review copy as the release or weaken the release
policy to accommodate the selector.
