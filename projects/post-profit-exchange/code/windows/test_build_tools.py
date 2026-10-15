#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Package-contract validation only: these files are never linked or executed."""
import copy
import hashlib
import json
import pathlib
import tempfile
import unittest
import dependency
import package


class PackageContract(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="exchange-package-contract-")
        self.addCleanup(self.temporary.cleanup)
        self.root = pathlib.Path(self.temporary.name)
        def record(name, contents):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(contents)
            return {"path": name, "sha256": hashlib.sha256(contents).hexdigest()}
        self.value = {
            "schema_version": dependency.SCHEMA, "architecture": "x86_64",
            "consumer_abi": "mingw-w64-x86_64", "sizing_mode": "BIG",
            "header": record("include/db.h", b"metadata-only header fixture\n"),
            "library": {**record("lib/libidydb.a", b"not a library; never linked\n"), "kind": "static"},
            "link_libraries": [], "system_libraries": [], "runtime_dlls": [],
            "licenses": [record("licenses/idydb.txt", b"metadata-only test notice\n")],
            "contract": {key: True for key in dependency.CONTRACTS},
            "evidence": record("evidence/validation.md", b"manifest validator fixture, not runtime evidence\n"),
        }

    def write(self):
        (self.root / "exchange-idydb-windows.json").write_text(json.dumps(self.value))
        return dependency.validate(self.root)

    def test_valid_metadata(self):
        self.assertEqual(self.write()["library"]["kind"], "static")

    def test_digest_mismatch(self):
        (self.root / "lib/libidydb.a").write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "SHA256"): self.write()

    def test_missing_library(self):
        (self.root / "lib/libidydb.a").unlink()
        with self.assertRaisesRegex(ValueError, "absent"): self.write()

    def test_unconfirmed_contract(self):
        self.value["contract"]["safe_decrypted_working_storage"] = False
        with self.assertRaisesRegex(ValueError, "contract assertions"): self.write()

    def test_nonboolean_contract(self):
        self.value["contract"]["safe_decrypted_working_storage"] = 1
        with self.assertRaisesRegex(ValueError, "contract assertions"): self.write()

    def test_traversal(self):
        self.value["header"]["path"] = "../db.h"
        with self.assertRaisesRegex(ValueError, "within"): self.write()

    def test_symlink(self):
        header = self.root / "include/db.h"
        header.rename(self.root / "other.h")
        header.symlink_to(self.root / "other.h")
        with self.assertRaisesRegex(ValueError, "symbolic"): self.write()

    def test_incompatible_abi(self):
        self.value["sizing_mode"] = "TINY"
        with self.assertRaisesRegex(ValueError, "BIG sizing"): self.write()

    def test_import_requires_dll(self):
        self.value["library"]["kind"] = "import"
        with self.assertRaisesRegex(ValueError, "runtime DLL"): self.write()

    def test_duplicate_json_keys(self):
        self.write()
        path = self.root / "exchange-idydb-windows.json"
        path.write_text('{"schema_version":"first",' + path.read_text()[1:])
        with self.assertRaisesRegex(ValueError, "duplicate"): dependency.validate(self.root)

    def test_extra_link_injection(self):
        self.value["system_libraries"] = ["ws2_32;-Wl,injected"]
        with self.assertRaisesRegex(ValueError, "system link"): self.write()


class DistributionGate(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="exchange-receipt-metadata-")
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        for name in ("post-profit-exchange.exe", "exchange_environment.exe",
                     "exchange_windows_storage_files_test.exe", "exchange_windows_database_storage_test.exe"):
            (self.root / name).write_bytes(b"metadata validation fixture only; never executed")
        self.server, self.cli = self.root / "post-profit-exchange.exe", self.root / "exchange_environment.exe"
        self.page = self.root / "transactions.html"
        self.page.write_bytes(b"metadata-only page fixture")
        self.dependency = {"manifest_sha256": "a" * 64, "runtime_dlls": []}
        digest = package.sha(self.server.read_bytes())
        self.receipt = {"schema_version": "exchange.windows-integration.v1", "platform": "windows-ntfs",
                        "server_sha256": digest, "cli_sha256": digest, "security_test_sha256": digest,
                        "storage_test_sha256": digest, "idydb_manifest_sha256": "a" * 64,
                        "page_sha256": package.sha(self.page.read_bytes()), "runtime_dlls": {},
                        "tests": {item: True for item in package.TESTS}}

    def verify(self):
        package.verify_receipt(self.receipt, self.server, self.cli, self.dependency, self.page)

    def test_matching_receipt_metadata(self):
        self.verify()

    def test_wine_cannot_certify_native(self):
        self.receipt["platform"] = "wine"
        with self.assertRaisesRegex(ValueError, "NTFS"): self.verify()

    def test_changed_application_rejected(self):
        self.server.write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "exact"): self.verify()

    def test_changed_interface_rejected(self):
        self.page.write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "exact"): self.verify()

    def test_untested_runtime_dll_rejected(self):
        self.dependency["runtime_dlls"] = [{"path": "bin/idydb.dll", "sha256": "b" * 64}]
        with self.assertRaisesRegex(ValueError, "runtime DLLs"): self.verify()

    def test_skipped_security_rejected(self):
        self.receipt["tests"]["windows_storage_security"] = False
        with self.assertRaisesRegex(ValueError, "must pass"): self.verify()

    def test_sample_is_explicitly_uninitialized(self):
        samples = package.sample_files()
        self.assertEqual(len(samples), 3)
        self.assertFalse(any(name.endswith((".key", ".idydb", ".exe", ".dll")) for name in samples))
        cfg = json.loads(samples["sample/new_vendor/exchange.cfg"])
        self.assertEqual(cfg["environment_id"], "new_vendor")
        self.assertIs(cfg["runtime"]["encryption"]["required"], True)
        self.assertNotIn("vendor_0x01", samples["sample/new_vendor/exchange.cfg"].decode())


if __name__ == "__main__":
    unittest.main()
