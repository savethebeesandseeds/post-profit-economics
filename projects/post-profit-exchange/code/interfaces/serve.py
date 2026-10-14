#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Local HTTP transport. All records and account changes are handled by C++."""
import argparse
import json
import secrets
import subprocess
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlsplit

MAX_REQUEST = 16384

def make_server(binary, environment, page, bind="127.0.0.1", port=8765):
    binary, environment, page = Path(binary).resolve(), Path(environment).resolve(), Path(page).resolve()
    if not binary.is_file() or not environment.is_dir() or not page.is_file():
        raise ValueError("binary, environment directory and application page must exist")
    token = secrets.token_urlsafe(32)
    html = page.read_bytes()

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.0"
        server_version = "ExchangeLocal/1"
        def log_message(self, fmt, *args):
            # No request bodies, financial data or session token in the log.
            pass

        def reply(self, status, content, kind="application/json; charset=utf-8"):
            body = json.dumps(content, ensure_ascii=False).encode() if isinstance(content, dict) else content
            self.send_response(status)
            self.send_header("Content-Type", kind)
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.send_header("X-Content-Type-Options", "nosniff")
            self.send_header("X-Frame-Options", "DENY")
            self.send_header("Referrer-Policy", "no-referrer")
            self.end_headers()
            self.wfile.write(body)

        def failure(self, status, message):
            self.reply(status, {"status": "error", "message": message})

        def local_request(self):
            allowed = {f"127.0.0.1:{self.server.server_port}", f"localhost:{self.server.server_port}"}
            host = self.headers.get("Host", "")
            if host not in allowed:
                self.failure(403, "Use the configured local application URL.")
                return False
            origin = self.headers.get("Origin")
            if origin is not None and origin != "http://" + host:
                self.failure(403, "Cross-origin requests are not accepted.")
                return False
            if self.headers.get("Sec-Fetch-Site") == "cross-site":
                self.failure(403, "Cross-site requests are not accepted.")
                return False
            return True

        def native(self, command, body=None):
            try:
                result = subprocess.run([str(binary), command, str(environment)],
                                        input=body, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                        timeout=20, check=False)
                response = json.loads(result.stdout)
                if not isinstance(response, dict):
                    raise ValueError("unexpected application response")
                if result.returncode == 0:
                    self.reply(200, response)
                else:
                    error_type = response.get("error_type", "storage")
                    status = {"validation": 400, "conflict": 409}.get(error_type, 500)
                    self.reply(status, response)
            except subprocess.TimeoutExpired:
                self.failure(500, "The application did not confirm completion. Retry the same submission to check its status.")
            except (OSError, ValueError):
                self.failure(500, "The application response is unavailable. Retry the same submission to check its status.")

        def do_GET(self):
            if not self.local_request():
                return
            path = urlsplit(self.path).path
            if path == "/":
                self.reply(200, html, "text/html; charset=utf-8")
            elif path == "/api/session":
                self.reply(200, {"token": token})
            elif path == "/api/state":
                self.native("inspect")
            else:
                self.failure(404, "Unknown application route.")

        def do_POST(self):
            if not self.local_request():
                return
            if urlsplit(self.path).path != "/api/transactions":
                self.failure(404, "Unknown application route.")
                return
            if not secrets.compare_digest(self.headers.get("X-Exchange-Token", ""), token):
                self.failure(403, "This application session is not current. Reload before recording.")
                return
            if self.headers.get("Transfer-Encoding") is not None:
                self.failure(400, "Chunked requests are not supported.")
                return
            if self.headers.get("Content-Type", "").split(";")[0].strip().lower() != "application/json":
                self.failure(415, "Transactions must use application/json.")
                return
            lengths = self.headers.get_all("Content-Length", [])
            if len(lengths) != 1 or not lengths[0].isdigit():
                self.failure(400, "One valid request length is required.")
                return
            size = int(lengths[0])
            if not 0 < size <= MAX_REQUEST:
                self.failure(413, "Transaction request must contain 1 to 16384 bytes.")
                return
            self.connection.settimeout(10)
            try:
                body = self.rfile.read(size)
            except (TimeoutError, OSError):
                self.failure(400, "Request was incomplete; no transaction was submitted.")
                return
            if len(body) != size:
                self.failure(400, "Request was incomplete; no transaction was submitted.")
                return
            # Preserve raw JSON so C++ can reject duplicate keys and validate exact integers.
            self.native("transact", body)

    server = ThreadingHTTPServer((bind, port), Handler)
    server.daemon_threads = True
    return server

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--environment", type=Path, required=True)
    parser.add_argument("--page", type=Path, required=True)
    parser.add_argument("--bind", choices=["127.0.0.1", "0.0.0.0"], default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    args = parser.parse_args()
    if not 0 <= args.port <= 65535:
        parser.error("port is outside the supported range")
    server = make_server(args.binary, args.environment, args.page, args.bind, args.port)
    print(json.dumps({"url": f"http://127.0.0.1:{server.server_port}/",
                      "environment": str(args.environment.resolve())}), flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()

if __name__ == "__main__":
    main()
