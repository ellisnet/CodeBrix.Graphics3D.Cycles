#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Replay captured native buffers on shipped runtimes; no Android Cycles host claim."""
import argparse
import json
import math
import shlex
import shutil
import struct
import subprocess
from pathlib import Path
from build import ROOT, REPO, WORK, PACKAGE, NDK, audit
from device_gate import sha, save

ADB = Path.home()/'Android/Sdk/platform-tools/adb'
CAPTURE = WORK/'shader-data/linux-x64'
BASE = WORK/'shader-data/replay'
REMOTE = '/data/local/tmp/codebrix-cycles-shader-data'

def configure(capture, base, remote):
    """Set one capture for this process before starting independent device jobs."""
    global CAPTURE, BASE, REMOTE
    CAPTURE, BASE, REMOTE = Path(capture), Path(base), remote

def build(rid):
    out = BASE/rid
    out.mkdir(parents=True, exist_ok=True)
    save(out/'webgpu-compatibility.json', audit(allow_rust_documentation_change=True))
    compiler = {'linux-x64':'clang++', 'android-arm64':str(NDK/'aarch64-linux-android33-clang++'),
                'android-x64':str(NDK/'x86_64-linux-android33-clang++')}[rid]
    command = [compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(REPO/'third_party/deps/tbb/include'),
               '-I', str(ROOT/'src'), '-I', str(ROOT/'include'), '-I', str(PACKAGE/'include'),
               str(ROOT/'tests/arena_replay.cpp'), str(ROOT/'src/webgpu_device.cpp'),
               '-L', str(PACKAGE/'runtimes'/rid/'native'), '-lcodebrix_wgpu', '-ldl', '-pthread',
               '-Wl,-rpath,$ORIGIN', '-o', str(out/'arena_replay')]
    if rid.startswith('android'):
        command += ['-static-libstdc++']
    subprocess.run(command, check=True)
    library = PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so'
    shutil.copy2(library, out/library.name)
    save(out/'build.json', {'command':command, 'minimumAndroidApi':33 if rid.startswith('android') else None,
                          'driverSha256':sha(out/'arena_replay'), 'webGpuSha256':sha(library),
                          'sourceSha256':{str(p.relative_to(REPO)):sha(p) for p in
                              [ROOT/'tests/arena_replay.cpp', ROOT/'src/webgpu_device.cpp', ROOT/'src/webgpu_device.h', ROOT/'include/cycles_backend.h']}})
    print(out)

def validate(out):
    meta = json.loads((CAPTURE/'run.capture.json').read_text())
    before = (CAPTURE/'run.arena.bin').read_bytes()
    actual = (out/'arena.result.bin').read_bytes()
    expected = (CAPTURE/'run.reference.bin').read_bytes()
    offset, size, count = meta['outputOffset'], meta['recordSize'], meta['count']
    assert len(actual) == len(before) and len(expected) == size*count
    assert struct.unpack_from('<I', actual, meta['errorOffset'])[0] == 0, 'GPU error word set'
    spans = [(offset, offset+size*count)]
    scratch = meta.get('closureScratch')
    scratch_bytes = 0
    if scratch:
        scratch_bytes = scratch['stride']*count
        spans.append((scratch['offset'], scratch['offset']+scratch_bytes))
        assert scratch['recordOffset']+scratch['stride'] == size
        for n in range(count):
            begin = scratch['offset']+n*scratch['stride']
            result_begin = offset+n*size+scratch['recordOffset']
            assert actual[begin:begin+scratch['stride']] == actual[result_begin:result_begin+scratch['stride']], 'Scratch differs from evaluated closure records'
    last = 0
    for begin, end in sorted(spans):
        assert last <= begin <= end <= len(before), 'Invalid/overlapping capture output spans'
        assert before[last:begin] == actual[last:begin], 'Replay changed scene/state/guard bytes'
        last = end
    assert before[last:] == actual[last:], 'Replay changed trailing guard bytes'

    fields = [(field, i) for field in meta['fields'] for i in range(0, field['size'], 4)]
    mask = bytearray(size)
    for field, i in fields:
        mask[field['offset']+i:field['offset']+i+4] = b'\1'*4
    floats = integers = strict = 0
    maximum = 0.0
    for n in range(count):
        base = n*size
        for field, i in fields:
            at = base+field['offset']+i
            if field['floating']:
                a = struct.unpack_from('<f', actual, offset+at)[0]
                e = struct.unpack_from('<f', expected, at)[0]
                error = abs(a-e)
                assert math.isfinite(a) and math.isfinite(e) and error <= 5e-5+2e-4*abs(e), f"{field['name']} path {n}: GPU {a}, CPU {e}"
                maximum = max(maximum, error)
                strict += error > 1e-7+1e-7*abs(e)
                floats += 1
            else:
                assert actual[offset+at:offset+at+4] == expected[at:at+4], f"Integer {field['name']} differs"
                integers += 1
        for i in range(size):
            if not mask[i]:
                assert actual[offset+base+i] == before[offset+base+i], 'Record padding changed'
    return {'passed':True, 'gpuKernelReplayOnly':True, 'paths':count, 'floatChecks':floats,
            'integerChecks':integers, 'strictFloatFailures':strict, 'maximumAbsoluteError':maximum,
            ('unchangedBytesOutsideOutputAndScratch' if scratch else 'unchangedBytesOutsideOutput'):len(before)-size*count-scratch_bytes,
            'closureScratchBytesVerified':scratch_bytes,
            'absoluteTolerance':5e-5, 'relativeTolerance':2e-4,
            'inputSha256':{p.name:sha(p) for p in [CAPTURE/('run.'+name) for name in
                ['wgsl','arena.bin','params.bin','reference.bin','capture.json']]},
            'outputSha256':sha(out/'arena.result.bin')}

def run(rid, serial=None):
    binary = BASE/rid
    if rid.startswith('android') != bool(serial):
        raise ValueError('Android replay needs a device serial; Linux does not')
    if sha(binary/'libcodebrix_wgpu.so') != sha(PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so'):
        raise RuntimeError('Staged WebGPU runtime differs from package')
    out = BASE/(('android-'+serial.replace(':','_')) if serial else 'linux-result')
    out.mkdir(parents=True, exist_ok=True)
    inputs = [CAPTURE/('run.'+name) for name in ['wgsl','arena.bin','params.bin']]
    entry = json.loads((CAPTURE/'run.capture.json').read_text()).get('entry', 'shader_data_probe')
    if serial:
        adb = [str(ADB), '-s', serial]
        def shell(args):
            return subprocess.run(adb+['shell', shlex.join(args)], check=True, capture_output=True, text=True, timeout=180).stdout.strip()
        properties = {key:shell(['getprop', 'ro.'+key]) for key in ['build.version.sdk','product.cpu.abi','product.model','build.fingerprint']}
        assert int(properties['build.version.sdk']) >= 33
        assert properties['product.cpu.abi'] == {'android-arm64':'arm64-v8a','android-x64':'x86_64'}[rid]
        save(out/'device.json', {'serial':serial, **properties})
        shell(['mkdir', '-p', REMOTE])
        for path in [binary/'arena_replay', binary/'libcodebrix_wgpu.so', *inputs]:
            subprocess.run(adb+['push', str(path), REMOTE+'/'+path.name], check=True, capture_output=True)
        shell(['chmod','700',REMOTE+'/arena_replay'])
        deployed = shell(['sha256sum', REMOTE+'/libcodebrix_wgpu.so']).split()[0]
        assert deployed == sha(binary/'libcodebrix_wgpu.so')
        command = adb+['shell', shlex.join(['env', 'LD_LIBRARY_PATH='+REMOTE, REMOTE+'/arena_replay',
                    *[REMOTE+'/'+p.name for p in inputs], REMOTE+'/arena.result.bin', entry, REMOTE+'/run'])]
    else:
        deployed = sha(binary/'libcodebrix_wgpu.so')
        command = [str(binary/'arena_replay'), *map(str, inputs), str(out/'arena.result.bin'), entry, str(out/'run')]
    result = subprocess.run(command, capture_output=True, text=True, timeout=180)
    (out/'run.stdout.txt').write_text(result.stdout)
    (out/'run.stderr.txt').write_text(result.stderr)
    if result.returncode:
        raise RuntimeError(result.stderr or result.stdout)
    if serial:
        for name in ['arena.result.bin','run.adapter.txt','run.library.txt']:
            subprocess.run(adb+['pull',REMOTE+'/'+name,str(out/name)], check=True, capture_output=True)
    loaded = (out/'run.library.txt').read_text().strip()
    expected_library = REMOTE+'/libcodebrix_wgpu.so' if serial else str((binary/'libcodebrix_wgpu.so').resolve())
    assert loaded == expected_library, 'Replay loaded an unexpected library path: '+loaded
    report = validate(out)
    report.update({'rid':rid, 'serial':serial, 'matchesPinnedPackage':True, 'webGpuSha256':deployed,
                   'loadedWebGpuPath':loaded, 'driverSha256':sha(binary/'arena_replay')})
    save(out/'validation.json', report)
    print(json.dumps(report, indent=2))

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('action', choices=['build','run'])
    p.add_argument('--rid', choices=['linux-x64','android-arm64','android-x64'], required=True)
    p.add_argument('--serial')
    p.add_argument('--capture', type=Path, default=CAPTURE)
    p.add_argument('--output', type=Path, default=BASE)
    p.add_argument('--remote', default=REMOTE)
    args = p.parse_args()
    configure(args.capture, args.output, args.remote)
    if args.action == 'build':
        build(args.rid)
    else:
        run(args.rid, args.serial)
