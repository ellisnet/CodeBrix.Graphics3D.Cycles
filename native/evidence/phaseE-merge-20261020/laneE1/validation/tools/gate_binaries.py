#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase C: run named gate binaries directly (pattern of B3's extra_gates.py and
B2's Linux runs). Each binary's own assertions decide pass/fail; per-gate wall
budget is enforced by termination (failure, partial output kept)."""
import argparse, json, subprocess, time, hashlib
from pathlib import Path
ap = argparse.ArgumentParser(); ap.add_argument('--bin', type=Path, required=True); ap.add_argument('--output', type=Path, required=True)
ap.add_argument('--gate', action='append', required=True, help='name=binary[:arg,...]'); ap.add_argument('--budget', type=float, default=900)
a = ap.parse_args(); a.output.mkdir(parents=True, exist_ok=False)
status = {}
for spec in a.gate:
    name, rest = spec.split('=', 1); binary, _, extra = rest.partition(':'); args = [x for x in extra.split(',') if x]
    folder = a.output/name; folder.mkdir()
    exe = a.bin/('codebrix_cycles_'+binary+'_gate')
    command = [str(exe), str(folder/'run'), *args]
    t = time.time(); timed_out = False
    with (folder/'stdout.txt').open('w') as out, (folder/'stderr.txt').open('w') as err:
        try: rc = subprocess.run(command, stdout=out, stderr=err, timeout=a.budget).returncode
        except subprocess.TimeoutExpired: rc = None; timed_out = True
    row = {'command': command, 'binarySha256': hashlib.sha256(exe.read_bytes()).hexdigest(), 'returncode': rc,
           'timedOut': timed_out, 'budgetSeconds': a.budget, 'seconds': round(time.time()-t, 1)}
    try: row['passed'] = bool(json.loads((folder/'stdout.txt').read_text()).get('passed')) and rc == 0
    except Exception: row['passed'] = False
    status[name] = row
    (a.output/'status.json').write_text(json.dumps(status, indent=1)+'\n')
    print(name, rc, row['passed'], row['seconds'], flush=True)
