#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 8: rebuild render_replay (capture format v10, 27 entries) for linux-x64 into the default
native/work/render/replay-build (used by the gate drivers' --skip-build replays) and for linux-x64 / android-arm64 /
android-x64 into native/work/merge8/replay-build (staged for part 2)."""
import sys
from pathlib import Path
ROOT = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles'); sys.path.insert(0, str(ROOT/'native/tools'))
import render_replay
render_replay.build('linux-x64')
render_replay.BASE = ROOT/'native/work/merge8/replay-build'
for rid in ('linux-x64', 'android-arm64', 'android-x64'): render_replay.build(rid)
