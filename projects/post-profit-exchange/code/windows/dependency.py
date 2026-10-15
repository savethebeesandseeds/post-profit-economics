#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Validate an explicitly supplied IdyDB binary package, never build or alter it."""
import hashlib
import json
import pathlib
import re
import sys

SCHEMA = "exchange.idydb-windows-package.v1"
CONTRACTS = (
    "utf8_paths", "encrypted_readonly_rejects_plaintext",
    "unchanged_encrypted_format", "safe_decrypted_working_storage",
    "atomic_encrypted_publication", "checked_windows_flush_and_close",
    "uncertain_commit_reported_as_error", "failed_open_discard_safe",
    "compatible_windows_file_sharing", "restrictive_acl_preserved",
    "encrypted_writable_open_no_change",
)
SYSTEM_LIBRARIES = {
    "advapi32", "bcrypt", "crypt32", "iphlpapi", "kernel32", "ncrypt",
    "secur32", "shell32", "user32", "winmm", "ws2_32",
}


def unique_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError("duplicate manifest key: " + key)
        value[key] = item
    return value


def fields(value, expected, context):
    if not isinstance(value, dict) or set(value) != set(expected):
        raise ValueError(context + " has missing or unknown fields")


def checked_file(root, value, context, extra=()):
    fields(value, ("path", "sha256", *extra), context)
    name, digest = value["path"], value["sha256"]
    if not isinstance(name, str) or not re.fullmatch(r"[A-Za-z0-9_.+/-]+", name):
        raise ValueError(context + ": use a simple relative package path")
    relative = pathlib.PurePosixPath(name)
    if relative.is_absolute() or any(part in (".", "..") for part in name.split("/")):
        raise ValueError(context + ": path must stay within the supplied package")
    if not isinstance(digest, str) or not re.fullmatch("[0-9a-f]{64}", digest):
        raise ValueError(context + ": expected lowercase SHA256")
    current = root
    for part in relative.parts:
        current = current / part
        if current.is_symlink():
            raise ValueError(context + ": symbolic links are not package files")
    if not current.is_file():
        raise ValueError(context + ": required file is absent")
    if hashlib.sha256(current.read_bytes()).hexdigest() != digest:
        raise ValueError(context + ": SHA256 mismatch")
    return {**value, "absolute_path": str(current.resolve())}


def validate(root):
    root = pathlib.Path(root).resolve(strict=True)
    if not root.is_dir():
        raise ValueError("IdyDB package must be a directory")
    manifest_path = root / "exchange-idydb-windows.json"
    if manifest_path.is_symlink() or manifest_path.stat().st_size > 65536:
        raise ValueError("invalid IdyDB package manifest")
    value = json.loads(manifest_path.read_text(encoding="utf-8"), object_pairs_hook=unique_object)
    fields(value, ("schema_version", "architecture", "consumer_abi", "sizing_mode",
                   "header", "library", "link_libraries", "system_libraries",
                   "runtime_dlls", "licenses", "contract", "evidence"), "manifest")
    if (value["schema_version"] != SCHEMA or value["architecture"] != "x86_64"
            or value["consumer_abi"] != "mingw-w64-x86_64" or value["sizing_mode"] != "BIG"):
        raise ValueError("expected x86_64 MinGW-compatible C API, BIG sizing and current package schema")
    fields(value["contract"], CONTRACTS, "contract")
    if any(value["contract"][key] is not True for key in CONTRACTS):
        raise ValueError("all Windows encryption/durability contract assertions must be explicit true")
    header = checked_file(root, value["header"], "header")
    if pathlib.PurePosixPath(header["path"]).name != "db.h":
        raise ValueError("public header must be named db.h")
    library = checked_file(root, value["library"], "library", ("kind",))
    if library["kind"] not in ("static", "import") or not library["path"].endswith(".a"):
        raise ValueError("library must be a MinGW static archive or DLL import archive")
    result = {**value, "package_root": str(root), "header": header, "library": library,
              "manifest_sha256": hashlib.sha256(manifest_path.read_bytes()).hexdigest()}
    for group in ("link_libraries", "runtime_dlls", "licenses"):
        entries = value[group]
        if not isinstance(entries, list) or len(entries) > 64:
            raise ValueError(group + " must be a bounded list")
        result[group] = [checked_file(root, entry, group) for entry in entries]
    if not result["licenses"]:
        raise ValueError("supplier must include licenses for IdyDB and its packaged dependencies")
    if any(not item["path"].endswith(".a") for item in result["link_libraries"]):
        raise ValueError("support link libraries must be MinGW archives")
    dll_names = [pathlib.PurePosixPath(item["path"]).name.lower() for item in result["runtime_dlls"]]
    if len(dll_names) != len(set(dll_names)) or any(not name.endswith(".dll") for name in dll_names):
        raise ValueError("runtime DLL basenames must be unique")
    if library["kind"] == "import" and not result["runtime_dlls"]:
        raise ValueError("an import library requires its runtime DLL")
    systems = value["system_libraries"]
    if (not isinstance(systems, list) or any(not isinstance(item, str) for item in systems)
            or len(systems) != len(set(systems)) or not set(systems) <= SYSTEM_LIBRARIES):
        raise ValueError("unsupported or repeated Windows system link library")
    result["evidence"] = checked_file(root, value["evidence"], "evidence")
    return result


def main():
    if len(sys.argv) != 3:
        raise ValueError("usage: dependency.py SUPPLIED_PACKAGE OUTPUT_JSON")
    validated = validate(sys.argv[1])
    pathlib.Path(sys.argv[2]).write_text(json.dumps(validated, indent=2) + "\n", encoding="utf-8")
    print("Supplied IdyDB package hashes verified; supplier contract assertions still require integration tests.")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError) as error:
        print("IdyDB dependency unavailable: " + str(error), file=sys.stderr)
        raise SystemExit(1)
