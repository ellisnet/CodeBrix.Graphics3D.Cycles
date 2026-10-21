#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E4: judge a probe replay's final arena with the maintained render_replay.validate (copies r.* to run.*)."""
import json, shutil, sys, tempfile
from pathlib import Path
REPO=Path(__file__).resolve().parents[4]; sys.path.insert(0,str(REPO/'native/tools'))
import render_replay
cap=Path(sys.argv[1]); prefix=Path(sys.argv[2])
with tempfile.TemporaryDirectory(dir=prefix.parent) as t:
    t=Path(t)
    shutil.copy2(str(prefix)+'.arena.bin', t/'run.arena.bin'); shutil.copy2(prefix.parent/'stdout.txt', t/'run.stdout.txt')
    rep=render_replay.validate(cap,t)
print(json.dumps({k:rep[k] for k in rep if k in('passed','maximumAbsoluteImageError','strictImageFailures','practicalImageFailures')} | {'readbacks':rep.get('results',{}).get('exactSchedulingReadbacks')}))
