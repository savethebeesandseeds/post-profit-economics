<!-- SPDX-License-Identifier: MIT -->
# Exchange simulation build environment

The standalone HTML runs locally without a server or installed compiler.
Building uses the shared [GPU Debian environment](../../../docs/environment.md),
`post-profit-economics-gpu`, immutable ID
`d790ed5301151c20b084a703acf6f04472234deb7cd6fd2c8c5d0b218f46b40e`.
The optimizer launcher forwards to its managed TFT launcher; no separate CPU
container is created. Exchange adds no mount, volume, port or service.

The canonical checkout `C:\Work\post-profit-economics` binds directly to
`/workspace/praesidium-humanitatis`. The recovered container preserves the
installed CUDA environment and enables NVIDIA GPUs, with 1 GiB shared memory,
no ports or named volumes, restart policy `no`, default bridge and no privileged
mode. Exchange's native and WebAssembly code retain their existing CPU behavior.
The CPU container, old TFT backup container and former host junction are gone.

Python and Emscripten 4.0.15 were already installed or staged and were verified
in the recovered container; no SDK installer was rerun during recovery.
The following provisioning record applies only when dependencies need explicit
restoration. Normal builds use the existing staged SDK.

`setup-wasm.sh` installs only dependencies: Debian `python3=3.11.2-1+b1`
with `--no-install-recommends`, and the official Emscripten SDK 4.0.15 into
ignored `.build/deps/emsdk-4.0.15`. It does not change shell startup files.
Transitive Debian package versions are recorded under
`.build/post-profit-exchange/environment/debian-packages.txt`; the pins are
not a complete historical Debian snapshot.

SDK archive: [official 4.0.15 release](https://github.com/emscripten-core/emsdk/releases/tag/4.0.15).
Observed and checked archive SHA-256:
`35be7626493e3bd22860ee2177147f9bca3b6ff871edeab27c5b061a9ed9d23d`.
The pinned SDK manifest resolves to
`sdk-releases-b412b6307e541b93dd93f01b61181e15c17302ec-64bit`, with
Node 22.16.0 and Emscripten compiler commit
`09f52557f0d48b65b8c724853ed8f4e8bf80e669`. The compiler and its bundled
standard libraries keep their licenses; relevant runtime notices are embedded
in the HTML alongside the pricing engine license and downloadable source.

From repository-root PowerShell:

```powershell
.\tools\price-optimization\environment\container.ps1 status
.\tools\price-optimization\environment\container.ps1 -Action exec -Command @('bash','projects/post-profit-exchange/environment/setup-wasm.sh')
.\tools\price-optimization\environment\container.ps1 -Action exec -Command @('bash','projects/post-profit-exchange/build-wasm.sh')
```

The normal launcher checks the existing container identity/configuration before
execution. If absent, follow its documented approval procedure instead of
creating an alternative container. Setup downloads dependencies; runtime
simulation in the finished HTML performs no network requests.

The build uses Emscripten's documented
[single-file packaging](https://emscripten.org/docs/tools_reference/settings_reference.html#single-file)
and [modularized output](https://emscripten.org/docs/compiling/Modularized-Output.html).
`build-wasm.sh` compiles the MIT simulator and reusable MIT exponential-smoothing tool plus the restricted C++ pricing core
and enumeration backend. `package-html.py` embeds that runtime, plain UI, full
license notices and a ZIP of corresponding source into
`dist/post-profit-exchange.html`. AMPL is neither compiled nor redistributed.
