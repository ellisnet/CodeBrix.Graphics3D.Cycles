#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase C close-out: build Android gates from main's merged sources into
native/work/phaseC4/build-<arch> (pattern of the C2 build_arm64.py; the maintained
build_cycles_android.py is used unchanged, only its WORK root moves).
Usage: build_android_c4.py RID OUTDIR JOBS [kind ...] [--host-default]"""
import sys, json, hashlib
from pathlib import Path
here = Path(__file__).resolve().parent
repo = here.parents[3]
sys.path.insert(0, str(repo/'native/tools'))
import build_cycles_android as build
rid, outdir, jobs = sys.argv[1], Path(sys.argv[2]).resolve(), sys.argv[3]
kinds = [k for k in sys.argv[4:] if not k.startswith('--')]
host_default = '--host-default' in sys.argv
build.WORK = outdir; outdir.mkdir(parents=True, exist_ok=True)
targets = (build.HOST_TARGETS + build.GATE_TARGETS if host_default else []) + ['codebrix_cycles_'+k+'_gate' for k in kinds]
targets = list(dict.fromkeys(targets))
(outdir/'wrapper.json').write_text(json.dumps({'wrapperSha256': hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
    'rid': rid, 'targets': targets, 'scope': 'Phase C close-out device confirmation build; toolchain/dependencies/runtime unchanged.'}, indent=2)+'\n')
sys.argv = [sys.argv[0], '--rid', rid, '--jobs', jobs]
for t in targets: sys.argv += ['--target', t]
build.main()
