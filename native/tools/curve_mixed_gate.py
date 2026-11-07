#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Curves in other features' state gates (Linux): each gate runs with CODEBRIX_CYCLES_GATE_CURVES=1, which adds tufts of every
curve shape (baked and instanced, BVH2 unaligned nodes on) to its own fixture (native/cycles/curve_gate_tuft.h), so the
feature's traversals and hit paths meet curves against the unmodified CPU kernels: ambient occlusion, the raytrace nodes,
subsurface scattering, closed volume boundaries (the volume stack), MNEE and shadow linking.
Usage: python3 native/tools/curve_mixed_gate.py --output DIR [GATE ...]."""
import argparse
import json
import os
import subprocess
import time
from pathlib import Path
from build import WORK

GATES = {'ao_state': [], 'raytrace_nodes_state': [], 'subsurface': [], 'volume_boundary': [], 'mnee': [], 'shadow_linking': ['0', '1', '4']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('gates', nargs='*')
    args = parser.parse_args()
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    report = {}
    for name in args.gates or GATES:
        folder = out/name
        folder.mkdir(exist_ok=True)
        env = dict(os.environ, CODEBRIX_CYCLES_GATE_CURVES='1', MESA_SHADER_CACHE_DIR=str(out/'shader-cache'))
        start = time.time()
        run = subprocess.run([str(WORK/'cycles-build/bin'/('codebrix_cycles_'+name+'_gate')), str(folder/'run')]+GATES[name], capture_output=True, text=True, env=env, timeout=1100)
        (folder/'stdout.txt').write_text(run.stdout)
        (folder/'stderr.txt').write_text(run.stderr)
        lines = [line for line in run.stdout.splitlines() if line.strip().startswith('{')]
        result = json.loads(lines[-1]) if lines else {}
        report[name] = {'returncode': run.returncode, 'passed': run.returncode == 0 and result.get('passed', False), 'seconds': round(time.time()-start), 'result': result}
        print(name, report[name]['passed'], report[name]['seconds'], 's', run.stderr.strip().splitlines()[-1][:300] if run.returncode else '', flush=True)
    (out/'results.json').write_text(json.dumps(report, indent=1)+'\n')
    print(json.dumps({'passed': all(r['passed'] for r in report.values()), 'gates': len(report)}))


if __name__ == '__main__':
    main()
