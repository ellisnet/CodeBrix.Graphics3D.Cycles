#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E5: write a probe WGSL variant (diagnostic copies only): mkvar.py BASE OUT REPLACEMENT_JSON
REPLACEMENT_JSON = [[old, new], ...]; each old must occur exactly once."""
import json, sys
from pathlib import Path
s = Path(sys.argv[1]).read_text()
for old, new in json.loads(Path(sys.argv[3]).read_text() if Path(sys.argv[3]).exists() else sys.argv[3]):
    assert s.count(old) == 1, (old[:80], s.count(old)); s = s.replace(old, new)
Path(sys.argv[2]).write_text(s)
