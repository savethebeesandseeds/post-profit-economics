#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Embed runtime, UI, notices and source; optional third argument selects output HTML."""
import base64
import hashlib
import html
import io
import json
import pathlib
import re
import sys
import zipfile

project = pathlib.Path(__file__).resolve().parent.parent
root = project.parent.parent
empty_history = project / "code/records/empty-history.json"
if json.loads(empty_history.read_text(encoding="utf-8")) != {"schema_version": "exchange.history.v1", "observations": []}:
    raise SystemExit("The packaged cold-start history must remain empty; use a separate file for observations")
runtime_path = pathlib.Path(sys.argv[1]).resolve()
sdk = pathlib.Path(sys.argv[2]).resolve()
runtime = runtime_path.read_text(encoding="utf-8")
embedded = re.search(r'findWasmBinary\(\)\{return base64Decode\("([A-Za-z0-9+/=]+)"\)', runtime)
if not embedded or not base64.b64decode(embedded.group(1), validate=True).startswith(b'\x00asm\x01\x00\x00\x00'):
    raise SystemExit("Expected an embedded SINGLE_FILE WebAssembly 1 module")

notices = [
    ("Repository and independent exchange application (MIT)", root / "LICENSE"),
    ("Simple exponential smoothing (EWMA, MIT)", root / "tools/exponential-smoothing/LICENSE"),
    ("Pricing core and bounded solver (Worker Protection License)", root / "tools/price-optimization/LICENSE"),
    ("nlohmann/json (MIT)", root / "tools/price-optimization/third_party/nlohmann/LICENSE.MIT"),
    ("Emscripten runtime", sdk / "upstream/emscripten/LICENSE"),
    ("LLVM libc++", sdk / "upstream/emscripten/system/lib/libcxx/LICENSE.TXT"),
    ("LLVM libc++abi", sdk / "upstream/emscripten/system/lib/libcxxabi/LICENSE.TXT"),
    ("musl C library", sdk / "upstream/emscripten/system/lib/libc/musl/COPYRIGHT"),
    ("compiler-rt", sdk / "upstream/emscripten/system/lib/compiler-rt/LICENSE.TXT"),
    ("LLVM libc", sdk / "upstream/emscripten/system/lib/llvm-libc/LICENSE.TXT"),
    ("LLVM libunwind", sdk / "upstream/emscripten/system/lib/libunwind/LICENSE.TXT"),
]
notice_text = [(title, path.read_text(encoding="utf-8")) for title, path in notices]
archive = io.BytesIO()
with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
    files = [root / "LICENSE", root / "docs/licensing.md", root / "docs/third-party-notices.md",
             root / "docs/exchange-simulation-verification.md", root / "docs/post-profit-economics.md",
             root / "docs/environment.md",
             root / "docs/adversarial-cooperation.md", root / "docs/reorganization.md",
             root / "docs/research-plan.md", root / "projects/post-profit-assurance/README.md",
             root / "projects/post-profit-assurance/CONTRACT.md"]
    # This release manifest deliberately excludes private records, configurations,
    # run directories and unrelated/generated browser artifacts.
    exchange_sources = (
        'README.md',
        'CMakeLists.txt',
        'doc/STORE_SPECIFICATION.md',
        'doc/CONTRACT.md',
        'doc/MODELS.md',
        'doc/FILES.md',
        'doc/HANDOFF.md',
        'doc/ENVIRONMENT.md',
        'doc/ROADMAP.md',
        'doc/STRUCTURE.md',
        'doc/OPERATING_ENVIRONMENT.md',
        'doc/APPLICATION_SERVICE.md',
        'doc/WINDOWS_NATIVE.md',
        'code/operations/opening.hpp',
        'code/operations/opening.cpp',
        'code/operations/transactions.hpp',
        'code/operations/transactions.cpp',
        'code/storage/environment_store.hpp',
        'code/storage/environment_store.cpp',
        'code/storage/environment_store_windows.cpp',
        'code/storage/windows_lock.hpp',
        'code/platform/files.hpp',
        'code/platform/files.cpp',
        'code/platform/windows_files.hpp',
        'code/platform/windows_files.cpp',
        'code/interfaces/environment.hpp',
        'code/interfaces/environment.cpp',
        'code/interfaces/environment_main.cpp',
        'code/interfaces/server_main.cpp',
        'code/interfaces/server_platform.hpp',
        'code/interfaces/server_platform.cpp',
        'code/third_party/cpp-httplib/httplib.h',
        'code/third_party/cpp-httplib/LICENSE',
        'code/third_party/cpp-httplib/PROVENANCE.md',
        'code/interfaces/application-container.ps1',
        'code/interfaces/run-service.sh',
        'code/launcher/main.cpp',
        'code/launcher/windows_arguments.hpp',
        'code/launcher/arguments_test.cpp',
        'code/launcher/launcher_test.ps1',
        'code/launcher/setup.sh',
        'code/launcher/build.sh',
        'code/windows/CMakeLists.txt',
        'code/windows/toolchain-mingw.cmake',
        'code/windows/setup.sh',
        'code/windows/build.sh',
        'code/windows/dependency.py',
        'code/windows/idydb-package.example.json',
        'code/windows/test_build_tools.py',
        'code/windows/run_windows_test.py',
        'code/windows/integration_test.ps1',
        'code/windows/package.py',
        'code/windows/README.md',
        'code/tests/environment_store_test.cpp',
        'code/tests/environment_test.cpp',
        'code/tests/transactions_test.cpp',
        'code/tests/transactions_integration_test.py',
        'code/tests/encrypted_environment_test.py',
        'code/tests/server_platform_test.cpp',
        'code/tests/windows_platform_files_test.cpp',
        'code/tests/windows_files_security_test.cpp',
        'code/tests/windows_environment_store_test.cpp',
        'environments/.gitignore',
        'environments/vendor_0x01/exchange.cfg',
        'environments/vendor_0x01/metadata.json',
        'environments/vendor_0x01/records/opening.json',
        'web/operations/environment.template.html',
        'web/tests/environment_view_test.cjs',
        'web/operations/transactions.html',
        'web/tests/transactions_view_test.cjs',
        'code/main.cpp',
        'code/files.hpp',
        'code/files.cpp',
        'code/models.hpp',
        'code/models.cpp',
        'code/simulation.hpp',
        'code/simulation.cpp',
        'code/default_config.hpp.in',
        'code/embed-config.py',
        'code/setup-wasm.sh',
        'code/setup.sh',
        'code/tests/files_test.cpp',
        'code/tests/models_test.cpp',
        'code/tests/simulation_test.cpp',
        'code/configs/exchange.cfg',
        'code/records/empty-history.json',
        'code/records/sample-history.json',
        'code/records/README.md',
        'web/app.js',
        'web/style.css',
        'web/index.template.html',
        'web/build-wasm.sh',
        'web/package-html.py',
        'web/tests/wasm_test.cjs',
        'web/tests/standalone_test.cjs',
    )
    files += [project / name for name in exchange_sources]
    files += [p for p in (root / "tools/price-optimization").rglob("*") if p.is_file()]
    files += [p for p in (root / "tools/exponential-smoothing").rglob("*") if p.is_file() and "__pycache__" not in p.parts]
    # Native environment source includes only fixed synthetic database fixtures;
    # operational databases and private instances are excluded.
    files += [root / "tools/idydb" / name for name in (
        "CMakeLists.txt", "README.md", "PROVENANCE.md", "REVIEW.md",
        "tests/roundtrip_test.cpp", "upstream/CMakeLists.txt",
        "upstream/README.md", "upstream/LICENSE",
        "upstream/include/db.h", "upstream/impl/db.cpp",
        "upstream/impl/platform.h", "upstream/impl/platform_posix.cpp",
        "upstream/impl/platform_windows.cpp", "upstream/cmake/idydbConfig.cmake.in",
        "upstream/tests/core_tests.cpp", "upstream/tests/query_tests.cpp",
        "upstream/tests/persistence_tests.cpp", "upstream/tests/c_api_test.c",
        "upstream/tests/failure_tests.cpp", "upstream/tests/failure_hooks.h",
        "upstream/tests/directory_tests.cpp", "upstream/tests/platform_tests.cpp",
        "upstream/tests/compatibility_tests.cpp", "upstream/tests/test_process.h",
        "upstream/tests/fixtures/README.md", "upstream/tests/fixtures/portable-v1.db",
        "upstream/tests/fixtures/portable-v1.encrypted.db",
        "upstream/tests/consumer/CMakeLists.txt", "upstream/tests/consumer/consumer.c",
        "upstream/tests/consumer/consumer.cpp")]
    # A source archive must never collect generated Windows dependencies,
    # application binaries or operational secrets, even if a tool adds one.
    private_names = {"exchange-idydb-windows.json", "idydb-verified.json",
                     "windows-integration.json"}
    synthetic_databases = {
        "tools/idydb/upstream/tests/fixtures/portable-v1.db":
            "6b573cdf9d88b3e559630dcf8058c3b8abf5bb7f903cfb9e84d223080ca50278",
        "tools/idydb/upstream/tests/fixtures/portable-v1.encrypted.db":
            "61cae19f4679d6b1b0dc251a6a80bdbb18c0adbf9c941d3befcd49efe4b608f8",
    }
    for path in sorted(set(files)):
        relative = path.relative_to(root).as_posix()
        if (path.suffix.lower() in {".key", ".idydb", ".exe", ".dll", ".pdb"} or
                path.name.lower() in private_names or
                (path.suffix.lower() == ".db" and relative not in synthetic_databases)):
            raise SystemExit(f"Private/generated file is not a source input: {path.relative_to(root)}")
        content = path.read_bytes()
        if relative in synthetic_databases and hashlib.sha256(content).hexdigest() != synthetic_databases[relative]:
            raise SystemExit(f"Synthetic database fixture differs from its fixed hash: {relative}")
        info = zipfile.ZipInfo(relative, (2026, 9, 20, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        bundle.writestr(info, content)
    for i, (title, content) in enumerate(notice_text):
        info = zipfile.ZipInfo(f"runtime-notices/{i}.txt", (2026, 9, 20, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        bundle.writestr(info, title + "\n\n" + content)

source_data = base64.b64encode(archive.getvalue()).decode("ascii")
licenses = ('<p>The application source is MIT. This combined file also contains the separately '
            'licensed pricing engine and third-party runtime code. Their terms remain applicable. '
            'It contains no AMPL binaries or vendor entitlement.</p>'
            '<p><a download="post-profit-exchange-source.zip" href="data:application/zip;base64,'
            + source_data + '">Download corresponding source and build scripts (ZIP)</a></p>')
licenses += '<p>Emscripten 4.0.15; compiled runtime SHA-256: <code>' + hashlib.sha256(runtime.encode()).hexdigest() + '</code></p>'
for title, content in notice_text:
    licenses += '<details><summary>' + html.escape(title) + '</summary><pre>' + html.escape(content) + '</pre></details>'

page = (project / "web/index.template.html").read_text(encoding="utf-8")
parts = {
    "__STYLE__": (project / "web/style.css").read_text(encoding="utf-8"),
    "__RUNTIME__": runtime.replace("</script", "<\\/script"),
    "__APP__": (project / "web/app.js").read_text(encoding="utf-8").replace("</script", "<\\/script"),
    "__LICENSES__": licenses,
}
for marker, content in parts.items():
    if page.count(marker) != 1:
        raise SystemExit(f"Expected one template marker {marker}")
    page = page.replace(marker, content)
destination = (pathlib.Path(sys.argv[3]).resolve() if len(sys.argv) > 3
               else project / "web/dist/post-profit-exchange.html")
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_text(page, encoding="utf-8", newline="\n")
print(f"Standalone HTML: {destination} ({destination.stat().st_size:,} bytes)")
