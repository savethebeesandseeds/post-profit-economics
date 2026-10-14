#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Real HTTP/native accounting and persistence, using isolated synthetic records."""
import concurrent.futures
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
import urllib.error
import urllib.request

BINARY = Path(sys.argv[1]).resolve()
PROJECT = Path(sys.argv[2]).resolve()
sys.argv = sys.argv[:1]
spec = importlib.util.spec_from_file_location("exchange_service", PROJECT / "code/interfaces/serve.py")
service = importlib.util.module_from_spec(spec)
spec.loader.exec_module(service)

class Transactions(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="exchange-events-")
        self.root = Path(self.tmp.name) / "operation"
        sample = PROJECT / "code/environments/vendor-operation"
        (self.root / "records").mkdir(parents=True)
        for name in ("exchange.cfg", "metadata.json", "records/opening.json"):
            shutil.copyfile(sample / name, self.root / name)
        self.db = self.root / "data/exchange.idydb"
        self.assertEqual(self.cli("initialize")[0], 0)
        self.server = service.make_server(BINARY, self.root, PROJECT / "web/operations/transactions.html", port=0)
        self.worker = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.worker.start()
        self.url = f"http://127.0.0.1:{self.server.server_port}"
        self.token = self.http("/api/session")[1]["token"]

    def tearDown(self):
        self.server.shutdown()
        self.server.server_close()
        self.worker.join(timeout=5)
        self.tmp.cleanup()

    def cli(self, command, body=None):
        r = subprocess.run([str(BINARY), command, str(self.root)],
                           input=json.dumps(body) if body is not None else None,
                           capture_output=True, text=True, timeout=10)
        return r.returncode, json.loads(r.stdout)

    def http(self, path, body=None, headers=None):
        hdr = {"Content-Type": "application/json", "Origin": self.url}
        if hasattr(self, "token"):
            hdr["X-Exchange-Token"] = self.token
        hdr.update(headers or {})
        req = urllib.request.Request(self.url + path,
              data=json.dumps(body).encode() if body is not None else None, headers=hdr)
        try:
            with urllib.request.urlopen(req, timeout=10) as r:
                raw = r.read()
                return r.status, json.loads(raw) if r.headers["Content-Type"].startswith("application/json") else raw
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read())

    def event(self, name, kind, details, revision=0, date="2026-10-15"):
        return {"expected_revision": revision, "event": {
            "schema_version": "exchange.transaction.v1", "environment_id": "vendor-operation",
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
        code, reopened = self.cli("inspect")
        self.assertEqual(code, 0, reopened)
        for key in ("state", "summary", "revision", "events"):
            self.assertEqual(reopened[key], final[key], key)
        self.assertEqual(self.digest(), digest)
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
