#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Create a native Windows package only from a validated, integration-tested build."""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess
import sys
import zipfile
import dependency

TESTS = (
    "encrypted_roundtrip", "reopen", "wrong_key_preserves_ciphertext",
    "ciphertext_only", "idempotent_retry", "stale_revision",
    "http_start_and_accounts", "http_transaction_and_retry", "graceful_shutdown", "windows_storage_security", "encrypted_storage_contract",
)
SYSTEM_DLLS = {
    "advapi32.dll", "bcrypt.dll", "crypt32.dll", "dnsapi.dll", "iphlpapi.dll",
    "kernel32.dll", "msvcrt.dll", "ncrypt.dll", "ntdll.dll", "ole32.dll",
    "oleaut32.dll", "secur32.dll", "shell32.dll", "shlwapi.dll", "user32.dll",
    "userenv.dll", "version.dll", "winmm.dll", "ws2_32.dll", "wtsapi32.dll",
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def verify_receipt(receipt, server, cli, verified, page):
    security = server.parent / "exchange_windows_storage_files_test.exe"
    storage = server.parent / "exchange_windows_database_storage_test.exe"
    dependency.fields(receipt, ("schema_version", "platform", "server_sha256",
                               "cli_sha256", "security_test_sha256", "storage_test_sha256", "idydb_manifest_sha256", "page_sha256", "runtime_dlls", "tests"), "integration receipt")
    if receipt["schema_version"] != "exchange.windows-integration.v1" or receipt["platform"] != "windows-ntfs":
        raise ValueError("a real Windows NTFS application integration receipt is required")
    dependency.fields(receipt["tests"], TESTS, "integration test results")
    if any(receipt["tests"][item] is not True for item in TESTS):
        raise ValueError("all required application integration tests must pass")
    expected_runtime = {pathlib.PurePosixPath(item["path"]).name.lower(): item["sha256"]
                        for item in verified["runtime_dlls"]}
    if receipt["runtime_dlls"] != expected_runtime:
        raise ValueError("integration receipt does not match the tested runtime DLLs")
    if (receipt["server_sha256"] != sha(server.read_bytes())
            or receipt["cli_sha256"] != sha(cli.read_bytes())
            or receipt["security_test_sha256"] != sha(security.read_bytes())
            or receipt["storage_test_sha256"] != sha(storage.read_bytes())
            or receipt["page_sha256"] != sha(page.read_bytes())
            or receipt["idydb_manifest_sha256"] != verified["manifest_sha256"]):
        raise ValueError("integration receipt does not match these exact application/dependency artifacts")


def pe_imports(path):
    output = subprocess.run(["x86_64-w64-mingw32-objdump", "-p", str(path)],
                            capture_output=True, text=True, check=True).stdout
    if "file format pei-x86-64" not in output:
        raise ValueError("package requires an x86_64 Windows PE image: " + path.name)
    return {name.lower() for name in re.findall(r"DLL Name:\s*(\S+)", output)}


def sample_files():
    def encode(value):
        return (json.dumps(value, indent=2) + "\n").encode()
    configuration = {
        "schema_version": "exchange.environment.v2", "environment_id": "new_vendor",
        "currency": "EUR", "minor_unit_digits": 2,
        "storage": {"database": "data/exchange.idydb"}, "metadata_file": "metadata.json",
        "opening_file": "records/opening.json", "runtime": {
            "workspace": ".", "server": {"bind": "127.0.0.1", "port": 8766},
            "encryption": {"required": True,
                           "key_file": "%USERPROFILE%/.post-profit-exchange/keys/new_vendor.key"}}}
    metadata = {
        "schema_version": "exchange.environment-metadata.v1",
        "display_name": "New operation — uninitialized example",
        "description": "Synthetic empty opening template. Replace settings and records before explicit initialization.",
        "provenance": "synthetic", "products": [{"sku": "example", "label": "Example product", "unit": "piece"}]}
    opening = {
        "schema_version": "exchange.opening.v1", "environment_id": "new_vendor",
        "as_of": "2026-10-15", "cash": 0, "reserve": 0, "earned_funding": 0,
        "obligations": [], "products": [
            {"sku": "example", "published_price": 100, "replacement_cost": 100, "lots": []}]}
    return {
        "sample/new_vendor/exchange.cfg": encode(configuration),
        "sample/new_vendor/metadata.json": encode(metadata),
        "sample/new_vendor/records/opening.json": encode(opening),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build", type=pathlib.Path, required=True)
    parser.add_argument("--idydb-package", type=pathlib.Path, required=True)
    parser.add_argument("--integration-receipt", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path,
                        default=pathlib.Path("/opt/post-profit-exchange/windows/packages/post-profit-exchange-windows-x64.zip"))
    args = parser.parse_args()
    if not pathlib.Path("/.dockerenv").is_file():
        raise ValueError("package inside the managed Debian container")
    build = args.build.resolve(strict=True)
    server, cli = build / "post-profit-exchange.exe", build / "exchange_environment.exe"
    if not server.is_file() or not cli.is_file():
        raise ValueError("native application executables are absent; compile-only objects cannot be packaged")
    cache = (build / "CMakeCache.txt").read_text()
    configured = re.search(r"^PH_EXCHANGE_IDYDB_PACKAGE:PATH=(.+)$", cache, re.MULTILINE)
    if not configured or pathlib.Path(configured.group(1)).resolve() != args.idydb_package.resolve():
        raise ValueError("build is not currently configured for this supplied IdyDB package")
    verified = dependency.validate(args.idydb_package)
    built_dependency = json.loads((build / "idydb-verified.json").read_text(), object_pairs_hook=dependency.unique_object)
    if built_dependency["manifest_sha256"] != verified["manifest_sha256"]:
        raise ValueError("supplied dependency differs from the configured build")
    receipt_bytes = args.integration_receipt.read_bytes()
    if len(receipt_bytes) > 65536:
        raise ValueError("integration receipt exceeds supported size")
    receipt = json.loads(receipt_bytes, object_pairs_hook=dependency.unique_object)
    project = pathlib.Path(__file__).resolve().parents[2]
    verify_receipt(receipt, server, cli, verified, project / "web/operations/transactions.html")
    runtime = {pathlib.Path(item["path"]).name.lower(): pathlib.Path(item["absolute_path"])
               for item in verified["runtime_dlls"]}
    if set(runtime) & SYSTEM_DLLS:
        raise ValueError("a supplied DLL must not shadow a Windows system DLL")
    imports = {}
    for binary in (server, cli, *runtime.values()):
        needed = pe_imports(binary)
        unprovided = needed - SYSTEM_DLLS - runtime.keys()
        if unprovided:
            raise ValueError(binary.name + ": unprovided runtime DLLs: " + ", ".join(sorted(unprovided)))
        imports[binary.name] = sorted(needed)
    repository = project.parents[1]
    files = sample_files()
    files["post-profit-exchange.exe"] = server.read_bytes()
    files["exchange_environment.exe"] = cli.read_bytes()
    files["web/transactions.html"] = (project / "web/operations/transactions.html").read_bytes()
    files["WINDOWS_NATIVE.md"] = (project / "doc/WINDOWS_NATIVE.md").read_bytes()
    files["LICENSE"] = (repository / "LICENSE").read_bytes()
    files["licenses/cpp-httplib.txt"] = (project / "code/third_party/cpp-httplib/LICENSE").read_bytes()
    files["licenses/nlohmann-json.txt"] = (repository / "tools/price-optimization/third_party/nlohmann/LICENSE.MIT").read_bytes()
    files["licenses/mingw-w64.txt"] = pathlib.Path("/usr/share/doc/mingw-w64-common/copyright").read_bytes()
    files["licenses/gcc-runtime.txt"] = pathlib.Path("/usr/share/doc/gcc-mingw-w64-x86-64-posix/copyright").read_bytes()
    for name, path in runtime.items():
        files[name] = path.read_bytes()
    for index, item in enumerate(verified["licenses"]):
        files["licenses/supplied-" + str(index) + "-" + pathlib.Path(item["path"]).name] = pathlib.Path(item["absolute_path"]).read_bytes()
    files["evidence/windows-integration.json"] = receipt_bytes
    # Supplier evidence may contain internal build paths; include only its digest.
    manifest = {
        "schema_version": "exchange.windows-distribution.v1",
        "architecture": "x86_64", "minimum_windows": "10",
        "idydb_manifest_sha256": verified["manifest_sha256"],
        "supplier_evidence_sha256": verified["evidence"]["sha256"],
        "integration_receipt_sha256": sha(receipt_bytes),
        "imports": imports,
        "files": {name: sha(contents) for name, contents in sorted(files.items())},
    }
    files["manifest.json"] = (json.dumps(manifest, indent=2) + "\n").encode()
    if len({name.lower() for name in files}) != len(files):
        raise ValueError("package names collide on Windows")
    output = args.output.resolve()
    allowed = pathlib.Path("/opt/post-profit-exchange/windows").resolve()
    if not output.is_relative_to(allowed):
        raise ValueError("create packages inside /opt/post-profit-exchange/windows")
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("xb") as stream:
        with zipfile.ZipFile(stream, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name, contents in sorted(files.items()):
                item = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
                item.compress_type = zipfile.ZIP_DEFLATED
                item.external_attr = 0o644 << 16
                archive.writestr(item, contents)
    print(json.dumps({"archive": str(output), "sha256": sha(output.read_bytes()), "files": len(files)}))


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print("Windows package not created: " + str(error), file=sys.stderr)
        raise SystemExit(1)
