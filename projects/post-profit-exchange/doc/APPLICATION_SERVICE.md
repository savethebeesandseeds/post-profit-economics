<!-- SPDX-License-Identifier: MIT -->
# Connected application service

The approved deployment direction is **one application container per operating
environment**, alongside the existing general development container.

The vendor example was provisioned and verified on 2026-10-14 at
[http://127.0.0.1:8766/](http://127.0.0.1:8766/), container ID
`1c7232e713cb2c7e8449a2b11bd7f5a15df4fc634f529cea8d8e7c15de5083a1`.
Setup verified the installed pins without downloads or package installation.
Source read-only access, the empty environment mask, the selected writable
operation, endpoint and unchanged opening database were checked.
Use `status` to check whether the container is currently running.

The application serves the shared operator interface and passes recorded
deliveries, sales and expenses to C++ accounting and IdyDB storage. The selected
[operating environment](OPERATING_ENVIRONMENT.md) supplies its configuration,
metadata and existing database. It contains no accounting or UI code.

A generated `reports/environment.html` is a snapshot. Recording transactions
uses the connected application at its configured loopback URL. No Python
service, compiler or database engine runs on Windows.

## Container per environment

The launcher reads `environment_id` from the selected `exchange.cfg`. IDs
must contain lowercase letters/digits separated by hyphens, at most 48
characters. For `vendor-operation`, the exact default configuration is:

| Setting | Value |
| --- | --- |
| Name | `post-profit-exchange-vendor-operation` |
| Local immutable image | `sha256:a12443bbcfb1d1acba2fbe63679b57422635dd6e88161cfaaf4554256fc1f9b4` |
| Provenance | Existing Linux amd64 recovery image; Debian 12 base `debian@sha256:6ebd97fa83deb272194a2cf015b3d26a4d538e9ad3a7a79d544c8af5b0a01443` |
| Command | `/bin/bash projects/post-profit-exchange/code/interfaces/run-service.sh /operation vendor-operation 8766` |
| User / working directory | `root` / `/workspace/exchange-source` |
| Read-only source bind | `C:\Work\post-profit-economics` → `/workspace/exchange-source` |
| Read/write operation bind | `C:\Work\post-profit-economics\projects\post-profit-exchange\code\environments\vendor-operation` → `/operation` |
| Empty read-only tmpfs | `/workspace/exchange-source/projects/post-profit-exchange/code/environments`, options `ro,nosuid,nodev,noexec,size=1m,mode=0555` |
| Named volumes | None |
| Published port | `127.0.0.1:8766` → container TCP `8766` |
| Restart / network | `no` / default bridge |
| GPU / other devices | None |
| Shared memory / privileged | 64 MiB / false |
| Interactive / TTY | false / false |
| Build, caches, inventories | Container filesystem under `/opt/post-profit-exchange` |
| Database | The environment's configured file under its writable `/operation` bind |
| Labels | Managed-by, project root, configuration `exchange-environment-v2`, environment path/id and recovery-source identity |

The source bind is read-only. The empty tmpfs hides all exchange environment
directories through that bind, while `/operation` exposes only the selected
environment for writes. The mask contains no unique data. Different operations
have separate databases, container writable layers and service ports. Shared
source remains common; this is operational separation, not a claim that the
entire repository is confidential to one operation.

The image already exists locally. The launcher checks its immutable identity
and [recovery provenance](../../../docs/environment.md) and never pulls or
replaces it. The fresh source mount target avoids its historical path symlink.
This recovered image is a local research dependency, not a portable vendor
installer.

The general `post-profit-economics-gpu` container remains available for shared
development, builds and tests. Its ID, mounts, command, GPU access and lifecycle
are unchanged. These environment containers do not require GPUs and do not
change the standalone simulator.

## Plan and initial provisioning

From repository-root PowerShell:

```powershell
.\projects\post-profit-exchange\code\interfaces\application-container.ps1 -Action plan
.\projects\post-profit-exchange\code\interfaces\application-container.ps1 -Action provision
```

`plan` verifies local image provenance and any same-named container, prints
the full configuration, and makes no changes. Before creating or starting a container, the launcher
checks running Docker publications and Windows TCP listeners. A validated
already-running instance may retain its own port. These checks preserve other
services; they do not reserve a free port against a later race. `provision` creates only an
absent managed container, or reuses the exact matching one. It starts the runner
and explicitly executes [code/setup.sh](../code/setup.sh) inside that container.

The runner waits for a verified dependency marker before building or serving.
Setup has **no action arguments, container lifecycle, build, test, run or data
migration commands**. It verifies Debian 12 amd64 and the existing pinned CPU
compiler, CMake and Python packages. Installed pins were checked against the
existing development environment. If all are installed, setup performs no
network access. If packages are missing, it installs their exact selected pins
with APT `--no-install-recommends`. An installed version mismatch fails rather
than silently upgrading or downgrading packages.

Setup keeps APT cache/list files, inventories and its marker under
`/opt/post-profit-exchange`. It atomically writes the marker only after package
and executable versions pass. The marker includes the setup script's hash.
Dependency changes therefore require explicit provisioning again; normal
startup never invokes setup or APT.

After provisioning, the runner builds only `exchange_environment` under
`/opt/post-profit-exchange/native`, verifies the mounted configuration's
environment ID, and inspects the existing accounts. It then starts the Python
standard-library transport inside the container. The launcher verifies the
container configuration and `/api/state` readiness. Launch never initializes,
resets, migrates or generates transactions in a database.

## Ordinary operation and a second environment

```powershell
.\projects\post-profit-exchange\code\interfaces\application-container.ps1 -Action up
.\projects\post-profit-exchange\code\interfaces\application-container.ps1 -Action status
.\projects\post-profit-exchange\code\interfaces\application-container.ps1 -Action logs
.\projects\post-profit-exchange\code\interfaces\application-container.ps1 -Action stop
```

`up` requires a previously provisioned container and preserves its data. An
absent container needs the explicit initial `provision` action. After source
changes, stop and start the same container to rebuild its application.
`logs` shows the last 100 log lines. Failure preserves the container and
records for inspection; no remove, prune, recreate or replacement action exists.

Another operation selects its existing environment directory and an unused
loopback port, for example:

```powershell
.\projects\post-profit-exchange\code\interfaces\application-container.ps1 -Action plan `
  -Environment projects/post-profit-exchange/code/environments/another-operation -Port 8767
```

This example requires that directory, its own configuration, metadata and
initialized database; the launcher does not create example records. The
container name comes from its configured ID. Use the same environment and port
for provisioning and every later lifecycle command. Host and container use the
same chosen port so the HTTP Host/origin checks remain valid. Ports range from
1024 through 65535 and bind only to host `127.0.0.1`.

Environment directories must be below the project's `code/environments/`
root. Absolute paths, traversal and reparse-point escapes are rejected. A
matching name with different labels, image, paths, command, mask, port, devices
or other validated configuration is preserved as a conflict. All operations
after inspection address the immutable container ID. Named volumes and other
containers are never deleted.

## Verification boundaries

Transport and UI tests run against C++ inside the general development container
with temporary labeled fixtures and internal loopback. Syntax and `plan`
checks do not create environment containers.

Successful startup proves that this instance can open the selected records and
serve its interface. It does not establish multi-user authorization, off-device
access, deployment portability or backup recovery. Record the actual new
container ID, verified mounts/mask, port and environment health after launch.

The [verification record](../../../docs/exchange-simulation-verification.md)
records launch checks and the resolved initial port conflict.
