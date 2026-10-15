#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Native encrypted environments and HTTP, using isolated synthetic accounts."""
import copy
import json
import os
from pathlib import Path
import selectors
import shutil
import socket
import subprocess
import sys
import tempfile
import unittest
import urllib.error
import urllib.parse
import urllib.request

BINARY = Path(sys.argv[1]).resolve()
PROJECT = Path(sys.argv[2]).resolve()
SERVER = Path(sys.argv[3]).resolve()
sys.argv = sys.argv[:1]


class EncryptedEnvironment(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="exchange-encryption-")
        self.addCleanup(self.tmp.cleanup)
        self.directory = Path(self.tmp.name)
        self.key = self.directory / "keys/operation.key"
        self.key.parent.mkdir()
        # This fixed secret belongs exclusively to deterministic test fixtures.
        self.key.write_text("0123456789abcdef" * 4 + "\n")
        self.key.chmod(0o600)
        self.server = None
        self.diagnostics = tempfile.TemporaryFile()
        self.addCleanup(self.diagnostics.close)
        self.addCleanup(self.stop_server)
        self.source = self.fixture("old", "legacy-vendor")
        self.destination = self.fixture("new", "vendor_0x01", encrypted=True)
        self.assert_ok(self.cli("initialize", self.source))

    def fixture(self, name, environment_id, encrypted=False):
        root = self.directory / name
        (root / "records").mkdir(parents=True)
        config = {
            "schema_version": "exchange.environment.v1",
            "environment_id": environment_id, "currency": "EUR",
            "minor_unit_digits": 2, "storage": {"database": "data/exchange.idydb"},
            "metadata_file": "metadata.json", "opening_file": "records/opening.json"}
        if encrypted:
            config["schema_version"] = "exchange.environment.v2"
            config["runtime"] = {
                "workspace": ".", "server": {"bind": "127.0.0.1", "port": 18766},
                "encryption": {"required": True, "key_file": str(self.key)}}
        metadata = {
            "schema_version": "exchange.environment-metadata.v1",
            "display_name": "Encrypted test operation" if encrypted else "Original test operation",
            "description": "Synthetic deterministic accounting fixture.",
            "provenance": "synthetic",
            "products": [{"sku": "bread", "label": "Bread", "unit": "piece"}]}
        opening = {
            "schema_version": "exchange.opening.v1", "environment_id": environment_id,
            "as_of": "2026-10-14", "cash": 100000, "reserve": 5000, "earned_funding": 0,
            "obligations": [{"id": "opening-wages", "kind": "wages", "amount": 800},
                            {"id": "opening-operating", "kind": "operating", "amount": 200}],
            "products": [{"sku": "bread", "published_price": 200, "replacement_cost": 100,
                          "lots": [{"id": "opening-bread", "units": 45, "unit_cost": 100}]}]}
        for name, value in (("exchange.cfg", config), ("metadata.json", metadata),
                            ("records/opening.json", opening)):
            (root / name).write_text(json.dumps(value))
        return root

    def cli(self, command, root, *extra, key=None, body=None):
        args = [str(BINARY), command, str(root), *map(str, extra)]
        if key is not None:
            args.extend(("--key-file", str(key)))
        result = subprocess.run(args, input=json.dumps(body) if body is not None else None,
                                capture_output=True, text=True, timeout=15)
        self.assertNotIn(self.key.read_text().strip(), result.stdout + result.stderr,
                         "native diagnostics must not expose the key")
        try:
            output = json.loads(result.stdout)
        except json.JSONDecodeError:
            self.fail("Native command did not produce JSON: " + result.stdout + result.stderr)
        return result.returncode, output

    def assert_ok(self, response):
        code, body = response
        self.assertEqual(code, 0, body)
        return body

    def assert_rejected(self, response):
        code, body = response
        self.assertNotEqual(code, 0, body)
        self.assertEqual(body["status"], "error")
        return body

    @staticmethod
    def files(root):
        return {str(p.relative_to(root)): p.read_bytes()
                for p in root.rglob("*") if p.is_file()}

    @staticmethod
    def database(root):
        return root / "data/exchange.idydb"

    def event(self, identity, revision, kind, details, environment="legacy-vendor"):
        return {"expected_revision": revision, "event": {
            "schema_version": "exchange.transaction.v1", "environment_id": environment,
            "id": identity, "date": "2026-10-15", "kind": kind, "details": details}}

    def record_history(self):
        events = [
            self.event("delivery-1", 0, "delivery",
                       {"sku": "bread", "lot_id": "received-bread", "units": 10, "unit_cost": 120}),
            self.event("sale-1", 1, "sale", {"sku": "bread", "units": 50, "unit_price": 200}),
            self.event("expense-1", 2, "expense",
                       {"category": "operating", "description": "Electricity fixture",
                        "amount": 700, "settlement": "paid"})]
        for event in events:
            final = self.assert_ok(self.cli("transact", self.source, body=event))
        self.assertEqual(final["revision"], 3)
        self.assertEqual(final["state"]["cash"], 108100)
        self.assertEqual(final["state"]["earned_funding"], 4200)
        self.assertEqual(final["summary"]["inventory_value"], 600)
        self.assertEqual(final["summary"]["inventory_units"], 5)
        self.assertEqual(final["summary"]["net_assets"], 107700)
        self.assertEqual(final["events"][1]["effect"]["cost_of_goods_sold"], 5100)
        return self.assert_ok(self.cli("inspect", self.source))

    def migrate(self):
        return self.assert_ok(self.cli("migrate", self.source, self.destination, key=self.key))

    def assert_accounts_equal(self, actual, expected, renamed=False):
        expected = copy.deepcopy(expected)
        if renamed:
            expected["state"]["environment_id"] = "vendor_0x01"
            for entry in expected["events"]:
                entry["event"]["environment_id"] = "vendor_0x01"
        for field in ("state", "summary", "revision", "events"):
            self.assertEqual(actual[field], expected[field], field)

    def assert_runtime_private(self, report):
        self.assertNotIn("runtime", report["environment"])
        text = json.dumps(report)
        self.assertNotIn(str(self.key), text)
        self.assertNotIn(self.key.read_text().strip(), text)
        self.assertNotIn('"key_file"', text)
        self.assertNotIn('"workspace"', text)

    def test_offline_migration_preserves_history_and_recovery(self):
        original = self.record_history()
        before = self.files(self.source)
        result = self.migrate()
        self.assertEqual(result["status"], "migrated")
        self.assertEqual(result["previous_environment_id"], "legacy-vendor")
        migrated = self.assert_ok(self.cli("inspect", self.destination, key=self.key))
        self.assert_accounts_equal(migrated, original, renamed=True)
        self.assertEqual(self.files(self.source), before)
        self.assert_runtime_private(migrated)
        for path in (self.database(self.destination),
                     self.destination / "data/migration-original.idydb"):
            raw = path.read_bytes()
            self.assertTrue(raw.startswith(b"IDYDBENC"))
            self.assertNotIn(b"Electricity fixture", raw)
            self.assertNotIn(b'"cash"', raw)

        # Restore the encrypted recovery into an isolated matching operation.
        # This exercises the public reader and reconstructs every original event
        # and effect without writing a plaintext database or custom decryptor.
        recovered = self.fixture("recovered", "legacy-vendor", encrypted=True)
        shutil.copyfile(self.source / "metadata.json", recovered / "metadata.json")
        (recovered / "data").mkdir()
        shutil.copyfile(self.destination / "data/migration-original.idydb", self.database(recovered))
        report = self.assert_ok(self.cli("inspect", recovered, key=self.key))
        self.assert_accounts_equal(report, original)
        self.assertEqual(report["environment"], original["environment"])
        self.assertEqual(report["metadata"], original["metadata"])
        self.assertEqual(self.files(self.source), before)

    def test_runtime_settings_change_without_account_migration(self):
        self.migrate()
        before = self.database(self.destination).read_bytes()
        report = self.assert_ok(self.cli("inspect", self.destination, key=self.key))
        config_path = self.destination / "exchange.cfg"
        cfg = json.loads(config_path.read_text())
        cfg["runtime"]["server"]["port"] += 1
        cfg["runtime"]["encryption"]["key_file"] = str(self.directory / "relocated-key")
        config_path.write_text(json.dumps(cfg))
        relocated_key = self.directory / "relocated-key"
        shutil.copyfile(self.key, relocated_key)
        reopened = self.assert_ok(self.cli("inspect", self.destination, key=relocated_key))
        self.assertEqual(reopened["environment"], report["environment"])
        self.assert_accounts_equal(reopened, report)
        self.assert_runtime_private(reopened)
        self.assertNotIn(str(relocated_key), json.dumps(reopened))
        self.assertEqual(self.database(self.destination).read_bytes(), before)

    def test_missing_wrong_and_malformed_keys_preserve_ciphertext(self):
        self.migrate()
        before = self.files(self.destination)
        self.assert_rejected(self.cli("inspect", self.destination))
        invalid_keys = [b"f" * 64, b"a" * 63, b"a" * 65, b"g" * 64,
                        b"a" * 31 + b"\0" + b"a" * 32,
                        self.key.read_bytes().rstrip(b"\n") + b"\r"]
        for number, value in enumerate(invalid_keys):
            with self.subTest(value=number):
                bad = self.directory / ("invalid-key-" + str(number))
                bad.write_bytes(value)
                self.assert_rejected(self.cli("inspect", self.destination, key=bad))
                event = self.event("blocked", 0, "sale",
                                   {"sku": "bread", "units": 1, "unit_price": 200},
                                   environment="vendor_0x01")
                self.assert_rejected(self.cli("transact", self.destination, key=bad, body=event))
                self.assertEqual(self.files(self.destination), before)
        linked = self.directory / "linked.key"
        linked.symlink_to(self.key)
        self.assert_rejected(self.cli("inspect", self.destination, key=linked))
        fifo = self.directory / "pipe.key"
        os.mkfifo(fifo)
        self.assert_rejected(self.cli("inspect", self.destination, key=fifo))
        self.assert_rejected(self.cli("inspect", self.destination, key=self.directory / "absent.key"))
        self.assertEqual(self.files(self.destination), before)
        good_crlf = self.directory / "crlf.key"
        good_crlf.write_bytes(self.key.read_bytes().rstrip(b"\n") + b"\r\n")
        self.assert_ok(self.cli("inspect", self.destination, key=good_crlf))

    def test_server_rejects_unavailable_keys_without_listening(self):
        self.migrate()
        before = self.files(self.destination)
        wrong = self.directory / "wrong-server.key"
        wrong.write_text("f" * 64)
        pipe = self.directory / "server-pipe.key"
        os.mkfifo(pipe)
        base = [str(SERVER), "--config", str(self.destination / "exchange.cfg"),
                "--page", str(PROJECT / "web/operations/transactions.html")]
        for key in (None, self.directory / "missing-server.key", wrong, pipe):
            with self.subTest(key=key):
                args = base + (["--key-file", str(key)] if key is not None else [])
                response = subprocess.run(args, capture_output=True, text=True, timeout=5)
                self.assertNotEqual(response.returncode, 0)
                self.assertNotIn('"url"', response.stdout)
                self.assertNotIn(self.key.read_text().strip(), response.stdout + response.stderr)
                self.assertEqual(self.files(self.destination), before)

    def test_fresh_encrypted_initialize_and_plaintext_mode_refusal(self):
        self.assert_rejected(self.cli("initialize", self.destination))
        self.assertFalse(self.database(self.destination).exists())
        initialized = self.assert_ok(self.cli("initialize", self.destination, key=self.key))
        self.assertEqual(initialized["revision"], 0)
        before = self.files(self.destination)
        self.assertTrue(self.database(self.destination).read_bytes().startswith(b"IDYDBENC"))
        self.assert_rejected(self.cli("initialize", self.destination, key=self.key))
        self.assertEqual(self.files(self.destination), before)
        original = self.files(self.source)
        self.assert_rejected(self.cli("inspect", self.source, key=self.key))
        self.assertEqual(self.files(self.source), original)

        # Changing configuration alone must not migrate the plaintext database.
        cfg = json.loads((self.source / "exchange.cfg").read_text())
        cfg["schema_version"] = "exchange.environment.v2"
        cfg["runtime"] = json.loads((self.destination / "exchange.cfg").read_text())["runtime"]
        (self.source / "exchange.cfg").write_text(json.dumps(cfg))
        db_before = self.database(self.source).read_bytes()
        self.assert_rejected(self.cli("inspect", self.source, key=self.key))
        self.assertEqual(self.database(self.source).read_bytes(), db_before)

    def test_report_requires_explicit_plaintext_export(self):
        self.migrate()
        template = PROJECT / "web/operations/environment.template.html"
        before = self.database(self.destination).read_bytes()
        self.assert_rejected(self.cli("render", self.destination, template, key=self.key))
        self.assertFalse((self.destination / "reports").exists())
        exported = self.assert_ok(self.cli("render", self.destination, template,
                                           "--allow-plaintext-export", key=self.key))
        path = Path(exported["report"])
        self.assertEqual(path, self.destination / "reports/environment.html")
        text = path.read_text()
        self.assertIn("vendor_0x01", text)
        self.assertNotIn("__ENVIRONMENT_JSON__", text)
        self.assertNotIn(str(self.key), text)
        self.assertNotIn(self.key.read_text().strip(), text)
        self.assertEqual(self.database(self.destination).read_bytes(), before)

    def test_invalid_runtime_configuration_cannot_change_accounts(self):
        self.migrate()
        cfg_path = self.destination / "exchange.cfg"
        valid = json.loads(cfg_path.read_text())
        before = self.database(self.destination).read_bytes()
        variants = []
        for field, value in (("port", 0), ("port", 1023), ("port", 65536), ("port", "8766"),
                             ("bind", "0.0.0.0")):
            bad = copy.deepcopy(valid)
            bad["runtime"]["server"][field] = value
            variants.append(bad)
        for field, value in (("required", False), ("required", "true"), ("key_file", "")):
            bad = copy.deepcopy(valid)
            bad["runtime"]["encryption"][field] = value
            variants.append(bad)
        bad = copy.deepcopy(valid)
        bad["runtime"]["unexpected"] = True
        variants.append(bad)
        for number, cfg in enumerate(variants):
            with self.subTest(config=number):
                cfg_path.write_text(json.dumps(cfg))
                self.assert_rejected(self.cli("inspect", self.destination, key=self.key))
                self.assertEqual(self.database(self.destination).read_bytes(), before)
        cfg_path.write_text(json.dumps(valid))

    def test_migration_rejects_mismatch_and_existing_destination(self):
        original_source = self.files(self.source)
        original_destination = self.files(self.destination)
        changes = [
            ("exchange.cfg", "currency", "USD"),
            ("metadata.json", "description", "Changed account metadata"),
            ("records/opening.json", "cash", 99999)]
        for filename, field, value in changes:
            with self.subTest(changed=filename):
                path = self.destination / filename
                original = path.read_bytes()
                changed = json.loads(original)
                changed[field] = value
                path.write_text(json.dumps(changed))
                self.assert_rejected(self.cli("migrate", self.source, self.destination, key=self.key))
                self.assertFalse((self.destination / "data").exists())
                path.write_bytes(original)
                self.assertEqual(self.files(self.source), original_source)
        self.assertEqual(self.files(self.destination), original_destination)
        self.migrate()
        before = self.files(self.destination)
        self.assert_rejected(self.cli("migrate", self.source, self.destination, key=self.key))
        self.assertEqual(self.files(self.destination), before)
        self.assertEqual(self.files(self.source), original_source)

    def http(self, path, body=None):
        headers = {"Content-Type": "application/json", "Origin": self.url}
        if getattr(self, "token", None):
            headers["X-Exchange-Token"] = self.token
        request = urllib.request.Request(self.url + path, headers=headers,
                    data=json.dumps(body).encode() if body is not None else None)
        try:
            response = urllib.request.urlopen(request, timeout=15)
        except urllib.error.HTTPError as error:
            response = error
        with response:
            raw = response.read()
            return response.status, (json.loads(raw) if
                response.headers.get("Content-Type", "").startswith("application/json") else raw)

    def read_diagnostics(self):
        self.diagnostics.seek(0)
        return self.diagnostics.read().decode(errors="replace")

    def start_server(self):
        # Configuration requires a real port. Use the OS allocator rather than
        # a fixed number; retry only the tiny release-to-listen collision window.
        for attempt in range(3):
            with socket.socket() as reserved:
                reserved.bind(("127.0.0.1", 0))
                port = reserved.getsockname()[1]
            cfg_path = self.destination / "exchange.cfg"
            cfg = json.loads(cfg_path.read_text())
            cfg["runtime"]["server"]["port"] = port
            cfg_path.write_text(json.dumps(cfg))
            self.diagnostics.seek(0)
            self.diagnostics.truncate()
            self.server = subprocess.Popen([
                str(SERVER), "--config", str(cfg_path), "--key-file", str(self.key),
                "--page", str(PROJECT / "web/operations/transactions.html")],
                stdout=subprocess.PIPE, stderr=self.diagnostics, text=True)
            with selectors.DefaultSelector() as ready:
                ready.register(self.server.stdout, selectors.EVENT_READ)
                if not ready.select(timeout=15):
                    self.fail("Encrypted C++ server did not report readiness.")
            line = self.server.stdout.readline()
            if not line:
                self.server.wait(timeout=5)
                self.server.stdout.close()
                self.server = None
                detail = self.read_diagnostics()
                if attempt < 2 and ("bind" in detail.lower() or "listen" in detail.lower()):
                    continue
                self.fail("Encrypted server startup failed: " + detail)
            started = json.loads(line)
            self.url = started["url"].rstrip("/")
            url = urllib.parse.urlsplit(self.url)
            self.assertEqual(url.hostname, "127.0.0.1")
            self.assertEqual(url.port, port, "server must read its port from exchange.cfg")
            self.assertEqual(Path(started["environment"]).resolve(), self.destination.resolve())
            self.token = None
            status, session = self.http("/api/session")
            self.assertEqual(status, 200, session)
            self.token = session["token"]
            return
        self.fail("Unable to obtain a free test port.")

    def stop_server(self):
        if self.server is None:
            return
        server, self.server = self.server, None
        if server.poll() is None:
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait(timeout=5)
        server.stdout.close()
        self.assertEqual(server.returncode, 0, self.read_diagnostics())

    def test_encrypted_http_transactions_retry_and_restart(self):
        original = self.record_history()
        self.migrate()
        self.start_server()
        self.assertEqual(self.http("/")[0], 200)
        status, before = self.http("/api/state")
        self.assertEqual(status, 200, before)
        self.assert_accounts_equal(before, original, renamed=True)
        self.assert_runtime_private(before)
        for private_path in ("/exchange.cfg", "/data/exchange.idydb", "/keys/operation.key"):
            self.assertEqual(self.http(private_path)[0], 404)
        request = self.event("encrypted-sale", 3, "sale",
                            {"sku": "bread", "units": 1, "unit_price": 200},
                            environment="vendor_0x01")
        status, recorded = self.http("/api/transactions", request)
        self.assertEqual(status, 200, recorded)
        self.assertEqual(recorded["commit_status"], "recorded")
        self.assertEqual(recorded["revision"], 4)
        self.assertEqual(recorded["state"]["cash"], 108300)
        self.assertEqual(recorded["summary"]["inventory_units"], 4)
        self.assertEqual(recorded["summary"]["inventory_value"], 480)
        self.assertEqual(recorded["state"]["earned_funding"], 4280)
        self.assert_runtime_private(recorded)
        saved = self.database(self.destination).read_bytes()
        self.assertTrue(saved.startswith(b"IDYDBENC"))
        status, retry = self.http("/api/transactions", request)
        self.assertEqual(status, 200, retry)
        self.assertEqual(retry["commit_status"], "already_recorded")
        self.assertEqual(self.database(self.destination).read_bytes(), saved)
        # Keys are reread for each native operation. Losing access after launch
        # must refuse reads/writes, never downgrade the live database or expose
        # previously decrypted accounts as a misleading successful response.
        good_key = self.key.read_bytes()
        try:
            self.key.write_text("f" * 64)
            for path, body in (("/api/state", None), ("/api/transactions", request)):
                status, rejected = self.http(path, body)
                self.assertEqual(status, 500, rejected)
                self.assertEqual(rejected["status"], "error")
                self.assertNotIn("state", rejected)
                self.assertEqual(self.database(self.destination).read_bytes(), saved)
        finally:
            self.key.write_bytes(good_key)
        self.stop_server()
        reopened = self.assert_ok(self.cli("inspect", self.destination, key=self.key))
        self.assert_accounts_equal(reopened, recorded)
        self.start_server()
        status, restarted = self.http("/api/state")
        self.assertEqual(status, 200, restarted)
        self.assert_accounts_equal(restarted, recorded)
        self.assertEqual(self.database(self.destination).read_bytes(), saved)
        self.stop_server()
        self.assertNotIn(self.key.read_text().strip(), self.read_diagnostics())


if __name__ == "__main__":
    unittest.main()
