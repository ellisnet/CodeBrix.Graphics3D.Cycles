#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Replay native Cycles scheduler commands and compare the independent CPU film.

Scheduling readbacks are assertions, never replacements for device output.
Android replay validates GPU portability; it is not an Android Cycles host build.
"""
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
BASE = WORK/'render/replay-build'
INPUTS = ['run.arena.bin', 'run.commands.bin', 'controls.wgsl', 'camera.wgsl',
          'closest.wgsl', 'background.wgsl', 'surface.wgsl']


def build(rid):
    out = BASE/rid
    out.mkdir(parents=True, exist_ok=True)
    save(out/'webgpu-compatibility.json', audit(allow_rust_documentation_change=True))
    compiler = {'linux-x64':'clang++', 'android-arm64':str(NDK/'aarch64-linux-android33-clang++'),
                'android-x64':str(NDK/'x86_64-linux-android33-clang++')}[rid]
    command = [compiler, '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror',
               '-I', str(ROOT/'src'), '-I', str(ROOT/'include'), '-I', str(PACKAGE/'include'),
               '-I', str(REPO/'third_party/deps/tbb/include'),
               str(ROOT/'tests/render_replay.cpp'), str(ROOT/'src/webgpu_device.cpp'),
               '-L', str(PACKAGE/'runtimes'/rid/'native'), '-lcodebrix_wgpu', '-ldl', '-pthread',
               '-Wl,-rpath,$ORIGIN', '-o', str(out/'render_replay')]
    if rid.startswith('android'):
        command += ['-static-libstdc++']
    subprocess.run(command, check=True)
    library = PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so'
    shutil.copy2(library, out/library.name)
    save(out/'build.json', {'command':command, 'minimumAndroidApi':33 if rid.startswith('android') else None,
                          'driverSha256':sha(out/'render_replay'), 'webGpuSha256':sha(library),
                          'sourceSha256':{str(p.relative_to(REPO)):sha(p) for p in
                              [ROOT/'tests/render_replay.cpp', ROOT/'src/webgpu_device.cpp',
                               ROOT/'src/webgpu_device.h', ROOT/'include/cycles_backend.h', Path(__file__)]}})
    print(out, flush=True)


def validate(capture, out):
    meta = json.loads((capture/'run.capture.json').read_text())
    observed = json.loads((out/'run.stdout.txt').read_text())
    assert observed['passed'] and observed['nativeCommandReplay']
    assert observed['commands'] == meta['commands'] and observed['exactSchedulingReadbacks'] > 0
    before = (capture/'run.arena.bin').read_bytes()
    actual = (out/'run.arena.bin').read_bytes()
    assert len(actual) == len(before) == meta['arenaBytes']
    assert struct.unpack_from('<I', actual, meta['errorOffset'])[0] == 0, 'GPU error word set'
    queued = meta['queuedKernelOffset']
    count = meta['pathCapacity']
    assert meta['queuedKernelElementBytes'] == 2
    assert actual[queued:queued+count*2] == bytes(count*2), 'Unfinished GPU paths'
    immutable = 0
    for span in meta['immutableSceneSpans']:
        begin, length = span['offset'], span['bytes']
        assert 0 <= begin <= begin+length <= len(actual)
        assert actual[begin:begin+length] == before[begin:begin+length], 'Changed scene table '+span['name']
        immutable += length
    reference = (capture/'run.cpu.film.bin').read_bytes()
    offset, length = meta['filmOffset'], meta['filmBytes']
    assert len(reference) == length == meta['width']*meta['height']*16
    assert offset+length <= len(actual)
    strict = 0
    maximum = squared = 0.0
    for i, (a, e) in enumerate(zip(struct.iter_unpack('<f', actual[offset:offset+length]),
                                   struct.iter_unpack('<f', reference), strict=True)):
        a, e = a[0], e[0]
        error = abs(a-e)
        assert math.isfinite(a) and math.isfinite(e) and error <= 5e-5+2e-4*abs(e), f'Film {i}: GPU {a}, CPU {e}'
        strict += error > 1e-7+1e-7*abs(e)
        maximum = max(maximum, error)
        squared += error*error
    return {'passed':True, 'scope':'Full recorded native render loop; every scheduling readback verified; independent original-CPU film',
            'androidNativeHostProven':False, 'results':observed, 'filmFloats':length//4,
            'maximumAbsoluteImageError':maximum, 'rmsImageError':math.sqrt(squared/(length//4)),
            'strictImageFailures':strict, 'strictNumericalParityPassed':strict == 0,
            'absoluteTolerance':5e-5, 'relativeTolerance':2e-4,
            'strictAbsoluteTolerance':1e-7, 'strictRelativeTolerance':1e-7,
            'immutableSceneBytes':immutable, 'terminalPathSlots':count, 'gpuErrorWord':0,
            'inputSha256':{name:sha(capture/name) for name in INPUTS+['run.capture.json','run.cpu.film.bin']},
            'outputSha256':sha(out/'run.arena.bin')}


def run(rid, capture, out, serial=None):
    binary = BASE/rid
    if rid.startswith('android') != bool(serial):
        raise ValueError('Android replay needs a device serial; Linux does not')
    library = PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so'
    assert sha(binary/'libcodebrix_wgpu.so') == sha(library), 'Staged runtime differs from package'
    out.mkdir(parents=True, exist_ok=True)
    remote = '/data/local/tmp/codebrix-cycles-native-render-'+capture.name
    if serial:
        adb = [str(ADB), '-s', serial]
        def shell(args):
            return subprocess.run(adb+['shell', shlex.join(args)], check=True, capture_output=True, text=True, timeout=180).stdout.strip()
        properties = {key:shell(['getprop', 'ro.'+key]) for key in ['build.version.sdk','product.cpu.abi','product.model','build.fingerprint']}
        assert int(properties['build.version.sdk']) >= 33
        assert properties['product.cpu.abi'] == {'android-arm64':'arm64-v8a','android-x64':'x86_64'}[rid]
        save(out/'device.json', {'serial':serial, **properties})
        shell(['mkdir','-p',remote])
        for path in [binary/'render_replay', binary/'libcodebrix_wgpu.so', *[capture/name for name in INPUTS]]:
            subprocess.run(adb+['push',str(path),remote+'/'+path.name], check=True, capture_output=True)
        shell(['chmod','700',remote+'/render_replay'])
        deployed = shell(['sha256sum',remote+'/libcodebrix_wgpu.so']).split()[0]
        assert deployed == sha(library)
        command = adb+['shell', shlex.join(['env','LD_LIBRARY_PATH='+remote,remote+'/render_replay',remote,remote+'/result'])]
    else:
        deployed = sha(library)
        command = [str(binary/'render_replay'),str(capture),str(out/'run')]
    save(out/'invocation.json', {'command':command})
    result = subprocess.run(command, capture_output=True, text=True, timeout=600)
    (out/'run.stdout.txt').write_text(result.stdout)
    (out/'run.stderr.txt').write_text(result.stderr)
    if result.returncode:
        raise RuntimeError(result.stderr or result.stdout)
    if serial:
        for suffix in ['arena.bin','adapter.txt','library.txt']:
            subprocess.run(adb+['pull',remote+'/result.'+suffix,str(out/('run.'+suffix))], check=True, capture_output=True)
    loaded = (out/'run.library.txt').read_text().strip()
    expected = remote+'/libcodebrix_wgpu.so' if serial else str((binary/'libcodebrix_wgpu.so').resolve())
    assert loaded == expected, 'Replay loaded unexpected runtime: '+loaded
    report = validate(capture, out)
    report.update({'rid':rid, 'serial':serial, 'matchesPinnedPackage':True, 'webGpuSha256':deployed,
                   'loadedWebGpuPath':loaded, 'driverSha256':sha(binary/'render_replay')})
    save(out/'validation.json', report)
    print(json.dumps(report, indent=2), flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action',choices=['build','run'])
    parser.add_argument('--rid',choices=['linux-x64','android-arm64','android-x64'],required=True)
    parser.add_argument('--capture',type=Path)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--serial')
    args = parser.parse_args()
    if args.action == 'build':
        build(args.rid)
    else:
        if args.capture is None or args.output is None:
            parser.error('run requires --capture and --output')
        run(args.rid,args.capture.resolve(),args.output.resolve(),args.serial)
