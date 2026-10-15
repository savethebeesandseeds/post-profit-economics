<!-- SPDX-License-Identifier: MIT -->
# Connected application service in Debian

This document describes the existing container deployment. The separate
[native Windows application](WINDOWS_NATIVE.md) will run the C++ server directly
without Docker or WSL for vendors. IdyDB's Windows libraries now exist, and a
small MinGW consumer linked successfully against the BIG static library and
OpenSSL. That executable was not run, and the complete application has not been
validated or packaged. Pinning the dependency package, aligning the storage
contract and completing Windows/NTFS acceptance remain the next steps. The
native work does not replace or migrate the live service.

Each operation uses one application container and one `exchange.cfg`, alongside
the existing general development container. The configured example is
`environments/vendor_0x01/`. Its settings select the workspace, loopback port and
a separate encryption key file. The Windows launcher starts the Debian service;
the C++ server, accounting and IdyDB engine run inside that container.

The executable under `code/launcher/dist/` is the existing container launcher,
not the native application being prepared. It uses this checkout and its documented
local image.
It is not yet a standalone installer: Docker Desktop, the checkout and the
verified Debian image remain prerequisites. The [verification record](../../../docs/exchange-simulation-verification.md)
records actual launch evidence; use `status` to check the current instance.

The C++ executable `exchange_server` uses cpp-httplib to serve the shared
operator interface. It calls the same environment functions as the native
commands. The selected [operating environment](OPERATING_ENVIRONMENT.md)
supplies configuration, metadata and existing accounts; it contains no
accounting or UI implementation. The service has no Python runtime or subprocess
bridge. Python remains a development test and simulator-packaging dependency.

## Launch the container service with one configuration

From repository-root PowerShell, after initial provisioning:

```powershell
.\projects\post-profit-exchange\code\launcher\dist\post-profit-exchange.exe --config .\projects\post-profit-exchange\environments\vendor_0x01\exchange.cfg
```

The default action is `up`. The launcher opens the loopback interface only after
readiness succeeds. It also accepts `--action plan`, `provision`, `status`,
`stop` and `logs`. It calls the authoritative
[application-container.ps1](../code/interfaces/application-container.ps1);
it does not implement a second Docker lifecycle.

`exchange.cfg` uses strict JSON. Its existing identity, currency and record
paths remain; `exchange.environment.v2` adds:

```json
"runtime": {
  "workspace": ".",
  "server": {"bind": "127.0.0.1", "port": 8766},
  "encryption": {
    "required": true,
    "key_file": "%USERPROFILE%/.post-profit-exchange/keys/vendor_0x01.key"
  }
}
```

This is a fragment of the complete [example configuration](../environments/vendor_0x01/exchange.cfg).
`workspace` resolves relative to the configuration file, or may be absolute.
The configuration must be named `exchange.cfg` in that workspace. The current
launcher requires a direct child of the project's `environments/` directory,
with the directory name equal to `environment_id`. IDs contain lowercase
letters/digits separated by underscores or hyphens, at most 48 characters.

The launcher resolves the explicit `%USERPROFILE%` prefix in `key_file`.
The resulting ordinary file must be outside the checkout and environment.
Runtime fields are not part of the persisted accounting configuration and are
not returned by the accounts API. Changing a runtime setting does not rewrite
transactions. A changed Docker mount or port can nevertheless conflict with an
existing container; the launcher preserves that container and reports the
conflict instead of replacing it automatically.

## Key and database

The key file holds a generated random **symmetric secret**, used through IdyDB's
built-in AES-256-GCM encryption and PBKDF2-HMAC-SHA256 key derivation. It is not an
asymmetric private key or a worker identity. Its format is 64 hexadecimal
characters (32 random bytes), with an optional trailing newline. Preserve those
characters exactly; generated keys use lowercase. Configuration
contains its location, never its contents. The container receives the file as a read-only bind at
`/run/secrets/exchange.key`. Secret bytes are not supplied in process arguments,
environment variables, HTTP responses or application logs.

Required encryption refuses a missing/wrong key or a plaintext database. Normal
startup never converts a database, generates a replacement key or initializes
accounts. Preserve the key separately from the encrypted files: losing it loses
access to them. Recovery and key-rotation workflows remain work to establish.

Encryption covers the IdyDB database and the explicit migration recovery
database. It does not encrypt configuration, input JSON, an intentionally
exported report, browser requests/outbox or other historical copies. The Linux adapter
uses a private tmpfs directory for bounded ciphertext copies; IdyDB handles its
decrypted working stream in memory on this Linux path. Plaintext and secrets necessarily exist in
the running process. This is not protection against control of the host or
running container, nor a claim about host swap or core dumps.

The Windows integration must use the supplied IdyDB library for encryption,
decrypted working storage and database durability. Exchange does not port those
internals, replace the database or fall back to plaintext. The native dependency
and its required evidence are recorded in [WINDOWS_NATIVE.md](WINDOWS_NATIVE.md).
The Windows backend uses a private temporary file for decrypted working storage;
the Linux memory-only description above does not apply to that backend.

## Application interface

The server owns HTTP handling, session checks and the shared page. The
environment interface owns validation and replay; operations own accounting,
and the storage adapter owns IdyDB access. Startup creates no business records.

| Route | Result |
| --- | --- |
| `GET /` | Shared operator interface from `web/operations/transactions.html`. |
| `GET /api/session` | A session token for this running server. |
| `GET /api/state` | Current recorded accounts for this environment. |
| `POST /api/transactions` | Record a delivery, cash sale or new expense through the native accounting path. |

Local Host, origin and cross-site checks remain. Transaction requests require
the current token, JSON and at most 16 KiB. Raw JSON reaches the strict native
parser, so duplicate keys and invalid amounts remain errors. Retry an uncertain
submission with the original event identity and body.

The service has four HTTP workers, at most eight waiting connections, and one
accounting worker with at most four queued jobs. Native calls are serialized.
Socket timeouts are ten seconds. An accounting response waits twenty seconds;
expiry reports uncertain completion while an accepted job may still finish.
It does not cancel or roll back the write. A full job queue returns HTTP 503.
These bounds do not establish production capacity or availability.

SIGTERM/SIGINT stop accepting connections and drain accepted work. External
storage blockage can delay shutdown; forced termination does not prove whether
a transaction committed. HTTP access/body logging is disabled and third-party
stdout diagnostics are suppressed. The token is a local request safeguard,
not worker authentication. Remote-access deployment is not configured.

The vendored [cpp-httplib v0.61.0](../code/third_party/cpp-httplib/PROVENANCE.md)
supplies HTTP under its MIT license. Builds use the pinned local source.

The container invokes:

```sh
exchange_server --config /operation/exchange.cfg --key-file /run/secrets/exchange.key \
  --page projects/post-profit-exchange/web/operations/transactions.html --bind 0.0.0.0
```

Configuration supplies identity and port. The old `--environment`,
`--environment-id` and `--port` interface remains for explicit legacy test
fixtures. Docker publishes the service only on host loopback. Malformed HTTP
may be rejected before routing; a malformed/duplicate Host can receive HTTP 400
rather than the application's HTTP 403.

## Exact container configuration

For the checked-in `vendor_0x01` settings:

| Setting | Value |
| --- | --- |
| Name | `post-profit-exchange-vendor_0x01` |
| Local immutable image | `sha256:a12443bbcfb1d1acba2fbe63679b57422635dd6e88161cfaaf4554256fc1f9b4` |
| Provenance | Existing Linux amd64 recovery image; Debian 12 base `debian@sha256:6ebd97fa83deb272194a2cf015b3d26a4d538e9ad3a7a79d544c8af5b0a01443` |
| Command | `/bin/bash projects/post-profit-exchange/code/interfaces/run-service.sh /operation/exchange.cfg` |
| User / working directory | `root` / `/workspace/exchange-source` |
| Read-only source bind | `C:\Work\post-profit-economics` → `/workspace/exchange-source` |
| Read/write operation bind | `C:\Work\post-profit-economics\projects\post-profit-exchange\environments\vendor_0x01` → `/operation` |
| Read-only key bind | Resolved configured key file → `/run/secrets/exchange.key` |
| Empty read-only tmpfs mask | `/workspace/exchange-source/projects/post-profit-exchange/environments`, options `ro,nosuid,nodev,noexec,size=1m,mode=0555` |
| Named volumes | None |
| Published port | `127.0.0.1:8766` → container TCP `8766` |
| Restart / network | `no` / default bridge |
| GPU / other devices | None |
| Shared memory / privileged | 64 MiB / false |
| Interactive / TTY | false / false |
| Build, caches, inventories | Container filesystem under `/opt/post-profit-exchange` |
| Labels | Managed-by, project root, configuration `exchange-environment-v3`, environment path/id and recovery-source identity |

The source mask hides the exchange environment directory. The completed folder
migration removes its historical location. Only the chosen workspace is exposed
at `/operation`. Shared source remains
common; this does not make the entire repository confidential to one operation.

The launcher checks the local image's immutable identity and
[recovery provenance](../../../docs/environment.md). It never pulls or replaces
it. The general `post-profit-economics-gpu` container retains its existing
lifecycle, mounts and GPU access; environment services require no GPU.

## Provisioning, development and ordinary operation

The executable and PowerShell entry points use the same actions:

```powershell
.\projects\post-profit-exchange\code\interfaces\application-container.ps1 -Action plan -Config projects/post-profit-exchange/environments/vendor_0x01/exchange.cfg
.\projects\post-profit-exchange\code\interfaces\application-container.ps1 -Action provision -Config projects/post-profit-exchange/environments/vendor_0x01/exchange.cfg
```

`plan` inspects image provenance, any matching container, port publications and
Windows listeners, then prints the configuration without mutation. These checks
do not reserve a port against a later race. `provision` creates only an absent
managed container or reuses an exact match, then explicitly runs
[code/setup.sh](../code/setup.sh) inside it.

Setup only verifies/installs reproducible dependencies and configures the
environment. It has no lifecycle, build, test, run or migration actions. Missing
APT pins use `--no-install-recommends`; installed mismatches fail. Dependency
markers, caches and inventories remain inside the container. Normal startup
never invokes setup or APT.

The runner waits for the matching setup marker, builds `exchange_server` and
`exchange_environment` under `/opt/post-profit-exchange/native`, then starts
the server against the existing accounts.
The launcher checks container configuration and API readiness. `up` requires a
previously provisioned container. `stop` and `logs` operate on the inspected
immutable ID. After source edits, stop and start the same matching container to
rebuild. No action removes, recreates or replaces a container automatically.

To add an operation, supply its own initialized workspace and configuration,
including an unused port and separate key, then select that cfg with `--config`
or `-Config`. Supported host ports are 1024–65535, bound to `127.0.0.1`. Host and
container port numbers match for HTTP origin validation. Reparse-point escapes,
conflicting image/labels/mounts/ports/devices and same-named unmanaged containers
are rejected and preserved.

The existing Windows container-launcher executable is cross-compiled inside the
managed Debian development container. `code/launcher/setup.sh` installs pinned cross-compiler dependencies;
`code/launcher/build.sh` builds to `/opt/post-profit-exchange/launcher`. Copy only
the finished executable to the ignored `code/launcher/dist/` directory. No
compiler, SDK or package cache belongs on the Windows host. Native Windows
application builds and packages belong to `code/windows/`; they do not reuse this
Docker-wrapper executable as their application binary.

## Migration and verification boundaries

Moving the old environment and encrypting it is an explicit offline migration,
separate from launch. See [OPERATING_ENVIRONMENT.md](OPERATING_ENVIRONMENT.md#explicit-environment-migration).
It verifies the original and migrated accounts and retains an encrypted copy of
the original persisted envelope. Container replacement is a separate approved
operation after checking data and recovery; normal launch never performs it.

Native/HTTP/UI tests use temporary labeled fixtures in the general development
container. Startup readiness proves that this instance can open its records and
serve the interface. It does not establish worker authorization, off-device
access, portable deployment or a complete backup and recovery procedure.
