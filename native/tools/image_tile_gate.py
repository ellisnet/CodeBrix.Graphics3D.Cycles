#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Tiled images (Cycles' texture cache) on the WGSL device against the pinned CPU kernel, on this machine.

Builds and runs codebrix_cycles_image_tile_gate (native/cycles/image_tile_gate.cpp): tile and mip level selection, the
dual-number sphere, tube and environment projections, the lcg states of volume evaluations, the tiles requested and used, the restart after a request, and the
values of a tile that failed to load or is requested.
Usage: python3 native/tools/image_tile_gate.py [--skip-build]. Output under native/work/image-tiles/."""
import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT/'work'
OUT = WORK/'image-tiles'
PROGRAM = WORK/'cycles-build/bin/codebrix_cycles_image_tile_gate'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--skip-build', action='store_true', help='Run the program as built')
    args = parser.parse_args()
    if not args.skip_build:
        subprocess.run([sys.executable, str(ROOT/'tools/build_cycles.py'), '--target', 'codebrix_cycles_image_tile_gate'], check=True)
    if not PROGRAM.is_file():
        raise SystemExit('Missing program: '+str(PROGRAM)+' (run without --skip-build)')
    OUT.mkdir(parents=True, exist_ok=True)
    with (OUT/'run.stdout.txt').open('w') as stdout, (OUT/'run.stderr.txt').open('w') as stderr:
        result = subprocess.run([str(PROGRAM), str(OUT/'run')], stdout=stdout, stderr=stderr, timeout=1800)
    text = (OUT/'run.stdout.txt').read_text().strip()
    report = json.loads(text) if result.returncode == 0 and text else None
    passed = bool(report and report.get('passed'))
    status = dict(passed=passed, exit=result.returncode, report=report, error=None if passed else (OUT/'run.stderr.txt').read_text()[-2000:])
    (OUT/'status.json').write_text(json.dumps(status, indent=1)+'\n')
    print(json.dumps(status))
    raise SystemExit(0 if passed else 1)


if __name__ == '__main__':
    main()
