#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Native GGX reflection comparisons and captured GPU replay, without fetching source."""
import argparse
import concurrent.futures
import json
import shutil
import subprocess
import tempfile
from pathlib import Path
from build import WORK, PACKAGE
from device_gate import sha, save
import arena_replay
import render_gate
import render_replay

OUT = WORK/'microfacet'
CAPTURE_FILES = ['run.'+suffix for suffix in ('wgsl', 'arena.bin', 'params.bin', 'reference.bin', 'capture.json')]


def native(kind):
    for name in ('svm', 'bounce', 'render') if kind == 'all' else (kind,):
        folder = OUT/name
        folder.mkdir(parents=True, exist_ok=True)
        exe = WORK/'cycles-build/bin'/('codebrix_cycles_microfacet_'+name+'_gate')
        command = [str(exe), str(folder/'run')]
        digest = sha(exe)
        sources = render_gate.inputs()
        save(folder/'invocation.json', {'command':command, 'executableSha256':digest,
                                       'sourceSha256':sources})
        with (folder/'run.stdout.txt').open('w') as stdout, (folder/'run.stderr.txt').open('w') as stderr:
            result = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=600)
        if result.returncode:
            raise RuntimeError(f'{name} failed; see {folder}/run.stderr.txt')
        assert sha(exe) == digest and sources == render_gate.inputs(), 'Inputs changed during native test'
        report = json.loads((folder/'run.stdout.txt').read_text())
        assert report['passed'] and report['cyclesAllocationBytesAfterCleanup'] == 0
        if name == 'svm':
            expected = {'compiledGraphs':20, 'cases':167, 'paths':16218,
                        'closureFamilies':6, 'integerChecks':1102824,
                        'floatChecks':28170666, 'stateChecks':23941120,
                        'untouchedGuardBytes':21376, 'invalidCasesRejected':16}
            assert report['originalCpuSurfaceSvm']
            assert all(report[k] == v for k, v in expected.items()), 'GGX SVM coverage changed'
        elif name == 'bounce':
            assert report['originalCpuSurfaceIntegrator']
            assert report['cases'] == 22 and report['paths'] == 21420
            assert report['invalidCasesRejected'] == 21 and report['zeroCapacityPathsRejected'] == 1020
        library = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
        loaded = (folder/'run.library.txt').read_text().strip()
        assert Path(loaded).resolve() == library.resolve() and sha(Path(loaded)) == sha(library)
        if name == 'svm':
            save(folder/'capture-validation.json', {
                'passed':True, 'invocationSha256':sha(folder/'invocation.json'),
                'inputSha256':{name:sha(folder/name) for name in CAPTURE_FILES}})
        print(name, json.dumps(report), flush=True)


def replay_kernels():
    source = OUT/'svm'
    validation = json.loads((source/'capture-validation.json').read_text())
    assert validation['passed'] and validation['invocationSha256'] == sha(source/'invocation.json'), 'Missing/stale native capture validation'
    invocation = json.loads((source/'invocation.json').read_text())
    assert invocation['sourceSha256'] == render_gate.inputs(), 'Native capture predates current sources'
    inputs = validation['inputSha256']
    assert set(inputs) == set(CAPTURE_FILES) and all(sha(source/name) == digest for name, digest in inputs.items()), 'Native capture changed after validation'
    captures = OUT/'replay-inputs'
    captures.mkdir(parents=True, exist_ok=True)
    fixed = Path(tempfile.mkdtemp(prefix='svm-', dir=captures))
    for name in CAPTURE_FILES:
        shutil.copy2(source/name, fixed/name)
    assert all(sha(fixed/name) == digest for name, digest in inputs.items()), 'Native capture changed while copying'
    save(fixed/'origin.json', {'validation':validation, 'invocation':invocation})
    arena_replay.configure(fixed, OUT/'kernel-replay', '/data/local/tmp/codebrix-cycles-microfacet-svm')
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        for result in [pool.submit(arena_replay.build, rid) for rid in sorted({d[0] for d in render_gate.DEVICES})]:
            result.result()
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        jobs = [(label, pool.submit(arena_replay.run, rid, serial, timeout=600))
                for rid, serial, label in render_gate.DEVICES]
        errors = {}
        for label, result in jobs:
            try:
                result.result()
            except Exception as error:
                errors[label] = str(error)
    assert all(sha(fixed/name) == digest for name, digest in inputs.items()), 'Fixed capture changed during replay'
    save(OUT/'kernel-replay/batch.json', {'passed':not errors, 'errors':errors,
                                       'capture':str(fixed), 'inputSha256':inputs})
    if errors:
        raise RuntimeError('Microfacet replay failures: '+json.dumps(errors))


def replay_render():
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        for result in [pool.submit(render_replay.build, rid) for rid in sorted({d[0] for d in render_gate.DEVICES})]:
            result.result()
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for result in [pool.submit(render_replay.run, rid, OUT/'render', OUT/'render-replay'/label, serial)
                       for rid, serial, label in render_gate.DEVICES]:
            result.result()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['native', 'replay-kernels', 'replay-render'])
    parser.add_argument('--kind', choices=['all', 'svm', 'bounce', 'render'], default='all')
    args = parser.parse_args()
    if args.action == 'native':
        native(args.kind)
    elif args.action == 'replay-kernels':
        replay_kernels()
    else:
        replay_render()
