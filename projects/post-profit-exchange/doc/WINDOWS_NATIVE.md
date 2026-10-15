# Native Windows application

## Status

The exchange-owned application has a Windows implementation and an isolated
cross-build. IdyDB now provides a Windows backend and x64 MinGW static/shared
SDKs. A consumer using all seven database functions required by exchange linked
successfully with exchange's existing compiler on 15 October 2026.

The remaining work is to identify and package a completed SDK, finish the
exchange dependency contract, and run application acceptance on native Windows.
The link-check executable was not run and was not the full exchange server.
No runnable native vendor release or release receipt has been produced.

The existing Debian application, live `vendor_0x01` environment, encrypted records,
browser interface, accounting and economic model remain in place. The Docker
launcher in `code/launcher` remains available. Its executable is not overwritten
by native Windows build output. AMPL subprocess portability is outside this phase.

## Vendor entry point

Once SDK integration and acceptance checks are complete, the packaged entry
point is:

```powershell
.\post-profit-exchange.exe --config C:\Exchange\vendor\exchange.cfg
```

The executable itself hosts the C++ HTTP server. It reads the central configuration,
opens the existing environment and its external encryption key, binds the configured
loopback port, then opens the existing interface in the default browser.
Vendors do not need Docker, WSL, a compiler or Python. The package includes
`web/transactions.html` beside the executable; page resolution is relative to the
executable, not the shell's working directory. `--no-browser` supports testing or
a deliberate manual browser launch.

The initial native target is 64-bit Windows 10 or later on a fixed local NTFS
volume. The pinned cpp-httplib release sets the Windows baseline. Network shares,
removable filesystems and Windows reparse paths are refused by the initial storage
adapter. Database, configuration and key paths must pass their respective checks.
Startup never initializes or replaces a missing database.

The central v2 `exchange.cfg` retains the existing fields:

- `runtime.workspace` selects the operation directory, relative to the configuration
  file when it is relative.
- `runtime.server.bind` is `127.0.0.1` for the native application.
- `runtime.server.port` selects the local port.
- `runtime.encryption.required` remains `true`.
- `runtime.encryption.key_file` locates the separate 64-hex-character secret.
  The existing `%USERPROFILE%` prefix is supported without running a shell.

Runtime settings are excluded from the stored business configuration and account
responses. Changing a port or moving the external key is not an accounting
migration. Secret contents are not supplied as process arguments, environment
variables, HTTP responses or log messages.

The key is opened using Windows handles and checked for regular-file identity,
bounded size, hard links, reparse paths and a private access-control list. Current
user, LocalSystem and Administrators are the trusted principals. The key remains
outside the environment. The application does not silently relax permissions or
rewrite existing ACLs to make a rejected path work.

Ctrl+C requests an orderly stop, closes the listener and drains queued accounting
work. Windows can forcibly end a process after a console-close or system-shutdown
deadline, and an abrupt exit is not a successful commit acknowledgement. The
existing retry-with-the-same-event-ID behavior still applies after an uncertain
response. These controls do not replace the supplied library's persistence
guarantees.

## Responsibility boundary

| Exchange application | Supplied Windows IdyDB library |
| --- | --- |
| Config/record validation, integer accounting and idempotent events | Database format and compatibility |
| External key-file policy and private path handling | Encryption and authentication |
| Stable Windows reader/writer lock and exact-envelope comparison | Decrypted working storage and its protection |
| Calling the existing public C API and checking every return status | Encrypted publication, flush/close and crash-durability semantics |
| Refusing missing keys or unsafe paths; reporting uncertain writes | Correct Windows path encoding, file sharing and cleanup behavior |

The Windows adapter publishes through IdyDB's encrypted operation. It does not
create an exchange-owned decrypted file or implement a second encryption format.
A successful close must mean what the supplied library documents; an error may
have occurred after publication, so the application does not invent a rollback.
A stale expected envelope is rejected under the application lock before writing.

## Available IdyDB SDK and compatibility review

The standalone source is maintained at `C:\Work\idydb`; the reviewed source
snapshot also exists under `tools/idydb/upstream` in this repository. The
[provider's Windows storage contract](../../../tools/idydb/upstream/README.md#windows-storage-contract)
defines its guarantees. This compatibility review did not change or reassess
the general quality of the database.

The existing `idydb-dev` container contains:

| SDK input | Container path |
| --- | --- |
| BIG static header and library | `/build/windows-big-static/install/include/db.h` and `/build/windows-big-static/install/lib/libidydb.a` |
| BIG shared SDK | `/build/windows-big-shared/install`, including `lib/libidydb.dll.a` and `bin/libidydb.dll` |
| Static OpenSSL Crypto | `/build/windows-deps/lib/libcrypto.a` |

The shared SDK supplies matching `libgcc_s_seh-1.dll`, `libstdc++-6.dll` and
`libwinpthread-1.dll` runtime dependencies and licenses. These are provider
artifacts, not files installed into the exchange application.

IdyDB's build uses MinGW GCC 14; exchange currently uses MinGW GCC 12. A bounded
consumer link with the existing exchange compiler, the BIG static IdyDB archive
and static Crypto succeeded and produced an x64 PE executable. This establishes
link compatibility for the recorded artifacts, not execution of the exchange
application or compatibility of every later provider build. Input hashes and
the exact test boundary are recorded in the
[verification notes](../../../docs/exchange-simulation-verification.md#2026-10-15-idydb-windows-compatibility-review).

Source inspection confirmed the C API and text capacity, UTF-8/Unicode/long-path
handling, file sharing with our retained inspection handles, initialization into
our reserved empty file, permission preservation and discard behavior required
by the adapter. The provider has native Windows test logs and Linux/Windows
format fixtures. Its native symlink fixture reported missing creation privilege;
those results do not replace the exchange's own NTFS acceptance checks.

Windows decrypted working storage belongs to IdyDB. It uses a private,
owner-only, delete-on-close scratch file that can reach disk. Neither exchange
nor this review assumes RAM-only storage, secure erasure or a universal
power-loss guarantee. The provider documents a flushed complete file, atomic
replacement and another file flush, rather than POSIX parent-directory sync.

The provider source and build outputs were changing during review. Select an
identified, completed SDK and its matching evidence before release integration;
do not treat old logs as certification of a newer source tree.

## IdyDB package handoff

Prepare an exchange dependency directory from the completed SDK containing the
public `db.h`, an x86_64 MinGW-compatible static
archive (`.a`), or an import archive plus its DLL, all runtime dependencies, licenses,
and documented Windows test evidence. Build-time OpenSSL dependencies must be
supplied explicitly if the library needs them; this project never fetches a
replacement library. A DLL built with another compiler is usable only with a
compatible C ABI and MinGW import archive. An arbitrary MSVC static `.lib` is not
accepted by this build.

Name its manifest `exchange-idydb-windows.json` using the example at
`code/windows/idydb-package.example.json`. The manifest pins SHA256 for the header,
library, support archives, runtime DLLs, licenses and evidence. It selects BIG
sizing, matching the existing public API's `idydb_column_row_sizing` and text-cell
capacity. Import libraries set `IDYDB_USE_DLL`; static ones do not.

The adapter uses these unchanged public symbols:

```text
idydb_open_encrypted
idydb_extract
idydb_retrieved_type
idydb_retrieve_char
idydb_insert_const_char
idydb_close
idydb_discard
```

The package must explicitly document and attest:

1. UTF-8 Windows paths, including spaces, Unicode and long paths, use the intended
   file without code-page substitution.
2. Encrypted read-only opens authenticate and reject plaintext. A failed open and
   its discarded handler never modify the file.
3. The existing encrypted format remains compatible, including Linux-created
   encrypted records. No implicit conversion is used by the Windows application.
4. Decrypted working storage is protected by the library; its implementation and
   cleanup guarantees are the library owner's responsibility.
5. Writable open does not publish or mutate data before a requested write. A
   whole-envelope encrypted write is atomically published by the library, retains
   restrictive ACLs, and checks its Windows flush/close operations.
6. A failure after publication reports an uncertain outcome. Discard releases
   resources without retrying an unacknowledged write.
7. Windows sharing and locking are compatible with pinned ancestor handles and
   the adapter's read/attribute checks and separate stable lock. The library
   cannot rely on exclusive access incompatible with these documented callers.

Hash checks establish which files were supplied. They do not prove these
assertions or replace application integration tests.

The existing `checked_windows_flush_and_close` manifest field remains
unconfirmed. Its wording must be reconciled with the provider's exact documented
commit contract: checked persistence flushes do not establish that every
resource-teardown close error is propagated. Do not mark the field true merely
to pass validation, or weaken protections as part of documentation cleanup.

The next exchange integration steps are:

1. Pin the completed header, BIG library, Crypto or DLL dependencies, licenses
   and matching evidence in `exchange-idydb-windows.json`.
2. Add the provider's declared `gdi32` system-link dependency to the exchange
   allowlist and verify the selected artifact's complete dependency set. The
   allowance is not implemented by this documentation update.
3. Resolve the manifest wording above without assuming stronger guarantees than
   the provider supplies.
4. Link the actual server and CLI, then run the native security, encrypted-store
   and HTTP/accounting acceptance tests before generating a vendor package.

## Build and independent checks

All development builds and executions in this phase use the managed Debian
container. `code/windows/setup.sh` reuses the pinned launcher MinGW dependencies
and adds Debian Wine64/libwine `8.0~repack-4` with `--no-install-recommends`.
It performs dependency work only. `build.sh` handles configure, build and tests.

The isolated CMake project does not enter the AMPL build or compile IdyDB sources.
Without an explicit `PH_EXCHANGE_IDYDB_PACKAGE`, it compiles actual Windows objects
against the existing public header and creates only independent test executables.

Verified on 15 October 2026: the complete exchange-owned Windows object build
passed. Wine executed the unchanged 112-check exact-accounting test, server
platform checks, 45 configuration/key/report checks, Ctrl+C handling and exact
readiness-output separation. The final run had six CTest cases pass and one
filesystem acceptance case skip; the build/package gate case contains 18 checks.
Manifest validation tests reject altered hashes,
missing files, path traversal, symbolic links, incomplete encryption guarantees,
incompatible sizing, missing runtime DLLs and injected link options.

The additional Windows filesystem-security test exercises ACL, path, file-identity
and stable-lock checks. Wine cannot establish all NTFS behavior: it does not enforce
the directory DELETE-sharing case and does not create the required genuine
Windows reparse fixture in this setup. These cases are explicitly skipped, and
CTest marks that acceptance case skipped. The native acceptance script refuses
any skipped security check. Its native test account must be able to create a real
symlink/reparse fixture; missing that permission produces a skip and no release
receipt. This is an acceptance-test prerequisite, not a requirement for vendors
to run the application as administrator or change Windows developer settings.
No such settings are changed by this work. Wine passing other component tests
is not evidence of Windows filesystem durability or a completed encrypted
application.

## Integration and packaging gate

`code/windows/integration_test.ps1` is prepared for an approved Windows NTFS test
environment after the identified SDK is integrated. It creates a new private temporary fixture,
uses fixed test-only keys outside that fixture's operation directory, and leaves
the live environment untouched. It runs the security test without skips and the
real encrypted-storage test, then initializes through the actual encrypted
application, records deliveries, a sale and an expense, reopens the database,
checks exact accounts, wrong-key preservation, duplicate retry and stale-revision
rejection, then starts the real HTTP server, posts and retries another sale, and
gracefully stops it before reopening the resulting accounts. It writes a receipt
only if every required step passes.

The deterministic worked account starts with 100000 minor units of cash and
45 bread units at acquisition cost 100. It receives 10 more at cost 120, sells
50 at 200 and pays a 700 expense. Expected ending cash is 108100, earned funding
4200, stock 5 units worth 600, obligations 1000 and net assets 107700.
The HTTP case sells one additional unit at 200: cash becomes 108300, earned
funding 4280, stock 4 units worth 480 and net assets 107780. Retrying that same
HTTP event must leave every amount unchanged. These are synthetic accounting
checks, not economic or market evidence.

The receipt binds the server, CLI, native security and encrypted-storage tests,
and supplied IdyDB manifest hashes, the tested interface hash and the exact runtime
DLL hash set. Acceptance verifies the DLLs beside the tested executables against
the provider manifest; packaging refuses a different DLL or page. `package.py`
requires this receipt, validates the current dependency again, checks PE64
architecture and all imported DLLs, and packages
only an explicit list of files. It excludes the development/test executables,
live `vendor_0x01` inputs, databases, reports and keys. The included `new_vendor`
sample is uninitialized synthetic input and cannot be mistaken for an existing
operation; normal server startup refuses its missing database.

The full Windows integration test, real NTFS acceptance, cross-platform encrypted
reopen, crash/failure durability and a vendor distribution remain pending.
No runnable native server or release ZIP is produced in compile-only mode.
The current container-only development policy also means the prepared native
acceptance script is not authorization to run project services or compile tools
on the host. Resolve that test environment before native application acceptance.
