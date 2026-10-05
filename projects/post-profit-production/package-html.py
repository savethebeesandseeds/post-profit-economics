#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Package the production runtime, research UI and applicable source/notices.

The release has hash-restricted scripts and styles. A separate ignored noindex
copy allows only the local Codex selector's inline style-element exception.
"""
import base64
import hashlib
import html
import io
import pathlib
import re
import sys
import zipfile

if len(sys.argv) != 3:
    raise SystemExit("Usage: package-html.py SINGLE_FILE_RUNTIME EMSDK_DIRECTORY")

project = pathlib.Path(__file__).resolve().parent
root = project.parent.parent
runtime_path = pathlib.Path(sys.argv[1]).resolve()
sdk = pathlib.Path(sys.argv[2]).resolve()
runtime = runtime_path.read_text(encoding="utf-8")
embedded = re.search(r'findWasmBinary\(\)\{return base64Decode\("([A-Za-z0-9+/=]+)"\)', runtime)
if not embedded or not base64.b64decode(embedded.group(1), validate=True).startswith(b"\x00asm\x01\x00\x00\x00"):
    raise SystemExit("Expected an embedded SINGLE_FILE WebAssembly 1 module")
if "createProductionModule" not in runtime:
    raise SystemExit("Expected the production runtime factory")

notices = [
    ("Repository and production application (MIT)", root / "LICENSE"),
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

files = [root / "LICENSE"]
json_directory = root / "tools/price-optimization/third_party/nlohmann"
files += [json_directory / name for name in ("json.hpp", "LICENSE.MIT", "PROVENANCE.md")]
approved_configs = {project / "configs/research.cfg", project / "configs/scheduling-comparison.cfg"}
for path in project.rglob("*"):
    relative = path.relative_to(project)
    if {"dist", "runs", "records", "__pycache__"}.intersection(relative.parts):
        continue
    if "configs" in relative.parts and path not in approved_configs:
        continue
    if path.is_symlink():
        raise SystemExit(f"Source bundle refuses a symbolic link: {relative}")
    if path.is_file():
        files.append(path)

archive = io.BytesIO()
with zipfile.ZipFile(archive, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as bundle:
    for path in sorted(set(files)):
        info = zipfile.ZipInfo(path.relative_to(root).as_posix(), (2026, 10, 5, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        bundle.writestr(info, path.read_bytes())
    for index, (title, content) in enumerate(notice_text):
        info = zipfile.ZipInfo(f"runtime-notices/{index}.txt", (2026, 10, 5, 0, 0, 0))
        info.compress_type = zipfile.ZIP_DEFLATED
        bundle.writestr(info, title + "\n\n" + content)

source_data = base64.b64encode(archive.getvalue()).decode("ascii")
licenses = (
    "<p>The production application source is MIT. The separately noticed JSON "
    "and compiler runtime retain their own terms. This file contains no pricing "
    "engine, AMPL, forecasting model, or vendor entitlement.</p>"
    '<p><a download="post-profit-production-source.zip" href="data:application/zip;base64,'
    + source_data + '">Download corresponding source and build scripts (ZIP)</a></p>'
)
licenses += ("<p>Emscripten 4.0.15; compiled runtime SHA-256: <code>"
             + hashlib.sha256(runtime.encode("utf-8")).hexdigest() + "</code></p>")
for title, content in notice_text:
    licenses += "<details><summary>" + html.escape(title) + "</summary><pre>" + html.escape(content) + "</pre></details>"

def inert_json(path):
    # Preserve number tokens and duplicate-key diagnostics; do not parse and
    # serialize configuration on its way to the strict native JSON boundary.
    return path.read_text(encoding="utf-8").replace("<", "\\u003c")

page = (project / "web/index.template.html").read_text(encoding="utf-8")
parts = {
    "__STYLE__": (project / "web/style.css").read_text(encoding="utf-8"),
    "__RUNTIME__": runtime.replace("</script", "<\\/script"),
    "__APP__": (project / "web/app.js").read_text(encoding="utf-8").replace("</script", "<\\/script"),
    "__DEFAULT_CONFIG__": inert_json(project / "configs/research.cfg"),
    "__DEFAULT_PLAN__": inert_json(project / "configs/scheduling-comparison.cfg"),
    "__LICENSES__": licenses,
}
for marker, content in parts.items():
    if page.count(marker) != 1:
        raise SystemExit(f"Expected one template marker {marker}")
    page = page.replace(marker, content)
for marker in ("__CSP__", "__REVIEW_META__"):
    if page.count(marker) != 1:
        raise SystemExit(f"Expected one template marker {marker}")

app_matches = re.findall(r'<script\b(?=[^>]*\bid="production-app")[^>]*>([\s\S]*?)</script\s*>', page)
style_matches = re.findall(r'<style\b[^>]*>([\s\S]*?)</style\s*>', page)
if len(app_matches) != 1 or len(style_matches) != 1:
    raise SystemExit("Expected one production-app script and one style element")

def csp_hash(content):
    return "'sha256-" + base64.b64encode(hashlib.sha256(content.encode("utf-8")).digest()).decode("ascii") + "'"

# No network fetch is needed: runtime bytes, examples and notices are embedded.
# Blob workers inherit this policy and may instantiate WASM without JS eval.
policy = (
    "default-src 'none'; "
    + "script-src " + csp_hash(app_matches[0]) + " 'wasm-unsafe-eval'; "
    + "script-src-attr 'none'; worker-src blob:; connect-src 'none'; "
    + "style-src " + csp_hash(style_matches[0]) + "; style-src-attr 'none'; "
    + "base-uri 'none'; object-src 'none'; form-action 'none'; frame-src 'none'"
)

def with_policy(value, review=False):
    review_meta = ('<meta name="robots" content="noindex, nofollow">'
                   "\n  <!-- Local review only: allow the selector's injected style elements. -->") if review else ""
    csp_meta = '<meta http-equiv="Content-Security-Policy" content="' + html.escape(value, quote=True) + '">'
    return page.replace("__CSP__", csp_meta).replace("__REVIEW_META__", review_meta)

destination = project / "dist/post-profit-production.html"
destination.parent.mkdir(exist_ok=True)
destination.write_text(with_policy(policy), encoding="utf-8", newline="\n")

review_destination = root / ".build/post-profit-production/web-review/post-profit-production.html"
review_destination.parent.mkdir(parents=True, exist_ok=True)
review_policy = policy + "; style-src-elem 'self' 'unsafe-inline'"
review_destination.write_text(with_policy(review_policy, review=True), encoding="utf-8", newline="\n")
print(f"Standalone HTML: {destination} ({destination.stat().st_size:,} bytes)")
print(f"Local noindex review HTML: {review_destination}")
