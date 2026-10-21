#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Object shadow terminator offsets: original CPU film oracles and native command replay.

codebrix_cycles_shadow_terminator_gate (lighting_gate.cpp, CODEBRIX_SHADOW_TERMINATOR_GATE)
renders a coarse smooth-shaded sphere lit by a grazing sun and by a point light
with geometry offsets 0.1/0 and shading offsets 0.5/0, two instances with
geometry 0.3 and shading 1, and a moving, deforming sphere, through native scene
preparation and the native scheduler, against independent original CPU kernel
images. The gate also checks that each offset changes the CPU image (it is
exercised) and replays every captured command stream.
"""
import argparse
import contextlib
import json
import struct
import subprocess
from pathlib import Path
from build import REPO, WORK, PACKAGE
from device_gate import sha, save
from render_gate import inputs
import render_replay

CASES = 10
# Pairs that differ only in one offset (same light and material): offset case, control.
ISOLATED = {'sun-geometry': (0, 3), 'sun-shading': (1, 3), 'point-geometry': (4, 7), 'point-shading': (5, 7)}


def floats(path):
    data = path.read_bytes()
    return struct.unpack('<%df' % (len(data)//4), data)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--skip-build', action='store_true')
    parser.add_argument('--jobs', default='6')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    status = {'complete': False, 'phase': 'prepare'}
    save(out/'status.json', status)
    source = inputs()
    runtime = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    binary = WORK/'cycles-build/bin/codebrix_cycles_shadow_terminator_gate'
    reports = {}
    try:
        if not args.skip_build:
            status['phase'] = 'build'
            save(out/'status.json', status)
            with (out/'build.log').open('w') as log:
                subprocess.run(['python3', str(REPO/'native/tools/build_cycles.py'), '--jobs', args.jobs,
                                '--target', 'codebrix_cycles_shadow_terminator_gate'], stdout=log, stderr=subprocess.STDOUT, check=True)
            render_replay.build('linux-x64')
        replay = render_replay.BASE/'linux-x64/render_replay'
        pins = {str(p): sha(p) for p in [runtime, binary, replay]}
        save(out/'invocation.json', {'productionSourceSha256': source, 'fileSha256': pins})
        status['phase'] = 'render'
        save(out/'status.json', status)
        directory = out/'shadow_terminator'
        directory.mkdir()
        with (directory/'stdout.txt').open('w') as stdout, (directory/'stderr.txt').open('w') as stderr:
            subprocess.run([str(binary), str(directory/'run')], stdout=stdout, stderr=stderr, check=True, timeout=900)
        report = json.loads((directory/'stdout.txt').read_text())
        assert report['passed'] and report['cyclesAllocationBytesAfterCleanup'] == 0
        assert report['cases'] == CASES and report['nativeSceneAndScheduler'] and report['independentOriginalCpuImage']
        assert report['deformingSurfacePaths'] > 100 and report['blocked'] > 100
        assert Path((directory/'run/library.txt').read_text().strip()).resolve() == runtime.resolve()
        cases = {}
        for case in range(CASES):
            cases[case] = json.loads((directory/'run'/('case-'+str(case))/'result.json').read_text())
        report['caseResults'] = cases
        isolated = {}
        for name, (offset, control) in ISOLATED.items():
            a = floats(directory/'run'/('case-'+str(offset))/'run.cpu.film.bin')
            b = floats(directory/'run'/('case-'+str(control))/'run.cpu.film.bin')
            changed = sum(abs(x-y) > 1e-6 for x, y in zip(a, b))
            assert changed > 10, name
            isolated[name] = {'case': offset, 'control': control, 'changedComponents': changed}
        report['offsetChangesCpuImage'] = isolated
        reports['shadow_terminator'] = report
        save(out/'results.json', reports)
        status['phase'] = 'replay'
        save(out/'status.json', status)
        for case in range(CASES):
            name = 'replay-'+str(case)
            with (out/(name+'.log')).open('w') as log, contextlib.redirect_stdout(log):
                render_replay.run('linux-x64', directory/'run'/('case-'+str(case)), out/name)
            reports[name] = json.loads((out/name/'validation.json').read_text())
            save(out/'results.json', reports)
        assert source == inputs() and all(sha(Path(name)) == digest for name, digest in pins.items())
        status.update(complete=True, passed=True, phase='complete', inputsUnchanged=True,
                      strictDifferences=report['strictDifferences'], maximumAbsoluteError=report['maximumAbsoluteError'],
                      offsetChangesCpuImage=isolated,
                      scope='Object shadow_terminator_geometry_offset (shadow-ray origin lift on smooth triangles, static, '
                            'instanced, moving and deforming) and shadow_terminator_shading_offset (shift_cos_in in BSDF '
                            'evaluation and sampling, offsets 0 to 1) under sun and point lights against original CPU '
                            'images, with native command replays. Shading offsets above 1 are rejected before dispatch.')
    except Exception as error:
        status.update(complete=True, passed=False, error=repr(error))
        raise
    finally:
        save(out/'status.json', status)
    print(json.dumps(status), flush=True)


if __name__ == '__main__':
    main()
