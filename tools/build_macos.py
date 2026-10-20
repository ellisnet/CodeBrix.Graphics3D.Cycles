#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build reviewed dependencies, the osx-arm64 Metal host and the oracle runner."""
import argparse,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--jobs',type=int,default=8)
parser.add_argument('--skip-dependencies',action='store_true',help='Reuse only a complete matching dependency prefix')
args=parser.parse_args()
if not args.skip_dependencies:
    subprocess.run([sys.executable,str(ROOT/'tools/dependencies/build_macos.py'),'--jobs',str(args.jobs)],cwd=ROOT,check=True)
subprocess.run([sys.executable,str(ROOT/'native/tools/build_cycles_macos.py'),'--jobs',str(args.jobs)],cwd=ROOT,check=True)
host=ROOT/'native/work/macos/osx-arm64/host/build/lib/libcodebrix_cycles_host.dylib'
oracle=ROOT/'native/work/macos/osx-arm64/oracle-build'
subprocess.run(['cmake','-S',str(ROOT/'native/oracle'),'-B',str(oracle),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_OSX_ARCHITECTURES=arm64',
                '-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0','-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local',
                '-DCODEBRIX_HOST_LIBRARY='+str(host)],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
subprocess.run(['cmake','--build',str(oracle),'--parallel',str(args.jobs)],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
print('osx-arm64: dependencies, Metal host and oracle runner built',flush=True)
