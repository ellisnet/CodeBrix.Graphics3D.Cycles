#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Compile/run the independent atomic contract on Linux and API-33 Android ABIs."""
import argparse
import concurrent.futures
import hashlib
import json
import shlex
import subprocess
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'native/work/license-clean/atomics'
SDK=Path.home()/'Android/Sdk'
NDK=SDK/'ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin'
DEVICES=[('linux-x64',None),('android-arm64','98271FFBA0048D'),
         ('android-arm64','R52W207XQ1V'),('android-x64','192.168.86.225:5555')]


def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def save(path,obj):path.write_text(json.dumps(obj,indent=2)+'\n')


def build(rid):
    folder=OUT/rid;folder.mkdir(parents=True,exist_ok=True)
    compiler={'linux-x64':'clang++','android-arm64':str(NDK/'aarch64-linux-android33-clang++'),
              'android-x64':str(NDK/'x86_64-linux-android33-clang++')}[rid]
    command=[compiler,'-std=c++20','-O2','-Wall','-Wextra','-Werror','-pthread',
             '-I',str(ROOT/'third_party/cycles/src'),str(Path(__file__).parent/'tests/atomic_contract.cpp'),
             '-o',str(folder/'atomic_contract')]
    if rid.startswith('android'):command+=['-static-libstdc++']
    subprocess.run(command,check=True)
    save(folder/'build.json',{'command':command,'minimumAndroidApi':33 if rid.startswith('android') else None,
        'driverSha256':sha(folder/'atomic_contract'),'headerSha256':sha(ROOT/'third_party/cycles/src/util/atomic_ops_codebrix.h'),
        'testSha256':sha(Path(__file__).parent/'tests/atomic_contract.cpp')})


def run(rid,serial):
    binary=OUT/rid/'atomic_contract';folder=OUT/(serial.replace(':','_') if serial else 'linux-result')
    folder.mkdir(parents=True,exist_ok=True)
    if serial:
        adb=[str(SDK/'platform-tools/adb'),'-s',serial]
        def shell(args):return subprocess.check_output(adb+['shell',shlex.join(args)],text=True,timeout=60).strip()
        properties={p:shell(['getprop','ro.'+p]) for p in ['product.cpu.abi','build.version.sdk','product.model']}
        assert int(properties['build.version.sdk']) >= 33
        assert properties['product.cpu.abi']=={'android-arm64':'arm64-v8a','android-x64':'x86_64'}[rid]
        remote='/data/local/tmp/codebrix-cycles-atomic-contract'
        subprocess.run(adb+['push',str(binary),remote],check=True,capture_output=True)
        shell(['chmod','700',remote])
        assert shell(['sha256sum',remote]).split()[0]==sha(binary)
        command=adb+['shell',remote]
    else:
        properties={};command=[str(binary)]
    result=subprocess.run(command,capture_output=True,text=True,timeout=60)
    (folder/'stdout.txt').write_text(result.stdout);(folder/'stderr.txt').write_text(result.stderr)
    if result.returncode:raise RuntimeError(result.stderr)
    report=json.loads(result.stdout);assert report['passed'] and report['updatesPerOperation']==200000
    save(folder/'validation.json',{'passed':True,'rid':rid,'serial':serial,'device':properties,
        'results':report,'driverSha256':sha(binary)})
    print(rid,serial or 'Linux','passed',flush=True)


if __name__ == '__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--android',action='store_true');args=parser.parse_args()
    devices=DEVICES if args.android else DEVICES[:1]
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        for result in [pool.submit(build,rid) for rid in sorted({r for r,_ in devices})]:result.result()
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for result in [pool.submit(run,*d) for d in devices]:result.result()
