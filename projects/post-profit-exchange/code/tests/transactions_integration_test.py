#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real HTTP/native accounting and persistence, using isolated synthetic records."""
import concurrent.futures
import fcntl
import hashlib
import http.client
import json
from pathlib import Path
import selectors
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.request
import urllib.parse

BINARY = Path(sys.argv[1]).resolve()
PROJECT = Path(sys.argv[2]).resolve()
SERVER = Path(sys.argv[3]).resolve()
sys.argv = sys.argv[:1]

class Transactions(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="exchange-events-")
        self.addCleanup(self.tmp.cleanup)
        self.diagnostics = tempfile.TemporaryFile()
        self.addCleanup(self.diagnostics.close)
        self.server = None
        self.addCleanup(self.stop_server)
        self.root = Path(self.tmp.name) / "operation"
        sample = PROJECT / "environments/vendor_0x01"
        (self.root / "records").mkdir(parents=True)
        for name in ("exchange.cfg", "metadata.json", "records/opening.json"):
            shutil.copyfile(sample / name, self.root / name)
        # Existing transport/accounting cases deliberately retain the legacy
        # plaintext contract. Encryption and config v2 have separate cases.
        config_path = self.root / "exchange.cfg"
        config = json.loads(config_path.read_text())
        config["schema_version"] = "exchange.environment.v1"
        config.pop("runtime", None)
        config_path.write_text(json.dumps(config))
        self.db = self.root / "data/exchange.idydb"
        self.assertEqual(self.cli("initialize")[0], 0)
        self.start_server()

    def server_args(self, environment_id="vendor_0x01", port=0):
        return [
            str(SERVER), "--environment", str(self.root),
            "--environment-id", environment_id,
            "--page", str(PROJECT / "web/operations/transactions.html"),
            "--bind", "127.0.0.1", "--port", str(port)]

    def start_server(self):
        self.server = subprocess.Popen(self.server_args(),
            stdout=subprocess.PIPE, stderr=self.diagnostics, text=True)
        with selectors.DefaultSelector() as ready:
            ready.register(self.server.stdout, selectors.EVENT_READ)
            if not ready.select(timeout=10):
                self.fail("C++ application server did not report its listening address.")
        line = self.server.stdout.readline()
        if not line:
            self.fail("C++ application server failed to start: " + self.read_diagnostics())
        started = json.loads(line)
        self.assertEqual(Path(started["environment"]).resolve(), self.root.resolve())
        self.url = started["url"].rstrip("/")
        self.port = urllib.parse.urlsplit(self.url).port
        self.assertEqual(urllib.parse.urlsplit(self.url).hostname, "127.0.0.1")
        self.token = self.http("/api/session")[1]["token"]

    def stop_server(self):
        if self.server is not None:
            if self.server.poll() is None:
                self.server.terminate()
                try:
                    self.server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    self.server.kill()
                    self.server.wait(timeout=5)
            exit_code = self.server.returncode
            self.server.stdout.close()
            self.server = None
            self.assertEqual(exit_code, 0, "C++ application server did not shut down cleanly.")

    def read_diagnostics(self):
        self.diagnostics.seek(0)
        return self.diagnostics.read().decode("utf-8", errors="replace")

    def cli(self, command, body=None):
        r = subprocess.run([str(BINARY), command, str(self.root)],
                           input=json.dumps(body) if body is not None else None,
                           capture_output=True, text=True, timeout=10)
        return r.returncode, json.loads(r.stdout)

    def http(self, path, body=None, headers=None, timeout=10):
        hdr = {"Content-Type": "application/json", "Origin": self.url}
        if hasattr(self, "token"):
            hdr["X-Exchange-Token"] = self.token
        hdr.update(headers or {})
        req = urllib.request.Request(self.url + path,
              data=json.dumps(body).encode() if body is not None else None, headers=hdr)
        try:
            with urllib.request.urlopen(req, timeout=timeout) as r:
                raw = r.read()
                return r.status, json.loads(raw) if r.headers["Content-Type"].startswith("application/json") else raw
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read())

    def raw_http(self, body=b"", headers=None, method="POST",
                 path="/api/transactions", add_length=True, close_write=False):
        """Send exact HTTP bytes, preserving duplicate headers and incomplete bodies."""
        supplied = list(headers or [])
        replaced = {name.lower() for name, _ in supplied}
        defaults = [
            ("Host", f"127.0.0.1:{self.port}"), ("Origin", self.url),
            ("Content-Type", "application/json"), ("X-Exchange-Token", self.token),
            ("Connection", "close")]
        request_headers = [(name, value) for name, value in defaults if name.lower() not in replaced]
        request_headers.extend(supplied)
        if add_length and "content-length" not in replaced:
            request_headers.append(("Content-Length", str(len(body))))
        request = (f"{method} {path} HTTP/1.1\r\n" +
                   "".join(f"{name}: {value}\r\n" for name, value in request_headers) +
                   "\r\n").encode("ascii") + body
        with socket.create_connection(("127.0.0.1", self.port), timeout=5) as connection:
            connection.sendall(request)
            if close_write:
                connection.shutdown(socket.SHUT_WR)
            response = http.client.HTTPResponse(connection)
            response.begin()
            raw = response.read()
            result_headers = dict(response.getheaders())
            return response.status, result_headers, raw

    def assert_json_failure(self, response, expected):
        status, headers, raw = response
        accepted = {expected} if isinstance(expected, int) else set(expected)
        self.assertIn(status, accepted, raw)
        self.assertTrue(headers.get("Content-Type", "").startswith("application/json"), headers)
        body = json.loads(raw)
        self.assertEqual(body["status"], "error")
        self.assertIsInstance(body["message"], str)
        self.assertTrue(body["message"])
        return body

    def event(self, name, kind, details, revision=0, date="2026-10-15"):
        return {"expected_revision": revision, "event": {
            "schema_version": "exchange.transaction.v1", "environment_id": "vendor_0x01",
            "id": name, "date": date, "kind": kind, "details": details}}

    def digest(self):
        return hashlib.sha256(self.db.read_bytes()).hexdigest()

    def test_http_accounts_restart_and_retry(self):
        original = self.db.read_bytes()
        self.assertEqual(self.http("/")[0], 200)
        self.assertEqual(self.http("/api/state")[1]["revision"], 0)
        self.assertEqual(self.db.read_bytes(), original)
        delivery = self.event("delivery-1", "delivery",
            {"sku": "bread", "lot_id": "received-bread", "units": 10, "unit_cost": 120})
        code, received = self.http("/api/transactions", delivery)
        self.assertEqual(code, 200, received)
        self.assertEqual(received["commit_status"], "recorded")
        self.assertEqual(received["state"]["cash"], 98800)
        self.assertEqual(received["summary"]["inventory_value"], 10950)
        self.assertEqual(received["state"]["earned_funding"], 0)
        digest = self.digest()
        code, retry = self.http("/api/transactions", delivery)
        self.assertEqual(code, 200)
        self.assertEqual(retry["commit_status"], "already_recorded")
        self.assertEqual(retry["revision"], 1)
        self.assertEqual(self.digest(), digest)
        sale = self.event("sale-1", "sale", {"sku": "bread", "units": 50, "unit_price": 200}, 1)
        code, sold = self.http("/api/transactions", sale)
        self.assertEqual(code, 200, sold)
        self.assertEqual(sold["state"]["cash"], 108800)
        self.assertEqual(sold["summary"]["inventory_value"], 5850)
        self.assertEqual(sold["state"]["earned_funding"], 4900)
        self.assertEqual(sold["events"][-1]["effect"]["cost_of_goods_sold"], 5100)
        paid = self.event("expense-paid", "expense",
            {"category": "operating", "description": "Example electricity", "amount": 700, "settlement": "paid"}, 2, "2026-10-16")
        unpaid = self.event("expense-unpaid", "expense",
            {"category": "wages", "description": "Example unpaid work", "amount": 300, "settlement": "unpaid"}, 3, "2026-10-16")
        self.assertEqual(self.http("/api/transactions", paid)[0], 200)
        code, final = self.http("/api/transactions", unpaid)
        self.assertEqual(code, 200, final)
        self.assertEqual(final["state"]["cash"], 108100)
        self.assertEqual(final["summary"]["inventory_value"], 5850)
        self.assertEqual(final["summary"]["obligations"], 1300)
        self.assertEqual(final["state"]["earned_funding"], 3900)
        self.assertEqual(final["summary"]["net_assets"], 112650)
        self.assertEqual(final["summary"]["inventory_units"], 40)
        self.assertEqual(final["state"]["reserve"], 5000)
        self.assertEqual(final["state"]["products"][0]["published_price"], 200)
        self.assertEqual(final["state"]["products"][0]["replacement_cost"], 100)
        self.assertEqual(final["state"]["products"][0]["lots"], [{"id": "received-bread", "units": 5, "unit_cost": 120}])
        (self.root / "records/opening.json").unlink()
        digest = self.digest()
        old_token = self.token
        self.stop_server()
        code, reopened = self.cli("inspect")
        self.assertEqual(code, 0, reopened)
        for key in ("state", "summary", "revision", "events"):
            self.assertEqual(reopened[key], final[key], key)
        self.assertEqual(self.digest(), digest)
        self.start_server()
        self.assertNotEqual(self.token, old_token)
        self.assertEqual(self.http("/api/transactions", delivery,
                                   {"X-Exchange-Token": old_token})[0], 403)
        code, retry = self.http("/api/transactions", delivery)
        self.assertEqual(code, 200)
        self.assertEqual(retry["revision"], 4)
        self.assertEqual(retry["commit_status"], "already_recorded")
        self.assertEqual(self.digest(), digest)

    def test_rejections_and_origin(self):
        digest = self.digest()
        good = self.event("event-a", "sale", {"sku": "bread", "units": 1, "unit_price": 200})
        for headers, expected in [
            ({"X-Exchange-Token": "wrong"}, 403), ({"Origin": "http://example.invalid"}, 403),
            ({"Host": "example.invalid"}, 403), ({"Sec-Fetch-Site": "cross-site"}, 403),
            ({"Content-Type": "text/plain"}, 415)]:
            self.assertEqual(self.http("/api/transactions", good, headers)[0], expected)
            self.assertEqual(self.digest(), digest)
        for bad in [
            self.event("too-many", "sale", {"sku": "bread", "units": 46, "unit_price": 200}),
            self.event("delivery-lot", "delivery", {"sku": "bread", "lot_id": "opening-bread", "units": 1, "unit_cost": 100}),
            self.event("expense-x", "expense", {"category": "operating", "description": "x", "amount": 95001, "settlement": "paid"})]:
            code, error = self.http("/api/transactions", bad)
            self.assertEqual(code, 400, error)
            self.assertEqual(self.digest(), digest)
        stale = self.event("stale", "sale", {"sku": "bread", "units": 1, "unit_price": 200}, 1)
        code, rejected = self.http("/api/transactions", stale)
        self.assertEqual(code, 409)
        self.assertEqual(rejected["commit_status"], "not_recorded")
        self.assertEqual(self.digest(), digest)
        self.assertEqual(self.http("/api/transactions", good)[0], 200)
        digest = self.digest()
        absent = self.event("unreceived", "sale", {"sku": "bread", "units": 1, "unit_price": 200})
        code, rejected = self.http("/api/transactions", absent)
        self.assertEqual(code, 409)
        self.assertEqual(rejected["commit_status"], "not_recorded")
        self.assertEqual(self.digest(), digest)
        reused = json.loads(json.dumps(good))
        reused["event"]["details"]["units"] = 2
        code, rejected = self.http("/api/transactions", reused)
        self.assertEqual(code, 409)
        self.assertEqual(rejected["commit_status"], "not_recorded")
        self.assertEqual(self.digest(), digest)
        raw = '{"expected_revision":1,"expected_revision":1,"event":{}}'
        r = subprocess.run([str(BINARY), "transact", str(self.root)], input=raw, capture_output=True, text=True)
        self.assertEqual(r.returncode, 2)
        self.assertEqual(json.loads(r.stdout)["error_type"], "validation")
        self.assertEqual(self.digest(), digest)

    def test_startup_rejects_wrong_environment_and_occupied_port(self):
        digest = self.digest()
        for label, options in (
                ("wrong environment identity", {"environment_id": "different-operation"}),
                ("occupied application port", {"port": self.port})):
            with self.subTest(case=label):
                rejected = subprocess.run(self.server_args(**options), capture_output=True,
                                          text=True, timeout=10)
                self.assertNotEqual(rejected.returncode, 0)
                self.assertEqual(rejected.stdout, "", "A failed startup must not report readiness.")
                self.assertTrue(rejected.stderr.strip())
                self.assertEqual(self.digest(), digest)
        self.assertEqual(self.http("/api/state")[1]["revision"], 0)

    def test_routes_headers_and_read_only_requests(self):
        digest = self.digest()
        for path in ("/", "/?view=home", "/api/session", "/api/state"):
            with self.subTest(path=path):
                code, headers, body = self.raw_http(method="GET", path=path, add_length=False)
                self.assertEqual(code, 200, body)
                self.assertEqual(headers.get("Cache-Control"), "no-store")
                self.assertEqual(headers.get("X-Content-Type-Options"), "nosniff")
                self.assertEqual(headers.get("X-Frame-Options"), "DENY")
                self.assertEqual(headers.get("Referrer-Policy"), "no-referrer")
        for path in ("/missing", "/exchange.cfg", "/metadata.json",
                     "/records/opening.json", "/data/exchange.idydb"):
            with self.subTest(path=path):
                self.assert_json_failure(self.raw_http(method="GET", path=path), 404)
        for method, path in (("GET", "/api/transactions"), ("POST", "/api/state"),
                             ("PUT", "/api/transactions"), ("DELETE", "/api/transactions")):
            with self.subTest(method=method, path=path):
                self.assert_json_failure(self.raw_http(method=method, path=path), (404, 405))
        # Non-browser local clients need not provide Origin.
        request = urllib.request.Request(self.url + "/api/state")
        with urllib.request.urlopen(request, timeout=10) as response:
            self.assertEqual(response.status, 200)
        self.assertEqual(self.digest(), digest)

    def test_raw_request_validation_and_framing(self):
        digest = self.digest()
        good = json.dumps(self.event("framing", "sale",
                          {"sku": "bread", "units": 1, "unit_price": 200})).encode()
        duplicate_keys = good.replace(b'"expected_revision": 0',
                                     b'"expected_revision": 0, "expected_revision": 0')
        chunked = (f"{len(good):x}\r\n".encode() + good + b"\r\n0\r\n\r\n")
        cases = [
            ("duplicate JSON keys", duplicate_keys, {}, 400),
            ("malformed JSON", b'{"event":', {}, 400),
            ("empty body", b"", {}, 413),
            ("oversized body", b" " * 16385, {}, 413),
            ("chunked body", chunked,
             {"headers": [("Transfer-Encoding", "chunked")], "add_length": False}, 400),
            ("duplicate lengths", good,
             {"headers": [("Content-Length", str(len(good))),
                          ("Content-Length", str(len(good)))]}, 400),
            ("invalid length", good,
             {"headers": [("Content-Length", "not-a-length")]}, 400),
            ("missing length", good, {"add_length": False, "close_write": True}, 400),
            ("incomplete body", good,
             {"headers": [("Content-Length", str(len(good) + 1))], "close_write": True}, 400),
        ]
        for label, body, options, expected in cases:
            with self.subTest(case=label):
                self.assert_json_failure(self.raw_http(body, **options), expected)
                self.assertEqual(self.digest(), digest)
                self.assertEqual(self.http("/api/state")[1]["revision"], 0)

    def test_duplicate_security_headers(self):
        digest = self.digest()
        good = json.dumps(self.event("header-check", "sale",
                          {"sku": "bread", "units": 1, "unit_price": 200})).encode()
        for name, value in (("Host", f"127.0.0.1:{self.port}"),
                            ("Origin", self.url), ("X-Exchange-Token", self.token)):
            with self.subTest(header=name):
                self.assert_json_failure(
                    self.raw_http(good, [(name, value), (name.lower(), value)]),
                    400 if name == "Host" else 403)
                self.assertEqual(self.digest(), digest)

    def test_concurrent_identical_retries_are_recorded_once(self):
        body = self.event("same-submission", "expense", {
            "category": "operating", "description": "Private-test-account-entry-789",
            "amount": 100, "settlement": "paid"})
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            results = list(pool.map(lambda _: self.http("/api/transactions", body), range(2)))
        self.assertEqual([code for code, _ in results], [200, 200])
        self.assertEqual(sorted(result["commit_status"] for _, result in results),
                         ["already_recorded", "recorded"])
        current = self.http("/api/state")[1]
        self.assertEqual(current["revision"], 1)
        self.assertEqual(len(current["events"]), 1)
        self.assertEqual(current["state"]["cash"], 99900)
        self.assertEqual(current["state"]["earned_funding"], -100)
        token = self.token
        self.stop_server()
        diagnostics = self.read_diagnostics()
        self.assertNotIn(token, diagnostics)
        self.assertNotIn("Private-test-account-entry-789", diagnostics)

    def test_slow_writer_has_bounded_uncertain_response_and_safe_retry(self):
        body = self.event("delayed-submission", "expense", {
            "category": "operating", "description": "Example delayed payment",
            "amount": 100, "settlement": "paid"})
        with open(str(self.db) + ".lock", "rb") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            started = time.monotonic()
            code, delayed = self.http("/api/transactions", body, timeout=30)
            elapsed = time.monotonic() - started
            self.assertEqual(code, 500, delayed)
            self.assertEqual(delayed["status"], "error")
            self.assertNotEqual(delayed.get("commit_status"), "not_recorded")
            self.assertIn("same submission", delayed["message"].lower())
            self.assertGreaterEqual(elapsed, 19)
            self.assertLess(elapsed, 30)
        # A timed-out response does not cancel or roll back an in-flight write.
        code, retried = self.http("/api/transactions", body)
        self.assertEqual(code, 200, retried)
        self.assertIn(retried["commit_status"], ("recorded", "already_recorded"))
        self.assertEqual(retried["revision"], 1)
        self.assertEqual(len(retried["events"]), 1)
        self.assertEqual(retried["state"]["cash"], 99900)
        self.assertEqual(retried["state"]["earned_funding"], -100)
        digest = self.digest()
        self.assertEqual(self.http("/api/transactions", body)[1]["commit_status"], "already_recorded")
        self.assertEqual(self.digest(), digest)

    def test_competing_forms_and_depleted_lot_identity(self):
        a = self.event("parallel-a", "sale", {"sku": "bread", "units": 1, "unit_price": 200})
        b = self.event("parallel-b", "sale", {"sku": "bread", "units": 1, "unit_price": 200})
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            statuses = list(pool.map(lambda body: self.http("/api/transactions", body)[0], [a, b]))
        self.assertEqual(sorted(statuses), [200, 409])
        current = self.http("/api/state")[1]
        self.assertEqual(current["revision"], 1)
        self.assertEqual(current["state"]["cash"], 100200)
        sale = self.event("empty-lot", "sale", {"sku": "bread", "units": 44, "unit_price": 200}, 1)
        self.assertEqual(self.http("/api/transactions", sale)[0], 200)
        reused = self.event("reuse-lot", "delivery", {"sku": "bread", "lot_id": "opening-bread", "units": 1, "unit_cost": 100}, 2)
        digest = self.digest()
        self.assertEqual(self.http("/api/transactions", reused)[0], 400)
        self.assertEqual(self.digest(), digest)

if __name__ == "__main__":
    unittest.main()
