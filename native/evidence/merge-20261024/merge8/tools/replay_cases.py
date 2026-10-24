#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 8 part 2: run the Linux command replays of chosen cases of a gate's captures INDIVIDUALLY (light_tree_gate.py's driver stops at the known
case-2 replay, DEVICE.md item 24). usage: replay_cases.py RUN_DIR OUT_DIR PREFIX CASE..."""
import contextlib, json, sys, time
from pathlib import Path
ROOT = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles'); sys.path.insert(0, str(ROOT/'native/tools'))
import render_replay
run, out, prefix, cases = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3], [int(c) for c in sys.argv[4:]]
out.mkdir(parents=True, exist_ok=True); results = {}
for c in cases:
    name = '%s-replay-%d' % (prefix, c); t = time.time()
    try:
        with (out/(name+'.log')).open('w') as log, contextlib.redirect_stdout(log):
            render_replay.run('linux-x64', run/('case-%d' % c), out/name)
        rep = json.loads((out/name/'validation.json').read_text())
        results[name] = dict(passed=True, seconds=round(time.time()-t, 1), readbacks=rep['results']['exactSchedulingReadbacks'], max=rep['maximumAbsoluteImageError'], strict=rep['strictImageFailures'], fmt=rep.get('captureFormat'))
    except BaseException as e:
        results[name] = dict(passed=False, seconds=round(time.time()-t, 1), error=repr(e)[-400:])
    print(name, json.dumps(results[name]), flush=True)
(out/'results.json').write_text(json.dumps(results, indent=1))
sys.exit(0 if all(r['passed'] for r in results.values()) else 1)
