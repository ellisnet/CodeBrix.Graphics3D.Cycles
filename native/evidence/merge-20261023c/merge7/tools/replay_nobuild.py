#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 5 part 2: replay one Linux capture with the render_replay binary that part 1 built, WITHOUT rebuilding it.
Refuses to run if the binary's recorded source hashes differ from the current sources."""
import json, sys
from pathlib import Path
ROOT = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles'); sys.path.insert(0, str(ROOT/'native/tools'))
import render_replay
from device_gate import sha
render_replay.BASE = ROOT/'native/work/merge7/replay-build'  # merge wave 7: rebuilt (render_replay.cpp v9)
b = json.loads((render_replay.BASE/'linux-x64/build.json').read_text())
now = {str(p.relative_to(ROOT)): sha(p) for p in [ROOT/'native/tests/render_replay.cpp', ROOT/'native/src/webgpu_device.cpp', ROOT/'native/src/webgpu_device.h', ROOT/'native/include/cycles_backend.h', ROOT/'native/tools/render_replay.py']}
assert b['sourceSha256'] == now, 'render_replay binary does not match current sources: not rebuilding (rule)'
assert sha(render_replay.BASE/'linux-x64/render_replay') == b['driverSha256']
render_replay.run('linux-x64', Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve())
print(open(Path(sys.argv[2])/'validation.json').read())
