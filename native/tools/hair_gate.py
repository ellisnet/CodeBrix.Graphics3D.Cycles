#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""The hair closures on the WebGPU device against the unmodified CPU kernels (Linux): the evaluate, sample and pdf state
gate per fixture graph and geometry (hair_state), the closure records (hair_svm), the surface kernel's path state, film
and queue counters (hair_bounce), the stage-locked image (hair_render), and the curve image gate with the same
materials (hair_curve_render) with its command replays.
Usage: python3 native/tools/hair_gate.py --output DIR [--skip-replay] [--kinds a,b]."""
import argparse
import contextlib
import json
import subprocess
from pathlib import Path
from build import WORK
import render_replay

KINDS = ['hair_state', 'hair_svm', 'hair_bounce', 'hair_render', 'hair_curve_render']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--skip-replay', action='store_true')
    parser.add_argument('--kinds', default=','.join(KINDS))
    args = parser.parse_args()
    out = args.output
    out.mkdir(parents=True, exist_ok=True)
    report = {}
    failed = []
    for kind in args.kinds.split(','):
        folder = out/kind
        folder.mkdir(exist_ok=True)
        binary = WORK/'cycles-build/bin'/('codebrix_cycles_'+kind+'_gate')
        prefix = folder/'run'
        result = subprocess.run([str(binary), str(prefix)], capture_output=True, text=True, timeout=1500)
        (folder/'stderr.txt').write_text(result.stderr)
        (folder/'stdout.json').write_text(result.stdout)
        if result.returncode != 0:
            lines = result.stderr.strip().splitlines()
            failed.append({'gate': kind, 'exit': result.returncode, 'reason': lines[-1] if lines else ''})
            continue
        report[kind] = json.loads(result.stdout)
    if not args.skip_replay and 'hair_curve_render' in report:
        render_replay.build('linux-x64')
        for case in range(report['hair_curve_render']['cases']):
            destination = out/f'replay-{case}'
            with (out/f'replay-{case}.log').open('w') as log, contextlib.redirect_stdout(log):
                render_replay.run('linux-x64', out/'hair_curve_render'/'run'/f'case-{case}', destination)
            report[f'replay-{case}'] = json.loads((destination/'validation.json').read_text())
    report['failed'] = failed
    (out/'results.json').write_text(json.dumps(report, indent=1)+'\n')
    print(json.dumps({'passed': not failed, 'failed': failed, 'gates': [k for k in report if not k.startswith('replay-') and k != 'failed'], 'replays': sum(k.startswith('replay-') for k in report)}))
    raise SystemExit(1 if failed else 0)


if __name__ == '__main__':
    main()
