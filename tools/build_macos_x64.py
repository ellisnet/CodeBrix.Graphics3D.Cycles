#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build reviewed dependencies, the osx-x64 WGSL/WebGPU host and the oracle runner.

    python3 tools/build_macos_x64.py --jobs 8 [--skip-dependencies] [--reference-overlay linux-overlay.json]

Runs on an Intel Mac only. The osx-arm64 product (Cycles' Metal device) is built
by tools/build_macos.py.
"""
import argparse,platform,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BASE=ROOT/'native/work/macos/osx-x64'
parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('--jobs',type=int,default=8)
parser.add_argument('--skip-dependencies',action='store_true',help='Reuse only a complete matching dependency prefix')
parser.add_argument('--reference-overlay',type=Path,help='Overlay hashes written on Linux (see native/host/MACOS_X64.md)')
args=parser.parse_args()
if platform.system()!='Darwin' or platform.machine()!='x86_64':
    raise SystemExit(f'osx-x64: this coordinator runs only on an Intel Mac; this host is {platform.system()} {platform.machine()}')
if not args.skip_dependencies:
    subprocess.run([sys.executable,str(ROOT/'tools/dependencies/build_macos_x64.py'),'--jobs',str(args.jobs)],cwd=ROOT,check=True)
subprocess.run([sys.executable,str(ROOT/'native/tools/build_cycles_macos_x64.py'),'--jobs',str(args.jobs),
                *(['--reference-overlay',str(args.reference_overlay.resolve())] if args.reference_overlay else [])],cwd=ROOT,check=True)
host=BASE/'host/build/lib/libcodebrix_cycles_host.dylib'
oracle=BASE/'oracle-build'
subprocess.run(['cmake','-S',str(ROOT/'native/oracle'),'-B',str(oracle),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_OSX_ARCHITECTURES=x86_64',
                '-DCMAKE_OSX_DEPLOYMENT_TARGET=15.0','-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew;/usr/local',
                '-DCODEBRIX_HOST_LIBRARY='+str(host)],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
subprocess.run(['cmake','--build',str(oracle),'--parallel',str(args.jobs)],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
print('osx-x64: dependencies, WGSL/WebGPU host and oracle runner built',flush=True)
