#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase C close-out copy (via C3) of the B3 copy of all_lanes_v2.py (maintained Linux coordinator), parameterised by tree/output.

Only paths and optional skip flags differ; every gate, criterion and assertion is unchanged.
"""
import contextlib
import json
import subprocess
import sys
from pathlib import Path

import argparse
_ap = argparse.ArgumentParser(); _ap.add_argument('--tree', type=Path, required=True); _ap.add_argument('--output', type=Path, required=True); _ap.add_argument('--skip-build', action='store_true'); _ap.add_argument('--skip-audit', action='store_true'); _args = _ap.parse_args()
ROOT = _args.tree.resolve()
BASE = _args.output.resolve()
BASE.mkdir(exist_ok=False)
sys.path.insert(0, str(ROOT / 'native/tools'))
from build import PACKAGE, WORK
from device_gate import save, sha
from render_gate import inputs
import render_replay

kinds = ['closest', 'shader_data', 'shadow', 'normal', 'image', 'object_motion_render',
         'volume', 'volume_render', 'glass_svm', 'glass_bounce', 'glass_render',
         'microfacet_svm', 'microfacet_bounce', 'microfacet_render', 'camera', 'render',
         'emitter_sample', 'broad_light_state', 'broad_lighting', 'light_state',
         'lighting_mis', 'feature_combination', 'transparent_shadow', 'transparent_lighting',
         'svm_surface', 'device', 'deforming_motion_render', 'volume_boundary', 'closed_volume_render']
source = inputs()
save(BASE / 'source-pins.json', source)
pins = {}
state = {'complete': False, 'phase': 'build', 'targets': len(kinds)}
save(BASE / 'status.json', state)
reports = {}


def run_logged(name, command):
    state['phase'] = name
    save(BASE / 'status.json', state)
    save(BASE / (name + '-invocation.json'), {'command': command})
    with (BASE / (name + '.stdout.txt')).open('w') as stdout, (BASE / (name + '.stderr.txt')).open('w') as stderr:
        result = subprocess.run(command, stdout=stdout, stderr=stderr)
    reports[name] = result.returncode
    save(BASE / 'gate-exit-codes.json', reports)
    print(name, result.returncode, flush=True)
    result.check_returncode()


def broad_remainder():
    # The transparent gate runs the same 28 broad/point/combined images.
    # Reuse only this run's captures after verifying all source and binary pins.
    assert source == inputs() and all(sha(Path(p)) == h for p, h in pins.items())
    prior = BASE / 'transparent_shadow_gate'
    assert json.loads((prior / 'status.json').read_text())['passed']
    prior_results = json.loads((prior / 'results.json').read_text())
    out = BASE / 'broad_lighting_remainder'
    out.mkdir()
    results = {}
    runtime = PACKAGE / 'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    for kind in ('broad_lighting', 'lighting_mis', 'feature_combination'):
        report = prior_results[kind]
        assert report['passed'] and report['cyclesAllocationBytesAfterCleanup'] == 0
        assert report['cases'] == (12 if kind == 'broad_lighting' else 8)
        assert report['nativeSceneAndScheduler'] and report['independentOriginalCpuImage']
        assert Path((prior / kind / 'run/library.txt').read_text().strip()).resolve() == runtime.resolve()
        results[kind] = {'report': report, 'source': str(prior / kind),
                         'sourceReportSha256': sha(prior / kind / 'stdout.txt')}
    for kind in ('emitter_sample', 'broad_light_state', 'light_state'):
        directory = out / kind
        directory.mkdir()
        command = [str(WORK / 'cycles-build/bin' / ('codebrix_cycles_' + kind + '_gate')), str(directory / 'run')]
        with (directory / 'stdout.txt').open('w') as stdout, (directory / 'stderr.txt').open('w') as stderr:
            subprocess.run(command, stdout=stdout, stderr=stderr, check=True, timeout=900)
        report = json.loads((directory / 'stdout.txt').read_text())
        assert report['passed'] and report['cyclesAllocationBytesAfterCleanup'] == 0
        assert Path((directory / 'run.library.txt').read_text().strip()).resolve() == runtime.resolve()
        results[kind] = report
        save(out / 'results.json', results)
    for kind in ('broad_lighting', 'feature_combination'):
        for case in range(12 if kind == 'broad_lighting' else 8):
            name = kind + '-replay-' + str(case)
            with (out / (name + '.log')).open('w') as log, contextlib.redirect_stdout(log):
                render_replay.run('linux-x64', prior / kind / 'run' / ('case-' + str(case)), out / name)
            results[name] = json.loads((out / name / 'validation.json').read_text())
            save(out / 'results.json', results)
    assert source == inputs() and all(sha(Path(p)) == h for p, h in pins.items())
    save(out / 'status.json', {'complete': True, 'passed': True, 'inputsUnchanged': True,
                              'reports': len(results), 'sharedImagesFrom': str(prior)})


try:
    command = [sys.executable, str(ROOT / 'native/tools/build_cycles.py'), '--jobs', '2']
    for kind in kinds:
        command += ['--target', 'codebrix_cycles_' + kind + '_gate']
    if not _args.skip_build:
        run_logged('build', command)
    render_replay.build('linux-x64')
    binaries = [WORK / 'cycles-build/bin' / ('codebrix_cycles_' + k + '_gate') for k in kinds]
    paths = binaries + [render_replay.BASE / 'linux-x64/render_replay', PACKAGE / 'runtimes/linux-x64/native/libcodebrix_wgpu.so']
    pins = {str(path): sha(path) for path in paths}
    save(BASE / 'binary-runtime-pins.json', pins)
    command = [sys.executable, str(ROOT / 'tools/licensing/binary_audit.py'),
               '--build', str(WORK / 'cycles-build'), '--prefix', str(WORK / 'dependencies/install'),
               '--output', str(BASE / 'binary-audit.json')]
    for binary in binaries:
        command += ['--executable', str(binary)]
    if not _args.skip_audit:
        run_logged('binary-audit', command)
    for name in ['transparent_shadow_gate', 'deforming_motion_gate', 'volume_boundary_gate',
                 'volume_gate', 'glass_gate', 'camera_motion_gate']:
        run_logged(name, [sys.executable, str(ROOT / 'native/tools' / (name + '.py')),
                          '--skip-build', '--output', str(BASE / name)])
        if name == 'transparent_shadow_gate':
            state['phase'] = 'broad_lighting_remainder'
            save(BASE / 'status.json', state)
            broad_remainder()
            reports['broad_lighting_remainder'] = 0
            save(BASE / 'gate-exit-codes.json', reports)
            print('broad_lighting_remainder', 0, flush=True)
    state.update(complete=True, passed=True, phase='complete')
except BaseException as error:
    state.update(complete=True, passed=False, error=repr(error))
    raise
finally:
    state['productionSourcesUnchanged'] = source == inputs()
    state['binaryRuntimeInputsUnchanged'] = all(sha(Path(path)) == digest for path, digest in pins.items())
    if not state['productionSourcesUnchanged'] or not state['binaryRuntimeInputsUnchanged']:
        state['passed'] = False
    save(BASE / 'status.json', state)
