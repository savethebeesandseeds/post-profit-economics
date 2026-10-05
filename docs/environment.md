# Shared GPU development environment

TFT, price optimization, production and exchange use one managed container:
`post-profit-economics-gpu`. On 2026-10-05, the user requested recovery from the
installed TFT backup, removal of the CPU container, and deletion of the backup
after verification. The recovery preserved the installed CUDA environment;
no dependency installer or large toolchain download was run.

## Container configuration

| Setting | Value |
| --- | --- |
| Container name | `post-profit-economics-gpu` |
| Immutable container ID | `d790ed5301151c20b084a703acf6f04472234deb7cd6fd2c8c5d0b218f46b40e` |
| Local recovered image | `post-profit-economics-gpu:recovered-2026-10-05` |
| Immutable image ID | `sha256:a12443bbcfb1d1acba2fbe63679b57422635dd6e88161cfaaf4554256fc1f9b4` |
| Original base | `debian@sha256:6ebd97fa83deb272194a2cf015b3d26a4d538e9ad3a7a79d544c8af5b0a01443` (Debian 12, Linux amd64) |
| Command / user | `/bin/bash`, interactive and TTY / root |
| Bind mount / working directory | `C:\Work\post-profit-economics` to `/workspace/praesidium-humanitatis`, read/write / that directory |
| Named volumes / published ports | None / none |
| Restart policy | `no` |
| GPU / other devices | All NVIDIA GPUs / none |
| Shared memory | 1 GiB |
| Network / privileged | Default bridge / false |
| Ownership labels | `org.post-profit-economics.managed-by=post-profit-economics`, canonical project root |
| Configuration labels | Configuration `1`, LibTorch `2.6.0+cu124-cxx11`, immutable recovery-source ID |

The snapshot came from the stopped container
`fb76bcc496b41c383fd4363cb5c2af15a877076beb4254d2af957dab6eba4b24`, formerly
`praesidium-humanitatis-tft-before-path-migration`. That backup and
`post-profit-economics-cpu` have now been deleted by explicit user instruction.
Only the GPU container remains for this project. The old Windows paths
`C:\Work\praesidium-humanitatis` and `C:\Work\Ad-humanitatem` are absent.

The local image manifest is `.build/environment/gpu-recovery.json`. The launcher
pins the immutable image ID and checks its recovery-source and base-image
provenance, container identity, labels, command, canonical bind, GPU request,
ports and other configuration before mutation. It reuses a matching stopped
container and preserves mismatches. If the local image or manifest is missing,
it fails rather than silently starting an empty Debian environment or installing
dependencies. This local snapshot is a recovery artifact, not a portable
reproducible package or a published image.

The snapshot inherited a Linux symlink from the September path migration.
`environment/normalize-bind.sh` corrects that inspected symlink inside the
container; the launcher restarts the same container only when needed to apply
the corrected bind target. The Windows checkout is never copied or linked.
Both tool launchers use this shared GPU container. The optimizer entry forwards
to the TFT launcher and cannot recreate a separate CPU environment.

CUDA execution was verified on the NVIDIA RTX A2000 8GB Laptop GPU: the existing
TFT executable passed all 11 CUDA checks. GCC 12.2, CMake 3.25.1, Python 3.11.2,
CUDA toolkit 12.4.1, cuDNN 9.26.0.51, LibTorch 2.6.0+cu124 and the staged
Emscripten 4.0.15 / Node 22.16.0 executables were checked. Existing native checks
passed except one deadline assertion on the initial pass; that test passed its
targeted rerun. See [verification](verification.md) for the recorded limits.

Ignored `.temp/` migration scripts and receipts are historical records. They
must not be rerun to recreate former names or paths. Host source, SDK bundles,
model checkpoints and existing build artifacts were retained.

## Dependency provenance

The base image, CUDA installation procedure and package versions come from the
inspected local reference `C:\Work\cuwacunu\cuwacunu_embedding` at commit
`ded843a`. The reference includes `setup.sh`, `container.ps1`, `dependencies.lock`
and `README.md`. It uses a pinned Debian 12 base, CUDA toolkit 12.4.1, and system
cuDNN 9.26.0.51. This project retains that reference's package list, including
its development/profiling tools; it is a development environment, not a minimal
runtime image. CMake is added for this project's build:
[cmake 3.25.1-1](https://packages.debian.org/bookworm/cmake) and
[cmake-data 3.25.1-1](https://packages.debian.org/bookworm/cmake-data).

`setup.sh` only installs dependencies and configures/verifies the environment.
All APT installations use `--no-install-recommends`. Exact package versions are
requested and verified; missing versions fail instead of silently choosing newer
ones. Debian/NVIDIA repositories are live, so the lock is not an offline archive
or a guarantee that those packages remain downloadable. Base-image packages and
additional transitive dependencies are recorded in `.build/environment/debian-packages.txt`.
Package upgrades should be a reviewed lock update, followed by model validation.

## Stage LibTorch

The required Linux x86-64 bundle is LibTorch `2.6.0+cu124`, C++11 ABI. Stage an
independent copy of the existing bundle at `.build/deps/libtorch`; the source is
`C:\Work\cuwacunu\cuwacunu_embedding\.external\libtorch`. No host-wide library
installation is needed. Preserve the bundle's license files when copying or
redistributing it. Do not replace another existing staged bundle without review.

The installer checks `build-version`, the C++11 ABI flag and required library
files before installing packages. LibTorch's bundled CUDA/cuDNN runtime libraries
take priority over system libraries through `/etc/profile.d/praesidium-humanitatis.sh`.
CUDA stub libraries must never be placed on the runtime library path.

## Provision and verify

The recovered environment already contains its installed dependencies and staged LibTorch. Run these commands from the project root in PowerShell. Dependency installation is an explicit separate operation; do not rerun setup merely to start this container:

```powershell
.\tools\temporal-fusion-transformer\environment\container.ps1 plan
.\tools\temporal-fusion-transformer\environment\container.ps1 up
.\tools\temporal-fusion-transformer\environment\container.ps1 -Action exec -Command @('bash', 'tools/temporal-fusion-transformer/tasks.sh', 'test')
.\tools\temporal-fusion-transformer\environment\container.ps1 -Action exec -Command @('bash', 'tools/temporal-fusion-transformer/tasks.sh', 'test-cuda')
.\tools\temporal-fusion-transformer\environment\container.ps1 -Action exec -Command @('bash', 'tools/temporal-fusion-transformer/tasks.sh', 'demo', '--device', 'cuda', '--steps', '80')
```

Docker Desktop must be using Linux containers with NVIDIA GPU support. Setup
verifies the package pins, `ldd` resolution, `nvcc`, CMake, and visible GPUs;
model CPU/CUDA behavior is verified separately by the tests. On an installation
or verification failure, preserve the container and inspect the error.

The task runner uses `cmake -S tools/temporal-fusion-transformer -B .build/temporal-fusion-transformer` and
`-DCMAKE_PREFIX_PATH=/workspace/praesidium-humanitatis/.build/deps/libtorch`.
`test-cuda` enables `PRAESIDIUM_HUMANITATIS_CUDA_TESTS=ON` and runs all tests ending in `_cuda`;
`test` runs all tests ending in `_cpu`, including the CLI continuation workflow.
Build parallelism defaults to two jobs; override with the
positive integer environment variable `PRAESIDIUM_HUMANITATIS_BUILD_JOBS` when memory permits.
Build products stay under `.build/`; setup downloads stay under `.temp/`.
The first build can spend several minutes reading LibTorch's headers through the
Windows bind mount. A quiet compiler log during that stage does not necessarily
mean compilation has stalled; inspect compiler activity before interrupting it.

Other lifecycle commands are `status`, `shell`, and `stop`. Arbitrary `exec`
commands do not automatically source shell profiles; `tools/temporal-fusion-transformer/tasks.sh` does.
For direct CUDA tooling use `/usr/local/cuda-12.4/bin/nvcc` or an interactive shell.
`setup.sh` deliberately accepts no action arguments and performs no build,
training, testing, or container lifecycle operations.

## Isolated original TFT reference

The replication harness uses `setup-reference.sh` in the same approved container.
It installs Python 3.7.16 and the pinned TensorFlow 1.15.5 CPU reference under
`.build/reference-runtime`, without changing system Python, LibTorch or container
lifecycle. The Miniconda installer is checksum-pinned; dependency versions are in
`tools/temporal-fusion-transformer/environment/reference-requirements.txt`. No conda channel operations or shell
initialization are used. Download caches remain under `.temp/reference-runtime`.

```powershell
.\tools\temporal-fusion-transformer\environment\container.ps1 -Action exec -Command @('bash', 'tools/temporal-fusion-transformer/environment/setup-reference.sh')
```

Setup checks dependency consistency and executes a small TensorFlow session.
The resolved package list is `.build/verification/replication/reference-freeze.txt`.
Model comparison, data preparation, and training are separate operations described
in the [replication harness](../tools/temporal-fusion-transformer/replication/README.md). The original reference
runs on CPU; the C++ comparison additionally runs on CUDA with TF32 disabled.
