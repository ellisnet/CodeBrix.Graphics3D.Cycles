#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""M4 copy of the M3 measurement helper (from M1): build the Linux render replay tool and replay one capture."""
import sys
from pathlib import Path
ROOT=Path('/home/jeremy/ClaudeHome/cycles-worktrees/m4-bump-derivative')
sys.path.insert(0,str(ROOT/'native/tools'))
import render_replay
render_replay.build('linux-x64')
render_replay.run('linux-x64',Path(sys.argv[1]).resolve(),Path(sys.argv[2]).resolve())
print(open(Path(sys.argv[2])/'validation.json').read())
