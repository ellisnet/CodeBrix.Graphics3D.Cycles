#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E5: build Android gates from this worktree's sources into native/work/e5/build-<rid>
(merge wave 1 wrapper pattern: the maintained build_cycles_android.py unchanged, only WORK moves)."""
import sys, json, hashlib
from pathlib import Path
here = Path(__file__).resolve().parent
e5 = here.parent
repo = here.parents[3]
sys.path.insert(0, str(repo/'native/tools'))
import build_cycles_android as build
rid = sys.argv[1]; label = sys.argv[2]; kinds = sys.argv[3:]
build.WORK = e5/('build-'+label+'-'+rid)
build.WORK.mkdir(parents=True, exist_ok=True)
(build.WORK/'wrapper.json').write_text(json.dumps({'wrapperSha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    'kinds': kinds, 'scope': 'Phase E5 validation build; reviewed Android toolchain/dependencies/runtime unchanged.'}, indent=2)+'\n')
sys.argv = [sys.argv[0], '--rid', rid, '--jobs', '6']
for kind in kinds:
    sys.argv += ['--target', 'codebrix_cycles_'+kind+'_gate']
build.main()
