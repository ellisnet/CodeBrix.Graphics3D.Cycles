#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Prepare and validate reproducible hardware GPU feasibility probes."""
import argparse
import array
import ctypes
import hashlib
import json
import math
import random
import struct
import subprocess
from pathlib import Path
from translate import generate, verify_cycles
from cycles_source import SOURCE
import bvh
import native_runner

ROOT = Path(__file__).resolve().parent
WORK = ROOT / 'work'
REVISION = 'a456b761034dda42c32eef9f4aae0fa5a5c9f604'

def floats(path, values):
    data = array.array('f', values)
    path.write_bytes(data.tobytes())
    return data

def shader_case(name, source, values, expected, groups, dispatches=None):
    (WORK / f'{name}.wgsl').write_text(source)
    floats(WORK / f'{name}.input.bin', values)
    floats(WORK / f'{name}.output.bin', [0] * len(expected))
    floats(WORK / f'{name}.expected.bin', expected)
    return {'Name': name, 'Shader': f'{name}.wgsl',
            'Buffers': [f'{name}.input.bin', f'{name}.output.bin'], 'Output': 1,
            'Dispatches': dispatches or [{'Entry': 'main', 'Groups': groups}]}

BINDINGS = '''
@group(0) @binding(0) var<storage, read> inputs: array<vec4<f32>>;
@group(0) @binding(1) var<storage, read_write> outputs: array<vec4<f32>>;
'''

def prepare(upstream):
    WORK.mkdir(exist_ok=True)
    if upstream.resolve() != verify_cycles().resolve():
        raise RuntimeError('CPU references require reviewed filtered Cycles source')
    revision = REVISION
    command = ['clang++', '-std=c++20', '-O2', '-fPIC', '-shared', '-ffp-contract=off',
               '-I', str(ROOT.parent/'third_party/deps/tbb/include'),
               '-I', str(upstream / 'src'), str(ROOT / 'native/reference.cpp'),
               '-o', str(WORK / 'libcycles_reference.so')]
    subprocess.run(command, check=True)
    translated = generate(upstream, WORK, ROOT / 'native/reference.cpp')
    lib = ctypes.CDLL(str(WORK / 'libcycles_reference.so'))
    fp = ctypes.POINTER(ctypes.c_float)
    lib.cycles_triangle.argtypes = [fp, fp]
    lib.cycles_math.argtypes = [ctypes.c_int, ctypes.c_float, ctypes.c_float, ctypes.c_float]
    lib.cycles_math.restype = ctypes.c_float
    rng = random.Random(20261018)
    cases = []
    values, expected = [], []
    for i in range(16384):
        a, b, c = [[rng.uniform(-3, 3) for _ in range(3)] for _ in range(3)]
        origin = [rng.uniform(-5, 5) for _ in range(3)]
        if i % 3 == 0:
            dest = [(a[j] + b[j] + c[j]) / 3 for j in range(3)]
            direction = [dest[j] - origin[j] for j in range(3)]
        else:
            direction = [rng.uniform(-2, 2) for _ in range(3)]
        tmin, tmax = 0.0, 10000.0
        if i < 64:
            a, b, c = [0, 0, 0], [1, 0, 0], [0, 1, 0]
            origin, direction = [0.25, 0.25, 1], [0, 0, -1]
            mode = i % 8
            if mode == 0: direction = [1, 0, 0]  # parallel
            if mode == 1: c = b[:]               # degenerate
            if mode == 2: origin = [0, 0, 1]     # exact vertex
            if mode == 3: origin = [.5, .5, 1]   # exact edge
            if mode == 4: tmin = 1.0             # inclusive near bound
            if mode == 5: tmax = .5              # clipped
            if mode == 6: direction = [0, 0, 1]  # behind ray
        row = origin + [tmin] + direction + [tmax] + a + [0] + b + [0] + c + [0]
        data = (ctypes.c_float * 20)(*row)
        output = (ctypes.c_float * 4)(0, -999, -999, -999)
        lib.cycles_triangle(data, output)
        values.extend(data); expected.extend(output)
    triangles = translated + BINDINGS + '''
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x; if (i >= arrayLength(&outputs)) { return; }
    let p = inputs[5u*i]; let d = inputs[5u*i+1u];
    var u = -999.0; var v = -999.0; var t = -999.0;
    let hit = c_ray_triangle_intersect(p.xyz, d.xyz, p.w, d.w,
        inputs[5u*i+2u].xyz, inputs[5u*i+3u].xyz, inputs[5u*i+4u].xyz, &u, &v, &t);
    outputs[i] = vec4<f32>(select(0.0, 1.0, hit), u, v, t);
}
'''
    cases.append(shader_case('triangles', triangles, values, expected, 256))
    values, expected = [], []
    # All 41 scalar math node operations in the pinned revision.
    for op in range(41):
        for i in range(256):
            a, b, c = [rng.uniform(-4, 4) for _ in range(3)]
            if i < 16:
                a, b, c = [(0, 0, 0), (-2, 3, .1), (-2, 2, .1), (-2, .5, .1),
                           (1, 1, 0), (.5, 0, 0), (2, -3, .1), (0, -1, 1),
                           (4, 2, 0), (-4, 2, 1), (1e-6, 2, 1), (-1, -1, 1),
                           (0, 1, 1), (2, 1, 0), (1, 0, 1), (3, 2, 1)][i]
            row = array.array('f', [op, a, b, c])
            ref = lib.cycles_math(op, *row[1:])
            if not math.isfinite(ref):
                raise RuntimeError(f'Nonfinite CPU fixture: {op}, {list(row)}')
            values.extend(row); expected.extend([ref, 0, 0, 0])
    source = translated + BINDINGS + '''
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3<u32>) {
    let i = id.x; if (i >= arrayLength(&outputs)) { return; }
    let p = inputs[i]; outputs[i] = vec4<f32>(c_svm_math(u32(p.x), p.y, p.z, p.w), 0.0, 0.0, 0.0);
}
'''
    cases.append(shader_case('material_math', source, values, expected, (len(expected)//4 + 63)//64))
    cases.append(bvh.prepare(ROOT, WORK, upstream, translated))
    (WORK / 'cases.json').write_text(json.dumps(cases, indent=2) + '\n')
    source_files = ['src/util/math_intersect.h', 'src/util/math_base.h', 'src/kernel/svm/math_util.h',
                    'src/kernel/svm/types.h', 'src/kernel/bvh/traversal.h', 'src/kernel/integrator/state.h',
                    'src/kernel/closure/bsdf_diffuse.h', 'src/device/queue.h']
    provenance = {'upstream': 'https://github.com/blender/cycles', 'revision': revision,
                  'compiler': subprocess.check_output(['clang++', '--version'], text=True).splitlines()[0],
                  'referenceCompileCommand': command,
                  'files': {p: hashlib.sha256((upstream/p).read_bytes()).hexdigest() for p in source_files}}
    (WORK / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    print('Prepared', len(cases), 'experiments')

def verify(directory):
    results = []
    failed = False
    for test in json.loads((WORK / 'cases.json').read_text()):
        name = test['Name']
        expected = array.array('f'); expected.frombytes((WORK / f'{name}.expected.bin').read_bytes())
        actual = array.array('f'); actual.frombytes((directory / f'{name}.gpu.bin').read_bytes())
        if len(expected) != len(actual): raise RuntimeError('Output length mismatch: ' + name)
        errors = []; max_abs = 0; max_scaled = 0
        for i, (a, e) in enumerate(zip(actual, expected)):
            delta = abs(a-e)
            tolerance = 5e-5 + 2e-4 * abs(e)
            # Intersection classification must match exactly.
            if name == 'triangles' and i % 4 == 0: tolerance = 0
            if name == 'bvh' and i % 4 == 3: tolerance = 0
            max_abs = max(max_abs, delta); max_scaled = max(max_scaled, delta / max(1, abs(e)))
            if not math.isfinite(a) or delta > tolerance:
                errors.append({'index': i, 'actual': a, 'expected': e})
        item = {'name': name, 'records': len(actual)//4, 'passed': not errors,
                'mismatchedScalars': len(errors), 'maxAbsoluteError': max_abs,
                'maxErrorScaledByMaxOneReference': max_scaled, 'firstErrors': errors[:10]}
        if name == 'triangles':
            item['referenceHits'] = sum(expected[i] for i in range(0, len(expected), 4))
            item['classificationMismatches'] = sum(actual[i] != expected[i] for i in range(0, len(expected), 4))
        if name == 'bvh':
            item['referenceHits'] = sum(expected[i] >= 0 for i in range(3, len(expected), 4))
            item['primitiveMismatches'] = sum(actual[i] != expected[i] for i in range(3, len(expected), 4))
        if name == 'material_math':
            item['mismatchesByOpcode'] = {str(op): sum(e['index']//1024 == op for e in errors)
                                         for op in range(41) if any(e['index']//1024 == op for e in errors)}
        results.append(item); failed |= bool(errors)
    report = {'passed': not failed, 'absoluteTolerance': 5e-5, 'relativeTolerance': 2e-4, 'tests': results}
    (directory / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))
    return 1 if failed else 0

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('action', choices=['prepare', 'linux', 'android', 'verify'])
    p.add_argument('--upstream', type=Path, default=SOURCE)
    p.add_argument('--results', type=Path, default=WORK)
    p.add_argument('--managed', action='store_true', help='Use the auxiliary C# Linux harness')
    p.add_argument('--serial', help='ADB device serial for Android native probes')
    p.add_argument('--prototype', action='store_true', help='Run the native Cycles scene-to-image gate')
    p.add_argument('--materials', action='store_true', help='Run the original Cycles SVM/BSDF parity gate')
    p.add_argument('--asset-name', help='Render a prepared real-model geometry gate from parity/work/assets')
    p.add_argument('--trace-pixel', type=int, help='Diagnose a pixel in a prepared asset on Linux')
    p.add_argument('--paths', action='store_true', help='Compare WGSL multi-bounce paths with original Cycles megakernel')
    p.add_argument('--camera', action='store_true', help='Run WGSL camera/Sobol and multi-bounce integration gates')
    p.add_argument('--lighting', action='store_true', help='Run finite-light sampling, shadows and MIS gates')
    args = p.parse_args()
    if args.lighting:
        if args.prototype or args.materials or args.asset_name or args.paths or args.camera: p.error('Select one gate at a time')
        if args.action not in ('linux', 'android'): p.error('--lighting requires linux or android')
        command = ['python3', str(ROOT.parent/'parity/tools/light_gate.py'), args.action]
        if args.action == 'android':
            if not args.serial: p.error('--serial is required for Android')
            command += ['--serial', args.serial]
        raise SystemExit(subprocess.call(command))
    if args.camera:
        if args.prototype or args.materials or args.asset_name or args.paths: p.error('Select one gate at a time')
        if args.action not in ('linux', 'android'): p.error('--camera requires linux or android')
        command = ['python3', str(ROOT.parent/'parity/tools/camera_gate.py'), args.action]
        if args.action == 'android':
            if not args.serial: p.error('--serial is required for Android')
            command += ['--serial', args.serial]
        raise SystemExit(subprocess.call(command))
    if args.paths:
        if args.prototype or args.materials or args.asset_name: p.error('Select one gate at a time')
        if args.action not in ('linux', 'android'): p.error('--paths requires linux or android')
        command = ['python3', str(ROOT.parent/'parity/tools/path_gate.py'), args.action]
        if args.action == 'android':
            if not args.serial: p.error('--serial is required for Android')
            command += ['--serial', args.serial]
        raise SystemExit(subprocess.call(command))
    if args.asset_name:
        if args.prototype or args.materials: p.error('Select one gate at a time')
        if args.action not in ('linux', 'android'): p.error('--asset-name requires linux or android')
        command = ['python3', str(ROOT.parent/'parity/tools/asset_gate.py'), args.action, '--name', args.asset_name]
        if args.trace_pixel is not None: command += ['--trace-pixel', str(args.trace_pixel)]
        if args.action == 'android':
            if not args.serial: p.error('--serial is required for Android')
            command += ['--serial', args.serial]
        raise SystemExit(subprocess.call(command))
    if args.materials:
        if args.prototype: p.error('Select one gate at a time')
        if args.action not in ('linux', 'android'): p.error('--materials requires linux or android')
        command = ['python3', str(ROOT.parent/'parity/tools/material_gate.py'), args.action]
        if args.action == 'android':
            if not args.serial: p.error('--serial is required for Android')
            command += ['--serial', args.serial]
        raise SystemExit(subprocess.call(command))
    if args.prototype:
        if args.action not in ('linux', 'android'): p.error('--prototype requires linux or android')
        command = ['python3', str(ROOT.parent/'prototype/tools/run.py'), args.action]
        if args.action == 'android':
            if not args.serial: p.error('--serial is required for Android')
            command += ['--serial', args.serial]
        raise SystemExit(subprocess.call(command))
    if args.action == 'prepare': prepare(args.upstream)
    elif args.action == 'linux':
        if args.managed:
            subprocess.run(['dotnet', 'build', str(ROOT/'GpuProbe'), '-c', 'Release', '--nologo'], check=True)
            with (WORK/'gpu.json').open('w') as out:
                subprocess.run(['dotnet', str(ROOT/'GpuProbe/bin/Release/net10.0/GpuProbe.dll'), str(WORK)], stdout=out, check=True)
        else: native_runner.linux(ROOT, WORK)
        raise SystemExit(verify(WORK))
    elif args.action == 'android':
        if not args.serial: p.error('--serial is required for Android')
        result = native_runner.android(ROOT, WORK, args.serial)
        raise SystemExit(verify(result))
    else: raise SystemExit(verify(args.results))
