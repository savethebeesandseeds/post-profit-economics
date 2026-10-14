# IdyDB source provenance

Updated on 2026-10-14 from the local working tree at `C:\Work\idydb`.

- Standalone repository: [savethebeesandseeds/IdyDB](https://github.com/savethebeesandseeds/IdyDB).
- Base source commit: `7912afd007734adb08b018a18e3808131b3c3108` (`initial commit, stable library`).
- Snapshot status: the shared source, build, regression tests and README include
  uncommitted revisions. This is a working-tree snapshot, not a clean or
  unchanged copy of the base commit.
- License: [MIT, copyright (c) 2025 Waajacu](upstream/LICENSE).
- The source retains its attribution to [FlitDB](https://github.com/bradley499/flitdb).

The files below are copied byte-for-byte from the recorded standalone working
tree. Their hashes identify the imported snapshot; the base commit alone does
not identify these revised bytes.

| Standalone path relative to `C:\Work\idydb` | Imported path | SHA-256 |
| --- | --- | --- |
| `idydb/include/db.h` | `upstream/include/db.h` | `78036e96a5cda3b2ec1a022c2e47ccf7e32746160793de4b0e519a34ca3f070c` |
| `idydb/impl/db.cpp` | `upstream/impl/db.cpp` | `37770ff3dab405d0afc66d4e237b1ac677b9f56c70d171f89663a71508338c08` |
| `idydb/CMakeLists.txt` | `upstream/CMakeLists.txt` | `c7a534770da8ee63b25e3b5b7d3356948853eb3836ceaa6c9440243cb5325228` |
| `idydb/tests/core_tests.cpp` | `upstream/tests/core_tests.cpp` | `85dd8a8e9c0ddb18591844635fa9583e405b5afbe2ef468dad188499f1deea5f` |
| `idydb/tests/query_tests.cpp` | `upstream/tests/query_tests.cpp` | `bb0f397c597c6d805c8d5c47a06908b4129557f3c747e48b6d3e8b92247ad3ac` |
| `idydb/tests/persistence_tests.cpp` | `upstream/tests/persistence_tests.cpp` | `7f97aee7f1ffb70092021206007ca675866a7408a07a76c8cd1792b178a4a26a` |
| `idydb/tests/c_api_test.c` | `upstream/tests/c_api_test.c` | `fe0b4f17bd7c8a25e79bf42bb824d1bb8c4f06a10a0430702c9865673f6c9ff0` |
| `idydb/tests/failure_tests.cpp` | `upstream/tests/failure_tests.cpp` | `db7045b462ac92b3c74e329875e3e20fb0f53ab8cd031a8977a3ccb7a9f45856` |
| `idydb/tests/failure_hooks.h` | `upstream/tests/failure_hooks.h` | `9457ea06c808b7d54b7eae41c0e3933c7309a5ede953ec87706ea2fa3eb041c7` |
| `idydb/tests/directory_tests.cpp` | `upstream/tests/directory_tests.cpp` | `52f1caf2f51d0e452da0fd60d325a95f80b5f9b4b7b09c2a0a271f9b65a2eb02` |
| `LICENSE` | `upstream/LICENSE` | `ff482591a448f788e4fc4a0b9e54393d02e17b71a5adefad670472de14716230` |
| `README.md` | `upstream/README.md` | `b0e2072f7ed5efff275cc78d6bf9194abefb476f76d36c1cc6383ce4a65773c9` |

These are hashes of actual imported bytes. Git text normalization can change
line endings on a later checkout; record that separately from code changes when
comparing copies.

The snapshot includes bounds-checked decoding, query/filter corrections, atomic
plaintext and text/vector updates, safer encrypted replacement, flush/discard
lifecycle operations, directory-sync retries, one-lock open behavior and the
POSIX plaintext pinned-directory API. Regression suites cover these shared
behaviors. The format remains unchanged; [the review](REVIEW.md) records limits
and actual validation results.

`.git`, the Node addon, standalone container/setup/launcher scripts, CI files and
local runtime/build data are excluded. OpenSSL is a separately supplied build
and runtime dependency; no OpenSSL binary is copied. The upstream README
describes the complete standalone project, including components outside this
snapshot. Use the [outer README](README.md#build-and-validation) for integration
commands in this repository's existing development container.

The outer README, CMake wrapper, adoption review and local roundtrip test are
repository-specific integration material. The exchange JSON-envelope adapter
remains part of the consuming project. Shared changes should continue to update
both copies with matching tests, documentation and a refreshed provenance
record; preserve unrelated work and record deliberate differences.
