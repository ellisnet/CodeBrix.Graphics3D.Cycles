#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build the current Linux native Cycles/WGSL implementation from this repo."""
import argparse
import platform
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--jobs',type=int,default=6)
    parser.add_argument('--skip-dependencies',action='store_true',help='Reuse the verified local dependency prefix')
    parser.add_argument('--rebase-build-tree',action='store_true',help='Re-point build trees copied from another checkout (lane seeding) at this checkout first; implies --skip-dependencies (tools/rebase_build_tree.py)')
    args=parser.parse_args()
    if sys.platform!='linux' or platform.machine()!='x86_64':raise RuntimeError('This recipe currently proves linux-x64; other native RID recipes remain implementation work')
    commands=[]
    rebase=['--rebase-build-tree'] if args.rebase_build_tree else []
    if not args.skip_dependencies and not args.rebase_build_tree:
        commands.append([sys.executable,'tools/dependencies/build.py','--jobs',str(args.jobs)])
    commands += [[sys.executable,'prototype/tools/build_host.py','--jobs',str(args.jobs),*rebase],
                 ['cmake','--build','prototype/work/cycles-build','--target',
                  'codebrix_cycles_material_host','codebrix_cycles_path_host','--parallel',str(args.jobs)],
                 [sys.executable,'native/tools/build_cycles.py','--jobs',str(args.jobs),*rebase],
                 [sys.executable,'native/tools/build.py','--rid','linux-x64']]
    for command in commands:subprocess.run(command,cwd=ROOT,check=True)


if __name__=='__main__':main()
