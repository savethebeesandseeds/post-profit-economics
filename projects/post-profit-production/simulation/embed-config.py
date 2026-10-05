#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Embed the existing example configuration without changing its JSON tokens."""
import pathlib
import sys

if len(sys.argv) != 2:
    raise SystemExit("Usage: embed-config.py OUTPUT_HEADER")

project = pathlib.Path(__file__).resolve().parent.parent
text = (project / "configs/research.cfg").read_text(encoding="utf-8")
if ')PHPCFG"' in text:
    raise SystemExit("Configuration contains reserved C++ embedding delimiter")

template = (project / "simulation/default_config.hpp.in").read_text(encoding="utf-8")
if template.count("@PH_PRODUCTION_DEFAULTS@") != 1:
    raise SystemExit("Expected one production default-configuration marker")

destination = pathlib.Path(sys.argv[1])
destination.parent.mkdir(parents=True, exist_ok=True)
destination.write_text(template.replace("@PH_PRODUCTION_DEFAULTS@", text), encoding="utf-8", newline="\n")
