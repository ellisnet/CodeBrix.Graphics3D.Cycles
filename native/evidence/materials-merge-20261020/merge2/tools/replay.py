#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 2 copy of M3's replay helper: build the Linux render replay tool (maintained location) and replay one capture."""
import sys
from pathlib import Path
ROOT=Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles')
sys.path.insert(0,str(ROOT/'native/tools'))
import render_replay
render_replay.build('linux-x64')
render_replay.run('linux-x64',Path(sys.argv[1]).resolve(),Path(sys.argv[2]).resolve())
print(open(Path(sys.argv[2])/'validation.json').read())
