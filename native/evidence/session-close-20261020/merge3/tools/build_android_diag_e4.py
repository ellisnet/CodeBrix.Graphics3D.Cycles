#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 3 diagnostic: build Android gates from the sealed E4 worktree's sources (baseline-merge1 + E4, no
M3/M4) into native/work/merge3/diag-e4-build-<rid>. The E4 worktree's build_cycles_android.py is used unchanged;
only its WORK root moves. Nothing in the worktree is written."""
import sys, json, hashlib
from pathlib import Path
here = Path(__file__).resolve().parent
e4 = Path('/home/jeremy/ClaudeHome/cycles-worktrees/e4-mode4-adreno')
sys.path.insert(0, str(e4/'native/tools'))
import build_cycles_android as build
assert Path(build.__file__).resolve().is_relative_to(e4)
rid = sys.argv[1]; kinds = sys.argv[2:]
build.WORK = here.parent/('diag-e4-build-'+rid)
build.WORK.mkdir(parents=True, exist_ok=True)
(build.WORK/'wrapper.json').write_text(json.dumps({'wrapperSha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    'kinds': kinds, 'sourceTree': str(e4), 'scope': 'Merge wave 3 diagnostic build (Pixel object motion SVM error 8 A/B).'}, indent=2)+'\n')
sys.argv = [sys.argv[0], '--rid', rid, '--jobs', '6']
for kind in kinds:
    sys.argv += ['--target', 'codebrix_cycles_'+kind+'_gate']
build.main()
