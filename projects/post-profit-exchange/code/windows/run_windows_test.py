#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run a cross-compiled Windows test inside the managed Linux container."""
import os
import pathlib
import subprocess
import sys
if not pathlib.Path("/.dockerenv").is_file():
    raise SystemExit("Windows development tests run inside the managed container")
environment = dict(os.environ, WINEPREFIX="/opt/post-profit-exchange/windows/wine",
                   WINEARCH="win64", WINEDEBUG="-all", LC_ALL="C.UTF-8")
result = subprocess.run(["/usr/lib/wine/wine64", *sys.argv[1:]], env=environment,
                        capture_output=True, timeout=80, check=False)
if result.returncode:
    sys.stdout.buffer.write(result.stdout)
    sys.stderr.buffer.write(result.stderr)
    raise SystemExit(result.returncode)
if sys.argv[-1] == "--output" and result.stdout.replace(b"\r\n", b"\n") != b'{"ready":true}\n':
    raise SystemExit("diagnostic output leaked into the readiness response")
sys.stdout.buffer.write(result.stdout)
