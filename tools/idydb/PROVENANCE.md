# IdyDB source provenance

Updated on 2026-10-15 from the committed and pushed standalone revision below.

- Standalone repository: [savethebeesandseeds/IdyDB](https://github.com/savethebeesandseeds/IdyDB).
- Source commit: [`11dfda017a4b5163357d991421ab06ff75ebdf98`](https://github.com/savethebeesandseeds/IdyDB/commit/11dfda017a4b5163357d991421ab06ff75ebdf98), `Add native Windows storage and portable SDK builds`, pushed to `origin/main`.
- Snapshot status: all 25 imported files match the raw Git blobs at that source commit. The shared implementation has no local patches; the outer wrapper and consumer integration documents remain project-owned.
- License: [MIT, copyright (c) 2025 Waajacu](upstream/LICENSE).
- The source retains its attribution to [FlitDB](https://github.com/bradley499/flitdb).

All 25 mapped files below are copied byte-for-byte from the committed Git
blobs. Their SHA-256 hashes identify the exact imported bytes, including the
review fixes and fixed compatibility fixtures.

| Standalone path relative to `C:\Work\idydb` | Imported path | SHA-256 |
| --- | --- | --- |
| `idydb/include/db.h` | `upstream/include/db.h` | `aa4af0e4c7ca27b61e1f51939df1b1e05e9d5edc3286051f2d7a9c6de30e0f77` |
| `idydb/impl/db.cpp` | `upstream/impl/db.cpp` | `eb3832f7f17f588e3fb1eaa233f534b0b0c33f30e328e1d1b8b7df6a6f666e34` |
| `idydb/impl/platform.h` | `upstream/impl/platform.h` | `228d50abdeddfae3ddba9afc52489bb56c9bb8091fba2b143110acc58729b285` |
| `idydb/impl/platform_posix.cpp` | `upstream/impl/platform_posix.cpp` | `49c180db3d7b810c4ef547b081da62f55da70e2fb1861f63aad83ec180575d2c` |
| `idydb/impl/platform_windows.cpp` | `upstream/impl/platform_windows.cpp` | `90394c111d14a14e09831dedd4b6b61de5c788aa43d44210836f04938b15b6b2` |
| `idydb/CMakeLists.txt` | `upstream/CMakeLists.txt` | `268b89d4a77bf9c943fb695e70503aa2cb5a001009a551fff62207d850c8b324` |
| `idydb/cmake/idydbConfig.cmake.in` | `upstream/cmake/idydbConfig.cmake.in` | `cf1c556f84945da349ef8da8bf7d8168ea14fa4d9991948ad7548774f56c9007` |
| `idydb/tests/core_tests.cpp` | `upstream/tests/core_tests.cpp` | `7af8056bf39b3da7d99252e8bc8099d3b27e3cf52ffbb3bed29528a3b38d49ab` |
| `idydb/tests/query_tests.cpp` | `upstream/tests/query_tests.cpp` | `34426ecefe2dba983c1c021dd998883ff41737e68cdfa5f426649c7afced6865` |
| `idydb/tests/persistence_tests.cpp` | `upstream/tests/persistence_tests.cpp` | `546c1f026b67d2cba62c56165178875c898d83d0399a1b6a6f2070706ab9c27a` |
| `idydb/tests/c_api_test.c` | `upstream/tests/c_api_test.c` | `401eaa4b2a27fa79d089681d65fc5be4f777e9c3ec39fffb184f4cf023b57177` |
| `idydb/tests/failure_tests.cpp` | `upstream/tests/failure_tests.cpp` | `f6ad364d197f97cbc7558d714d0b1e7cdb920ac1af9fd70f6a47f569f46ab40b` |
| `idydb/tests/failure_hooks.h` | `upstream/tests/failure_hooks.h` | `c4524780e355d7896276e12b55eacda9bc0cebd778c5ed62fb0b51cd256d1a42` |
| `idydb/tests/directory_tests.cpp` | `upstream/tests/directory_tests.cpp` | `7e3be551487b0e9c0b5098c79953f21b1ad1f82a175120f46c7a83ad22143812` |
| `idydb/tests/platform_tests.cpp` | `upstream/tests/platform_tests.cpp` | `bb6fcb89cf8d75a8d3cbe61454acf677eaf05e4a7c251093a9e94ba60743fc38` |
| `idydb/tests/compatibility_tests.cpp` | `upstream/tests/compatibility_tests.cpp` | `7d8f68993f2405274fe5163ea54ac31aba9e574d3ba1a957baaadbc89d90f8c8` |
| `idydb/tests/test_process.h` | `upstream/tests/test_process.h` | `5cce7c8e9397e8696e2ac4d6f2527a2ac89f62aa0fd8e164dd121f0ebd7af54e` |
| `idydb/tests/fixtures/README.md` | `upstream/tests/fixtures/README.md` | `d53a70ca863f61ed7f3c994b8a562e570bb85807b29262428acc792a1aafae26` |
| `idydb/tests/fixtures/portable-v1.db` | `upstream/tests/fixtures/portable-v1.db` | `6b573cdf9d88b3e559630dcf8058c3b8abf5bb7f903cfb9e84d223080ca50278` |
| `idydb/tests/fixtures/portable-v1.encrypted.db` | `upstream/tests/fixtures/portable-v1.encrypted.db` | `61cae19f4679d6b1b0dc251a6a80bdbb18c0adbf9c941d3befcd49efe4b608f8` |
| `idydb/tests/consumer/CMakeLists.txt` | `upstream/tests/consumer/CMakeLists.txt` | `1868657dcfcd385f501eb606cddd7f10570a6ba36a802dd3787a03a04eb6b4fd` |
| `idydb/tests/consumer/consumer.c` | `upstream/tests/consumer/consumer.c` | `240c9b3b5de231c22788608ffa806e261328a25ced67158a2273af698bcffb63` |
| `idydb/tests/consumer/consumer.cpp` | `upstream/tests/consumer/consumer.cpp` | `422f07c140a98ed8b1df44c5cf211840253340af3d507011bb38ce64bca3504a` |
| `LICENSE` | `upstream/LICENSE` | `9b9ccf7ff1a8c85b8596e9fec944266867c5b8075b93fea13eead2df645f9615` |
| `README.md` | `upstream/README.md` | `3c77e49b3e84c591836f44e47fdb969beace51f9b2701cc989f66a5bb3cd0062` |

The standalone Windows working checkout retains CRLF line endings in `LICENSE`
and `README.md`, while their committed Git blobs use LF. This import deliberately
uses the committed LF bytes for `upstream/LICENSE` and `upstream/README.md`.
Their text content is unchanged; the standalone checkout was not rewritten.
The other 23 imported files already matched their committed bytes. Future Git
text normalization can change checkout line endings, so compare the raw commit
blobs when verifying this snapshot. The two `.db` fixtures have an explicit
binary attribute.

The snapshot retains bounded decoding, atomic plaintext and text/vector
updates, encrypted flush/close retry and discard behavior, and POSIX pinned
directory access. Storage operations now use separate POSIX and Windows
backends. It also includes UTF-8 pathname handling, `idydb_alloc`, an installed
C/C++ consumer example, subprocess lock/crash tests and fixed record-encoding
fixtures. The fixture passphrase is intentionally public synthetic test data;
see [fixture documentation](upstream/tests/fixtures/README.md). The record
encoding remains unchanged. The review follow-up adds bounded same-parent
staging names for valid long basenames, explicit Windows attribute retention,
and exported static-library dependencies for genuine C-only consumers.

`.git`, the Node addon, standalone setup/launcher/cross-build scripts, CI, SDKs,
Windows executables/DLLs and local operational/build data are excluded. OpenSSL
is a separately supplied dependency; no OpenSSL binary is imported. The
upstream README describes the complete standalone project, including components
outside this snapshot. Use the [outer README](README.md#build-and-validation)
for this repository's existing managed Linux development container.

The outer README, CMake wrapper, review and local roundtrip test are
repository-specific integration material. The exchange adapters, schemas and
application platform support remain consuming-project code. This update does
not migrate or rewrite existing databases and does not validate that project's
separate Windows application implementation. [The review](REVIEW.md) records
the actual consumer validation scope.

Shared corrections should continue to update both copies with matching tests,
documentation and refreshed provenance. Preserve unrelated work and record
deliberate differences.
