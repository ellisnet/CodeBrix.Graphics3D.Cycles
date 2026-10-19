#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Compatibility entry point: build reviewed in-repository dependencies.

The old broad Debian package downloader is intentionally retired. Compiler and
platform SDK installation is separate from the renderer dependency build.
"""
import subprocess
import sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
subprocess.run([sys.executable,str(ROOT/'tools/dependencies/build.py'),*sys.argv[1:]],check=True,cwd=ROOT)
