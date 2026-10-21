#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build and run the libm deviation probe (libm_probe.c) on Linux or an attached Android device.

Writes OUTPUT (JSON): the probe's own record plus the build/run provenance (compiler,
binary SHA-256, device properties). Diagnostic only; it never decides a gate.

  libm_probe.py linux OUTPUT.json
  libm_probe.py android-arm64|android-x64 OUTPUT.json --serial SERIAL [--adb PATH]

On a device the probe runs from /data/local/tmp/codebrix-libm-probe-<pid>, which is
removed afterwards (also on failure). Android binaries use the reviewed NDK at API 33.
"""
import argparse
import hashlib
import json
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
SOURCE = HERE/'libm_probe.c'
FLAGS = ['-O2', '-std=c11', '-ffp-contract=off', '-fno-fast-math']


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def build(rid, folder):
    binary = Path(folder)/('libm_probe-'+rid)
    if rid == 'linux':
        compiler = ['gcc']
        command = [*compiler, *FLAGS, '-o', str(binary), str(SOURCE), '-lm', '-lpthread']
        version = subprocess.check_output(['gcc', '--version'], text=True).splitlines()[0]
    else:
        sys.path.insert(0, str(REPO/'tools'))
        from android_toolchain import AndroidToolchain
        toolchain = AndroidToolchain(rid)
        clang = toolchain.bin/'clang'
        command = [str(clang), '--target='+toolchain.triple+'33', *FLAGS, '-o', str(binary), str(SOURCE), '-lm']
        version = subprocess.check_output([str(clang), '--version'], text=True).splitlines()[0]
    subprocess.run(command, check=True)
    return binary, dict(command=command, compiler=version, binarySha256=sha(binary), sourceSha256=sha(SOURCE))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('rid', choices=['linux', 'android-arm64', 'android-x64'])
    parser.add_argument('output', type=Path)
    parser.add_argument('--serial')
    parser.add_argument('--adb', default=os.path.expanduser('~/Android/Sdk/platform-tools/adb'))
    parser.add_argument('--threads', type=int, default=4)
    parser.add_argument('--timeout', type=int, default=900)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as folder:
        binary, provenance = build(args.rid, folder)
        if args.rid == 'linux':
            result = subprocess.run([str(binary), str(args.threads)], capture_output=True, text=True,
                                    timeout=args.timeout, check=True)
            host = dict(uname=os.uname().release, machine=os.uname().machine)
        else:
            if not args.serial:
                parser.error('--serial is required for a device')
            adb = [args.adb, '-s', args.serial]
            remote = '/data/local/tmp/codebrix-libm-probe-'+str(os.getpid())
            props = {k: subprocess.check_output([*adb, 'shell', 'getprop', v], text=True).strip() for k, v in
                     dict(model='ro.product.model', abi='ro.product.cpu.abi', api='ro.build.version.sdk',
                          fingerprint='ro.build.fingerprint').items()}
            expected = {'android-arm64': 'arm64-v8a', 'android-x64': 'x86_64'}[args.rid]
            if props['abi'] != expected:
                raise RuntimeError('Device ABI '+props['abi']+' does not match '+args.rid)
            try:
                subprocess.run([*adb, 'shell', 'mkdir -p '+remote], check=True)
                subprocess.run([*adb, 'push', str(binary), remote+'/libm_probe'], check=True, capture_output=True)
                pushed = subprocess.check_output([*adb, 'shell', 'sha256sum '+remote+'/libm_probe'], text=True).split()[0]
                if pushed != provenance['binarySha256']:
                    raise RuntimeError('Pushed probe hash differs')
                result = subprocess.run([*adb, 'shell', shlex.join(['chmod', '755', remote+'/libm_probe'])+' && '+
                                         shlex.join([remote+'/libm_probe', str(args.threads)])],
                                        capture_output=True, text=True, timeout=args.timeout, check=True)
            finally:
                subprocess.run([*adb, 'shell', 'rm -rf '+remote], check=False)
                left = subprocess.run([*adb, 'shell', 'ls -d '+remote], capture_output=True, text=True)
                provenance['deviceCleaned'] = left.returncode != 0
            host = dict(serial=args.serial, **props)
    record = json.loads(result.stdout)
    record.update(rid=args.rid, host=host, build=provenance)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(record, indent=2)+'\n')
    for row in record['functions']:
        print('%-7s inputs=%d differ=%d ambiguous=%d maxUlps=%d digest=%s' % (
            row['function'], row['inputs'], row['differFromCorrectlyRounded'], row['ambiguous'],
            row['maximumUlps'], row['resultDigest']))


if __name__ == '__main__':
    main()
