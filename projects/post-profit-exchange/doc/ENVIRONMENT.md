<!-- SPDX-License-Identifier: MIT -->
# Exchange development environment

The delivered standalone HTML runs locally without a server or installed
compiler. Building and testing use the shared
[GPU Debian environment](../../../docs/environment.md),
`post-profit-economics-gpu`, immutable ID
`d790ed5301151c20b084a703acf6f04472234deb7cd6fd2c8c5d0b218f46b40e`.
The optimizer launcher forwards to its managed TFT launcher; no separate CPU
container is created by that development launcher. This shared container remains
available for whole-project work. Connected vendor operations use the separate
[per-operation application containers](APPLICATION_SERVICE.md): shared application
source is read-only, and each container writes only its selected environment.

The canonical checkout `C:\Work\post-profit-economics` binds directly to
`/workspace/praesidium-humanitatis`. The recovered container preserves the
installed CUDA environment and enables NVIDIA GPUs, with 1 GiB shared memory,
no ports or named volumes, restart policy `no`, default bridge and no privileged
mode. Exchange's native and WebAssembly code retain their existing CPU behavior.
The CPU container, old TFT backup container and former host junction are gone.

## Native build and checks

Keep compilers, SDKs, dependency caches and build products inside the managed
container filesystem. The repository bind mount is for source and intentional
deliverables; ignored directories on that mount are still Windows-host storage.
Do not install a Windows toolchain or create a substitute container.

From repository-root PowerShell, inspect and reuse the managed container:

```powershell
.\tools\price-optimization\environment\container.ps1 -Action plan
.\tools\price-optimization\environment\container.ps1 -Action status
.\tools\price-optimization\environment\container.ps1 -Action up
.\tools\price-optimization\environment\container.ps1 -Action shell
```

The launcher checks the existing container identity and configuration. If it is
absent or mismatched, follow the environment's documented procedure instead of
inventing another definition. Inside the container, from
`/workspace/praesidium-humanitatis`:

```sh
cmake -S projects/post-profit-exchange -B /tmp/post-profit-exchange/native -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/post-profit-exchange/native --parallel 2
ctest --test-dir /tmp/post-profit-exchange/native --output-on-failure --no-tests=error
/tmp/post-profit-exchange/native/exchange_simulation --example-config
```

This builds the enumeration-based exchange simulator, the native Linux
`exchange_environment` command with its IdyDB adapter, and their tests.
The [operating-environment walkthrough](OPERATING_ENVIRONMENT.md) describes the
separate operation directories those commands consume. For a simulator-only
CMake build, set `-DPH_EXCHANGE_BUILD_ENVIRONMENT=OFF`; the WASM simulator does not
link IdyDB. This build does not enable the separate native AMPL backend. See the
[verification record](../../../docs/exchange-simulation-verification.md) for
the latest checks and their scope. The temporary build directory is disposable
build output, not a location for unique operating records. For persisted runs,
follow the [file contract](FILES.md) and select a new output path.

If the container was stopped before this work and no other task is using it,
restore that state with the launcher's `-Action stop`. Do not delete or replace it.

## WebAssembly build and packaging

The existing HTML opens without a compiler. To rebuild it inside the managed
container, use the same pinned Linux SDK, **Emscripten 4.0.15**.
The exchange build uses these container-local paths:

| Item | Default |
| --- | --- |
| SDK | `/opt/post-profit-exchange/emsdk-4.0.15` |
| Download and compiler caches | `/opt/post-profit-exchange/cache/` |
| Native build (commands above) | `/tmp/post-profit-exchange/native/` |
| WebAssembly intermediate output | `/tmp/post-profit-exchange/wasm/` |
| Dependency inventory | `/opt/post-profit-exchange/environment/` |
| Final, intentional repository deliverable | `projects/post-profit-exchange/web/dist/post-profit-exchange.html` |

The existing pinned Linux SDK was copied from its historical repository staging
location into the managed container for this cleanup. The original staged copy
was preserved; no vendor package or new container was downloaded. Routine builds
do not rerun setup. If provisioning is needed, `code/setup-wasm.sh` installs only
the documented dependencies and configures the environment; it does not manage
container lifecycle or run project tasks.

After the native build, run these commands from the repository root inside the
container:

```sh
bash projects/post-profit-exchange/web/build-wasm.sh
node_bin=/opt/post-profit-exchange/emsdk-4.0.15/node/22.16.0_64bit/bin/node
"$node_bin" projects/post-profit-exchange/web/tests/wasm_test.cjs \
  /tmp/post-profit-exchange/wasm/exchange-runtime.js \
  /tmp/post-profit-exchange/native/exchange_simulation
"$node_bin" projects/post-profit-exchange/web/tests/standalone_test.cjs \
  projects/post-profit-exchange/web/dist/post-profit-exchange.html \
  /tmp/post-profit-exchange/native/exchange_simulation
```

`EXCHANGE_EMSDK` and `EXCHANGE_WASM_BUILD_DIR` can select other container-local
SDK/build locations. The scripts reject workspace-mounted SDK, build and cache
locations. Temporary build output must not hold unique operating records.

The packaging pipeline uses Emscripten's
[single-file packaging](https://emscripten.org/docs/tools_reference/settings_reference.html#single-file)
and [modularized output](https://emscripten.org/docs/compiling/Modularized-Output.html).
`web/build-wasm.sh` compiles the MIT simulator and exponential-smoothing tool plus
the separately licensed C++ pricing core and enumeration backend.
`web/package-html.py` embeds the runtime, interface, license notices and
corresponding-source ZIP into the final HTML. AMPL is neither compiled nor
redistributed. Packaged sources include the declared configuration and example
histories, but exclude private runs, imported histories and generated output.

Native/WASM parity and packaged-worker checks verify the executable and file
boundaries. They do not establish visual browser correctness.

## Recorded compiler provenance

The build is pinned to Emscripten **4.0.15**. Its
[official SDK release](https://github.com/emscripten-core/emsdk/releases/tag/4.0.15)
archive was checked against SHA-256
`35be7626493e3bd22860ee2177147f9bca3b6ff871edeab27c5b061a9ed9d23d`.
The pinned manifest resolves to
`sdk-releases-b412b6307e541b93dd93f01b61181e15c17302ec-64bit`, with
Node 22.16.0 and Emscripten compiler commit
`09f52557f0d48b65b8c724853ed8f4e8bf80e669`.

The dependency script pins Debian `python3=3.11.2-1+b1` with
`--no-install-recommends`; new dependency inventories belong at
`/opt/post-profit-exchange/environment/debian-packages.txt`. The former
`.build/post-profit-exchange/environment/debian-packages.txt` remains a historical
record, not a current build location.
These pins are not a complete historical Debian snapshot. The compiler and
bundled standard libraries retain their licenses; runtime notices accompany
the pricing-engine license and downloadable source in the HTML.
