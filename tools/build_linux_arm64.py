#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build reviewed dependencies and the native Linux ARM64 host from local sources."""
import argparse,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--jobs',type=int,default=4)
parser.add_argument('--skip-dependencies',action='store_true')
args=parser.parse_args()
if not args.skip_dependencies:
    subprocess.run([sys.executable,str(ROOT/'tools/dependencies/build_linux_arm64.py'),'--jobs',str(args.jobs)],cwd=ROOT,check=True)
subprocess.run([sys.executable,str(ROOT/'native/tools/build_cycles_linux_arm64.py'),'--jobs',str(args.jobs)],cwd=ROOT,check=True)
