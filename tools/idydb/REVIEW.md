# IdyDB adoption review

Updated on 2026-10-15 for committed and pushed source revision
[`11dfda017a4b5163357d991421ab06ff75ebdf98`](https://github.com/savethebeesandseeds/IdyDB/commit/11dfda017a4b5163357d991421ab06ff75ebdf98).
It includes the storage backends, portable SDK and code-review corrections.
[Provenance](PROVENANCE.md) identifies the exact imported Git-blob bytes.

## Decision

Keep IdyDB as an optional experimental reusable tool, disabled in the aggregate
build by default. The exchange operating environment already uses a bounded JSON
text-envelope adapter. Preserve that adapter's exact integer representation,
schema, writer serialization, conflict checks and replay contract when updating
the shared library. The changes below address the defects in the original
review; they do not establish financial certification or complete operational
recovery.

## Corrected findings

References are to [the imported implementation](upstream/impl/db.cpp),
[public header](upstream/include/db.h) and [regression suites](upstream/tests/).

| Original finding | Revised behavior and evidence |
| --- | --- |
| Encrypted save could truncate the previous backing file and destroy the handle on failure | Complete ciphertext is staged and synchronized before replacement. Save failures retain the handle; `idydb_flush` retries and `idydb_discard` releases it. Persistence and failure suites cover authentication, save errors, retry and cleanup. |
| Plaintext writes lacked atomic commit and reliable error reporting | Insert, replacement and deletion stage complete sibling images. Flush/close propagate persistence errors and retry directory sync after a visible replacement. Failure tests exercise short writes, disk exhaustion, synchronization failures and interruptions around replacement. |
| Text/vector upsert performed separate writes | `idydb_rag_upsert_text` commits the pair in one snapshot. Failure tests check that a partial pair is never published. This does not add general multi-call transactions. |
| Read-only next-row scans misread text/vector headers, and the highest row wrapped bookkeeping | A shared bounds-checked decoder handles stream and mapped records. Core and query suites cover mixed payloads, full columns, index boundaries and mapped next-row scans. |
| Parsing trusted unchecked record lengths and dimensions | Opens validate complete record structure before mapping; malformed and truncated records are rejected. Core and query suites exercise malformed records; core tests cover maximum payloads. Fuzzing remains a separate gate. |
| Debug logging printed stored values by default | Debug logging is disabled by default and encrypted values are redacted. Logging still requires deployment policy and controlled build flags. |
| Open acquired the same file lock twice | Open acquires one lock, preserving the intended read-only/writer behavior. Platform tests now exercise real child-process writer/reader contention. Native Windows results belong to the standalone project's recorded validation. |
| Atomic library replacement conflicted with the Linux adapter's pinned paths and inodes | POSIX plaintext `idydb_open_at` owns a duplicate directory descriptor and uses directory-relative creation, replacement and synchronization. It rejects basename symlinks; read-only descriptor-path opens support old unlinked inodes. Directory regression tests and exchange adapter tests validate the supported Linux integration. |

The original import's recorded disposable probes demonstrated the next-row and
boundary defects. Their results describe that earlier revision; the revised
regression tests supersede them as evidence for current behavior.

## Remaining limits and adoption gates

- Atomic replacement can become visible before a directory-sync error is
  reported. Callers must check the retained handle or reopen the record before
  retrying a business operation. Discard cannot undo a published replacement.
- The library has no SQL/schema layer, general multi-call transaction API,
  write-ahead log or automatic backup/recovery procedure. It cannot certify an
  accounting system. The exchange adapter's envelope replacement and revision
  checks remain project responsibilities.
- Primitive cells are `int` and `float`. The exchange adapter's bounded JSON text
  contract preserves its exact 64-bit amounts. Empty strings are stored as text;
  use `idydb_delete` to remove a cell. Other adapters must define null and encoding
  semantics.
- Each update copies the database. Search scans all candidate vectors. Maximum
  practical size, realistic latency, concurrent deployment load and performance
  remain unmeasured; full fuzzing and prolonged crash/recovery campaigns remain
  unperformed.
- Do not share a handle concurrently without caller serialization. Advisory
  locks cover cooperating processes; they do not establish safety against
  unrelated tools or a hostile filesystem.
- Replacement requires write access to the directory and changes the inode.
  POSIX owner, group and permission bits are retained, but ACLs, extended
  attributes and hard-link aliases need a separate deployment policy and tests.
- The format uses native integer/float encoding and assumes compatible byte
  order and primitive sizes. No portability migration is supplied.
- Native Windows behavior has its own backend and standalone validation scope.
  This repository's snapshot refresh validates its Linux integration; it does
  not validate the consuming application's separate Windows implementation.
  `idydb_open_at` remains a POSIX-only API.
- Deployment security, key handling, secure plaintext working storage on each
  platform, filesystem durability and backup restoration need validation in the
  actual operating environment. An embedding callback is caller-supplied.

## Verification history: 2026-10-14 hardening snapshot

Consumer validation uses the existing `post-profit-economics-gpu` Linux development
container and its installed compiler, CMake and OpenSSL. The standalone library
matrix runs in its own existing `idydb-dev` container. No host build or toolchain
installation is imported. The local wrapper and
`idydb_roundtrip` check are retained; the imported library suites include core,
query, persistence, C interoperability, fault injection and pinned-directory
coverage. The production library excludes the fault hooks.

| Check | Result |
| --- | --- |
| Compiler, CMake and OpenSSL versions | Consumer container: GCC 12.2.0, CMake 3.25.1, OpenSSL 3.0.20. Standalone container: GCC 14.2.0, CMake 3.31.6, OpenSSL 3.5.7. |
| Standalone wrapper configure/build and CTest | Debug configure/build passed; 7/7 CTest entries passed, including the retained roundtrip and six shared suites. |
| Sizing, shared/mmap and sanitizer configurations exercised | Standalone `idydb-dev`: 30/30 suite runs passed across big, tiny, small, shared/mmap-off and ASan+UBSan. Strict compiler warnings, normal/failure Node suites and production fault-hook exclusion passed. |
| Exchange operating-environment integration tests | All four environment/store/transaction suites passed in the combined build, including real HTTP-to-native transaction submission, persistence, replay, stale revisions and competing clients. |
| Aggregate build with IdyDB enabled | Debug configure/build with `PH_BUILD_IDYDB=ON`, `PH_BUILD_EXCHANGE=ON`, `PH_BUILD_TFT=OFF` passed; 22/22 CTest entries passed. |
| Default aggregate configure with IdyDB disabled | Fresh default Debug configure passed; `PH_BUILD_IDYDB=OFF` and `PH_BUILD_EXCHANGE=OFF` remain the defaults. |
| Source/import file checksums | All 12 mapped files match the standalone source byte-for-byte; SHA-256 recorded in PROVENANCE.md. |
| Standalone simulator source export | Temporary export passed ZIP CRC and exact source-byte checks. Pinned Node 22.16.0 passed the complete WASM/UI/native parity, replay, failure and packaged-file suite. |
| Existing vendor application | Same container `1c7232e713cb2c7e8449a2b11bd7f5a15df4fc634f529cea8d8e7c15de5083a1` rebuilt the current source and serves HTTP 200 at the interface and `/api/state` on loopback port 8766. Existing database SHA-256 remains `0b0bb11475fc7a2dad9558c9f65e612525f0900db83a9cca90048786473c72bd`. |

These results apply to the recorded snapshot and tested Linux environment. The
standalone project's normal and failure-instrumented Node tests also passed;
the addon and standalone CI configuration remain outside this import. Passing
synthetic regression tests does not prove real vendor outcomes, security,
unlimited scale or complete recovery.

## 2026-10-15 backend refresh

The shared engine now delegates OS storage operations to `platform_posix.cpp`
or `platform_windows.cpp` through a private interface. The existing record
encoding and the Linux pinned-directory API remain in place. `idydb_alloc`
lets an embedding callback allocate a vector in the same library that later
releases it. Installed C/C++ consumer tests include that callback contract.

Additional shared regressions cover UTF-8 and long filenames, real subprocess
lock contention and interrupted commits, fixed plaintext/encrypted encoding
fixtures, and independent pathname verification after a failed mutation and
discard. Windows-specific tests cover protected DACLs, reparse points and
alternate-stream rejection; those tests do not run as Windows tests in this
repository's Linux development container.

The source export includes only the two explicitly named synthetic database
fixtures, whose public passphrase and hashes are documented with the fixtures.
Operational databases, keys, generated dependencies and Windows binaries remain
outside the source archive. No existing application database is migrated or
rewritten by the snapshot update.

Consumer validation uses the existing managed `post-profit-economics-gpu`
container (`d790ed5301151c20b084a703acf6f04472234deb7cd6fd2c8c5d0b218f46b40e`),
GCC 12.2.0, CMake 3.25.1 and OpenSSL 3.0.20. Fresh build/install directories
are under `/tmp/post-profit-idydb-backend-refresh*`; no application service was
restarted and no host toolchain was installed.

| Backend refresh check | Result |
| --- | --- |
| Shared snapshot checksums | All 25 mapped files match the committed source Git blobs byte-for-byte; hashes are recorded in PROVENANCE.md. |
| Wrapper Debug build and CTest | Passed 9/9 tests: the eight shared suites and the unchanged local roundtrip test. |
| Installed package consumers | Passed 2/2 C/C++ consumer tests, including an encrypted reopen and a C embedding callback allocating its vector through `idydb_alloc`. |
| Aggregate Linux integration | Fresh Debug build with `PH_BUILD_IDYDB=ON`, `PH_BUILD_EXCHANGE=ON`, `PH_BUILD_TFT=OFF` passed 27/27 tests, including native HTTP transactions, encrypted environment persistence and server checks. |
| Temporary standalone source export | Pinned Node 22.16.0 passed WASM/UI/native parity, replay, failure and packaged-source checks. The Python packager and archive tests verify both fixed fixture hashes; only a temporary export under `/tmp` was written. |
| Standalone native Windows validation | The source project passed 40/40 executable checks across big static, tiny static, small static and big shared configurations; four Linux-to-Windows plaintext/encrypted pair reads and one Windows-to-Linux pair write/read passed. Its normal and failure Node suites passed with Node 24.19.0 x64. These were container-cross-compiled artifacts executed on Windows NT 10.0.26300.0, local C: NTFS. |

The Windows directory suite checks the expected unsupported status for the
POSIX-only `idydb_open_at` API. The symbolic-link creation fixture conditionally
skipped after Windows returned privilege error 1314; it does not establish
native symlink-rejection coverage on this host. Other native platform, crash,
protected-DACL and long-path checks passed. The final native runtime receipt is
`C:\Work\idydb\build\windows-runtime\run-20261015T084330Z-21686-host-639276506682005275`.
These standalone results do not validate the consuming application's separate
Windows implementation or arbitrary network filesystems.

## 2026-10-15 code-review follow-up

The review identified and corrected three concrete gaps in the initial backend
refresh:

- Appending a staging suffix to the database basename could reject an otherwise
  valid filename near the filesystem's component-length limit. Both backends
  now use bounded random staging basenames in the same directory. The platform
  suite covers valid 218, 219, 244, 245 and 255-character names and persistence
  across updates.
- Windows could omit the original `NOT_CONTENT_INDEXED` attribute when creating
  a staged replacement. The backend now applies the preserved attributes
  explicitly through the staged handle before publication; the native platform
  regression checks that attribute across replacement.
- The installed static target omitted runtime/math link dependencies required
  by a genuine C-only consumer. The exported target now supplies them, and the
  consumer fixture can configure a project with only C enabled. The earlier
  mixed C/C++ fixture could conceal this gap by selecting a C++ linker.

The file format, public APIs, consumer schemas and existing records are
unchanged. The source/import mapping remains 25 files; provenance hashes identify
the final reviewed bytes. The initial refresh's broader aggregate and Windows
receipts above remain dated evidence for that initial snapshot; targeted
follow-up checks apply to these review corrections.

| Review follow-up check | Result |
| --- | --- |
| Consumer wrapper regression suite | Rebuilt against the reviewed sources; 9/9 tests passed, including the new long-basename cases. |
| Genuine C-only installed consumer | Fresh `IDYDB_CONSUMER_C_ONLY=ON` configuration enabled only C, linked with the C driver and passed 1/1 encrypted allocator-callback test. |
| Shared source provenance | Exactly six changed shared files were recopied after checking all 25 previous import hashes. The final import is pinned to the committed source Git blobs; all 25 hashes are recorded in PROVENANCE.md. |
| Final standalone review-fix matrix | Linux passed 40 suite runs plus 15 installed-consumer checks, including five genuine C-only projects and ASan/UBSan. Windows passed 40/40 executable checks across four configurations; each mode used the genuine C-only consumer. Four Linux-to-Windows plaintext/encrypted pair reads and one Windows-to-Linux pair read passed. Normal and failure Node suites passed on both operating systems. |

These checks ran in the existing managed Linux consumer container with GCC
12.2.0 and OpenSSL 3.0.20. The initial aggregate suite was not repeated for this
targeted review follow-up. Existing distribution HTML, local wrapper and
roundtrip sources, operational records and running services were preserved.
The final standalone Windows review-fix runtime receipt is
`C:\Work\idydb\build\windows-runtime\run-20261015T091202Z-24031-host-639276523858117613`.
The native Windows API and symbolic-link privilege limits recorded above still
apply.

## Committed source pin

The standalone repository committed and pushed this reviewed implementation as
`11dfda017a4b5163357d991421ab06ff75ebdf98`. All 25 imported files match that
commit's raw Git blobs. Only the imported README and license needed CRLF-to-LF
normalization to match the committed bytes; their text and all code remained
unchanged. The existing validation receipts still apply, so the build matrix
was not repeated for this metadata and line-ending update.
