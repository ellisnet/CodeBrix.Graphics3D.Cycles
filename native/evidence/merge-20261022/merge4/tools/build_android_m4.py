#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 4 (from Phase X1): build Android gates from this worktree's sources into
native/work/x1/build-<rid> only. The maintained build_cycles_android.py is used
unchanged; only its WORK root moves (pattern of the Phase C2 wrappers)."""
import sys, json, hashlib
from pathlib import Path
here = Path(__file__).resolve().parent
m4 = here.parent
repo = here.parents[3]
sys.path.insert(0, str(repo/'native/tools'))
import build_cycles_android as build
rid = sys.argv[1]; kinds = sys.argv[2:]
build.WORK = m4/('build-'+rid)
build.WORK.mkdir(parents=True, exist_ok=True)
(build.WORK/'wrapper.json').write_text(json.dumps({'wrapperSha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    'kinds': kinds, 'scope': 'Merge wave 4 validation build; reviewed Android toolchain/dependencies/runtime unchanged.'}, indent=2)+'\n')
sys.argv = [sys.argv[0], '--rid', rid, '--jobs', '6']
for kind in kinds:
    sys.argv += ['--target', 'codebrix_cycles_'+kind+'_gate']
build.main()
