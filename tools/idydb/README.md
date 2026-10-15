# IdyDB (experimental)

IdyDB is an MIT-licensed C++17 library with a C API for sparse, file-backed
cells and vectors. It stores integers, floats, text, booleans and float vectors,
supports filtered nearest-neighbor search, and offers encrypted backing files.
Prefer the upstream CMake target, which selects the storage backend. Direct
Linux builds compile `upstream/impl/db.cpp` and
`upstream/impl/platform_posix.cpp` and include `upstream/include/db.h`;
OpenSSL Crypto is required for every build, including plaintext use.

This tool remains optional and experimental. The revised shared implementation
corrects the parsing and persistence defects in the original import and adds
regression and failure tests. Passing those tests does not certify financial
records, deployment security or recovery procedures. See the [review](REVIEW.md)
for the tested scope and remaining limits.

## Source and maintenance

The [upstream directory](upstream/) contains the standalone project from
committed and pushed revision
[`11dfda0`](https://github.com/savethebeesandseeds/IdyDB/commit/11dfda017a4b5163357d991421ab06ff75ebdf98),
including its regression suites, original README and complete
[MIT license](upstream/LICENSE). [Provenance](PROVENANCE.md) records all 25
committed-file mappings and checksums. The imported files match the raw Git
blobs at that revision; the shared implementation has no local patches.

Corrections and additions to shared database code belong in the standalone
project and this imported snapshot, with matching tests and documentation. Review
both copies and preserve unrelated changes. Repository-specific wrappers,
adapters, economic schemas and migrations belong in the consuming project.

The upstream README describes standalone development, including its separate
container and Node addon. This outer README provides the build commands for this
repository. The standalone launcher, setup scripts, Node addon and runtime data
are not imported.

## Build and validation

Use the existing [managed Linux development container](../../docs/environment.md),
`post-profit-economics-gpu`. Its C/C++ compiler, CMake and OpenSSL development
files supply the required dependencies. Build products stay inside the container;
no host build or new toolchain installation is needed.

From the repository root in that container:

```sh
cmake -S tools/idydb -B /tmp/post-profit-idydb-build \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build /tmp/post-profit-idydb-build --parallel 2
ctest --test-dir /tmp/post-profit-idydb-build --output-on-failure --no-tests=error
```

From PowerShell, run each command through the existing launcher, for example:

```powershell
.\tools\temporal-fusion-transformer\environment\container.ps1 -Action exec -Command @('cmake', '-S', 'tools/idydb', '-B', '/tmp/post-profit-idydb-build', '-DCMAKE_BUILD_TYPE=Debug', '-DBUILD_TESTING=ON')
```

The aggregate build enables this tool with `-DPH_BUILD_IDYDB=ON` (default: `OFF`).
The exchange operating-environment target also brings in the library when that
target is enabled. `ph::idydb` aliases the upstream `idydb` CMake target and
propagates its include path, OpenSSL dependency and sizing definitions. Link that
target so the consumer and library use the same sizing mode.

The installed-package fixture in `upstream/tests/consumer` also supports
`-DIDYDB_CONSUMER_C_ONLY=ON`. It configures with only the C language enabled and
checks that the exported static target supplies its runtime and math link
dependencies without requiring the application to select a C++ linker.

The wrapper retains the local `idydb_roundtrip` test alongside eight imported
suites: core, query, persistence, C API, failure, directory, platform and record
compatibility. Fixed synthetic plaintext and encrypted fixtures check the
existing file encoding; their passphrase is public test data. Installed C and
C++ consumer sources also accompany the snapshot. Fault injection and the
private backing-image snapshot helper use a separate instrumented test library;
both are absent from the production target.
Executed configurations and results are recorded in the [review](REVIEW.md).

## Persistence and API behavior

The file format is unchanged. Each plaintext insert, replacement or deletion
stages a complete sibling file, synchronizes it and atomically replaces the
backing file. `idydb_rag_upsert_text` stages its text/vector pair together.
Encrypted mutations update a private working image; explicit `idydb_flush` or
`idydb_close` saves the complete encrypted image by the same replacement pattern.

Check every persistence result. Failure before replacement preserves the previous
image. A directory-sync failure can return an error after a complete replacement
is visible; retry flush/close or reopen and inspect the authoritative record.
Failed saves retain the handle for inspection and retry. `idydb_discard` releases
the handle and lock without saving pending encrypted changes; it cannot undo an
already published replacement. It also releases a handle retained after failed
open. Borrowed extracted values expire on the next mutation, extract or release.

POSIX plaintext consumers can use `idydb_open_at` with a directory descriptor
opened with `O_RDONLY | O_DIRECTORY` and a single filename. The handle owns a
duplicate of the descriptor and keeps file creation, replacement and directory synchronization
relative to that pinned directory. Basename symlinks are rejected. Read-only
opens through `/proc/self/fd` can read pinned old inodes after replacement, as
required by the Linux exchange adapter.

The shared snapshot also contains a separate native Windows storage backend,
UTF-8 pathname support and `idydb_alloc` for allocator-safe embedding callbacks
across library boundaries. This refresh validates this repository's Linux
integration; the standalone project records its own Windows validation. It
does not change or establish support for the consumer application's Windows
storage, server or packaging code. `idydb_open_at` remains POSIX-only.

Updates copy the whole database and require temporary space proportional to its
size. Atomic replacement requires directory write access and changes the file's
inode. POSIX owner, group and permission bits are retained; hard-link aliases,
ACLs and extended attributes are not preserved as a group. Interrupted writes
can leave temporary siblings. File and directory sync guarantees depend on the
filesystem. The library has no general multi-call transaction API or write-ahead
log. Serialize shared handle access across threads; advisory locks coordinate
cooperating processes.

## Project boundary

The [exchange operating environment](../../projects/post-profit-exchange/doc/OPERATING_ENVIRONMENT.md)
already uses a project-owned adapter to store a bounded, versioned JSON envelope
in a text cell. That contract preserves exact bounded 64-bit monetary integers;
the library's native `int` and `float` cells do not supply that representation.
The adapter owns writer serialization, expected-revision checks, envelope
validation, replay and replacement of the authoritative database. Updating this
snapshot does not change its JSON schemas or migrate existing records.

Projects own schemas, null semantics, timestamps, retention, economic accounts,
backup and recovery. Empty strings are stored as text; use `idydb_delete` to
remove a cell. Existing exchange bounds and operator recovery instructions still
apply. The library's test fixtures and synthetic account tests do not establish
real vendor outcomes or financial certification.
