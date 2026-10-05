<!-- SPDX-License-Identifier: LicenseRef-Praesidium-Humanitatis-Worker-Protection-1.0 -->

# Price optimization in the shared GPU environment

The user requested removal of `post-profit-economics-cpu` on 2026-10-05.
The optimizer now uses the recovered shared container `post-profit-economics-gpu`,
immutable ID `d790ed5301151c20b084a703acf6f04472234deb7cd6fd2c8c5d0b218f46b40e`.
Its [authoritative environment guide](../../../docs/environment.md) records
the recovered Debian image, exact mounts, labels and verified NVIDIA access.
There are no named volumes or published ports; restart policy is `no`.
The canonical Windows root is `C:\Work\post-profit-economics`; it binds directly
to `/workspace/praesidium-humanitatis`. The old Windows folder and junction are gone.

`environment/container.ps1` forwards lifecycle and exec actions to the TFT
launcher. It cannot create a separate CPU container. Both entry points verify
and reuse the same managed GPU container, including when stopped; mismatches
are preserved. Native optimizer and simulation targets retain their existing
CPU behavior while running in this shared environment.

The launcher, setup and task scripts adapt MIT TFT material. Their inherited
portions retain MIT permission, with the complete original notice preserved in
[LICENSE-MIT-TFT-LAUNCHER](LICENSE-MIT-TFT-LAUNCHER). New copyrightable optimizer
modifications use the [Worker Protection License](../LICENSE).
[NOTICE.md](NOTICE.md) records the inherited files and later forwarding change.

From repository-root PowerShell:

```powershell
.\tools\price-optimization\environment\container.ps1 status
.\tools\price-optimization\environment\container.ps1 up
.\tools\price-optimization\environment\container.ps1 -Action exec -Command @('bash', 'tools/price-optimization/tasks.sh', 'test')
.\tools\price-optimization\environment\container.ps1 -Action exec -Command @('bash', 'tools/price-optimization/tasks.sh', 'demo')
```

The recovered environment already has the compiler, CMake, Python and CUDA
packages. No dependency installer was run during recovery. `setup.sh` remains
the historical dependency-only CPU procedure; it is not needed to launch or
use this shared environment. Explicit dependency changes require checking the
shared TFT package lock and environment, rather than applying a CPU lock over
it. AMPL, its API and solver remain separately staged under their own terms.

## AMPL and solver staging

The official API 3.2.0 SDK, AMPL interpreter 20260809, and HiGHS 1.15.1 driver
20260813 are now staged locally. Provenance and observed archive hashes are in
[`ampl-sdk.lock.json`](ampl-sdk.lock.json) and
[`ampl-runtime.lock.json`](ampl-runtime.lock.json). The interpreter reports the
vendor-bundled Demo license, with maintenance through 2027-01-31. No account or
license activation was needed for these small synthetic evaluation cases.
Vendor binaries and notices remain in ignored `.build/deps/`.

The Demo license has model-size and use restrictions. Its presence does not
establish permission for operational store pricing; obtain suitable rights
before that deployment. See [licensing](../../../docs/licensing.md).

AMPL, its C++ API and solver binaries are external dependencies under their own terms. `setup.sh` does not download or activate them. Obtain an authorized Linux amd64 runtime, C++ API and suitable solver, verify the vendor-provided integrity information, and stage them as follows:

```text
.build/deps/ampl/         # AMPL executable, authorized solver and runtime files
.build/deps/amplapi/      # C++ API include/ and lib/ directories
```

Configure the applicable license using AMPL's own instructions; do not commit license files, activation credentials or vendor binaries. No license or binary redistribution rights are supplied by this repository. The default build and tests validate the C++ pricing constraints without AMPL. Running an actual optimization requires the staged API, runtime, solver and a license permitting that use.

Inside the configured container:

```bash
bash tools/price-optimization/tasks.sh test
bash tools/price-optimization/tasks.sh demo
# With authorized dependencies staged:
bash tools/price-optimization/tasks.sh demo-ampl
bash tools/price-optimization/tasks.sh test-ampl
```

`PH_PRICE_WITH_AMPL=ON` enables the API for `configure`, `build` and `test`. `test-ampl` explicitly registers and runs the licensed integration test; regular `test` clears that registration. Override the SDK directory with `AMPLAPI_ROOT` and runtime directory with `PH_AMPL_BINARY_DIR`. `PH_AMPL_SOLVER` must be `highs`: only that driver's status handling is supported in this version. This is configuration, not a bundled solver or a promise that the installed license supports it. Build concurrency defaults to two jobs and can be set with `PRAESIDIUM_HUMANITATIS_BUILD_JOBS`.

The [dashboard backend](../docs/backend.md) runs as a local JSON Lines process
in this same container. It adds no exposed port, mount, service or installed
package. Its vendored JSON header retains MIT terms. `test-ampl` also tests this
backend through actual AMPL success, infeasibility, timeout and cancellation.

Use `container.ps1 shell` for an interactive shell and `container.ps1 stop` to stop this exact managed container. These actions preserve its writable layer and mounted files.
