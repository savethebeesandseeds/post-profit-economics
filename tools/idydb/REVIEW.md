# IdyDB adoption review

Updated on 2026-10-14 for the revised working-tree snapshot from
`C:\Work\idydb`, based on source commit
`7912afd007734adb08b018a18e3808131b3c3108`. The changes are uncommitted at snapshot
time; [provenance](PROVENANCE.md) identifies the imported file bytes.

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
| Open acquired the same file lock twice | Open acquires one lock, preserving the intended read-only/writer behavior. Native Windows locking behavior remains unvalidated. |
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
- Native Windows builds, locking, replacement and durability require separate
  validation. `idydb_open_at` and the current exchange storage adapter are POSIX
  and Linux integrations respectively.
- Deployment security, key handling, secure plaintext working storage on each
  platform, filesystem durability and backup restoration need validation in the
  actual operating environment. An embedding callback is caller-supplied.

## Verification scope

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
