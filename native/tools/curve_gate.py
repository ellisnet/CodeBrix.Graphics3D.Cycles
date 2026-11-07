#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Static curves on the WebGPU device against the unmodified CPU kernels (Linux): the state gate (closest and opaque
shadow traversal, curve shader data, BVH2's unaligned nodes on and off), the image gate and its command replays.
Usage: python3 native/tools/curve_gate.py --output DIR [--skip-replay]."""
import argparse
import contextlib
import json
import subprocess
from pathlib import Path
from build import WORK
import render_replay


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--skip-replay', action='store_true')
    args = parser.parse_args()
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    report = {}
    state = subprocess.run([str(WORK/'cycles-build/bin/codebrix_cycles_curve_gate'), str(out/'state')], capture_output=True, text=True, timeout=1500)
    (out/'state.stderr.txt').write_text(state.stderr)
    (out/'state.json').write_text(state.stdout)
    assert state.returncode == 0, state.stderr[-2000:]
    report['state'] = json.loads(state.stdout)
    assert report['state']['passed'] and report['state']['refused'] == 5
    assert all(r['curveHits'] > 1000 and r['decisionMismatches'] == 0 and r['shadowDecisionMismatches'] == 0 and r['shadowBlocked'] > 500 and r['shadowClear'] > 500 for r in report['state']['rows'])
    render = subprocess.run([str(WORK/'cycles-build/bin/codebrix_cycles_curve_render_gate'), str(out/'render')], capture_output=True, text=True, timeout=1500)
    (out/'render.stderr.txt').write_text(render.stderr)
    (out/'render.json').write_text(render.stdout)
    assert render.returncode == 0, render.stderr[-2000:]
    report['render'] = json.loads(render.stdout)
    assert report['render']['passed'] and report['render']['cyclesAllocationBytesAfterCleanup'] == 0
    if not args.skip_replay:
        render_replay.build('linux-x64')
        for case in range(report['render']['cases']):
            destination = out/f'replay-{case}'
            with (out/f'replay-{case}.log').open('w') as log, contextlib.redirect_stdout(log):
                render_replay.run('linux-x64', out/'render'/f'case-{case}', destination)
            report[f'replay-{case}'] = json.loads((destination/'validation.json').read_text())
    (out/'results.json').write_text(json.dumps(report, indent=1)+'\n')
    print(json.dumps({'passed': True, 'stateRows': len(report['state']['rows']), 'renderCases': report['render']['cases'],
                      'replays': sum(k.startswith('replay-') for k in report)}))


if __name__ == '__main__':
    main()
