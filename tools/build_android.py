#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build Android dependencies and the native Cycles host from reviewed sources."""
import argparse
import subprocess
import sys
from pathlib import Path

from android_toolchain import TARGETS

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--rid', required=True, choices=TARGETS)
    parser.add_argument('--jobs', default='4')
    parser.add_argument('--skip-dependencies', action='store_true',
                        help='Reuse only the verified dependency prefix for this target')
    args = parser.parse_args()
    if not args.skip_dependencies:
        subprocess.run([sys.executable, ROOT/'tools/dependencies/build_android.py',
                        '--rid', args.rid, '--jobs', args.jobs], cwd=ROOT, check=True)
    subprocess.run([sys.executable, ROOT/'native/tools/build_cycles_android.py',
                    '--rid', args.rid, '--jobs', args.jobs], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
