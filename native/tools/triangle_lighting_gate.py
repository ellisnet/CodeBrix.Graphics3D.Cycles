#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Bounded constant-emission triangle sampling/MIS and native image replay."""
import argparse
import contextlib
import json
import subprocess
import sys
from pathlib import Path
from build import REPO, WORK, PACKAGE
from device_gate import save, sha
from render_gate import inputs
import render_replay


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    source = inputs()
    kinds = ['triangle_sample', 'surface', 'triangle_lighting']
    runtime = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    pins = {str(runtime): sha(runtime)}
    state = {'complete': False, 'phase': 'build'}
    reports = {}
    save(out/'status.json', state)
    save(out/'source-pins.json', source)
    try:
        if not args.skip_build:
            command = [sys.executable, str(REPO/'native/tools/build_cycles.py'), '--jobs', '2']
            for kind in kinds:
                command += ['--target', 'codebrix_cycles_'+kind+'_gate']
            with (out/'build.log').open('w') as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        render_replay.build('linux-x64')
        binaries = {kind: WORK/'cycles-build/bin'/('codebrix_cycles_'+kind+'_gate') for kind in kinds}
        for binary in [*binaries.values(), render_replay.BASE/'linux-x64/render_replay']:
            pins[str(binary)] = sha(binary)
        save(out/'binary-runtime-pins.json', pins)
        for kind, binary in binaries.items():
            state['phase'] = kind
            save(out/'status.json', state)
            folder = out/kind
            folder.mkdir()
            command = [str(binary), str(folder/'run')]
            save(folder/'invocation.json', {'command': command})
            with (folder/'stdout.txt').open('w') as stdout, (folder/'stderr.txt').open('w') as stderr:
                subprocess.run(command, stdout=stdout, stderr=stderr, check=True, timeout=900)
            report = json.loads((folder/'stdout.txt').read_text())
            assert report['passed'] and report['cyclesAllocationBytesAfterCleanup'] == 0
            image = kind == 'triangle_lighting'
            library = folder/'run/library.txt' if image else folder/'run.library.txt'
            assert Path(library.read_text().strip()).resolve() == runtime.resolve()
            if kind == 'triangle_sample':
                assert report['samples'] == 4096 and report['guardCases'] == 5
            elif image:
                assert report['cases'] == 6 and report['meshMisPaths'] > 10
                assert report['unsupportedEmitterConfigurationsRejected'] == 0
                assert report['nativeSceneAndScheduler'] and report['independentOriginalCpuImage']
                assert not report['fullLightingMisProven']
            else:
                assert report['invalidCasesRejected'] == 21
            reports[kind] = report
            save(out/'results.json', reports)
        for case in range(6):
            name = 'replay-'+str(case)
            state['phase'] = name
            save(out/'status.json', state)
            with (out/(name+'.log')).open('w') as log, contextlib.redirect_stdout(log):
                render_replay.run('linux-x64', out/'triangle_lighting/run'/('case-'+str(case)), out/name)
            reports[name] = json.loads((out/name/'validation.json').read_text())
            save(out/'results.json', reports)
        state.update(complete=True, passed=True, phase='complete',
                     scope='Flat-CDF constant triangle emission, native area/solid-angle sampling, forward-hit MIS, mirrored static geometry, mixed point/mesh lighting and a moving/deforming emitter. Five independent CPU images and command replays. Static instanced triangle emitters are explicitly rejected after a two-pixel image mismatch traced to shading-point barycentric divergence; environment/nonconstant emitters, light trees and untested combinations remain open.')
    except BaseException as error:
        state.update(complete=True, passed=False, error=repr(error))
        raise
    finally:
        state['inputsUnchanged'] = source == inputs() and all(sha(Path(p)) == h for p, h in pins.items())
        if not state['inputsUnchanged']:
            state['passed'] = False
        save(out/'status.json', state)
    assert state['passed']
    print(json.dumps(state), flush=True)


if __name__ == '__main__':
    main()
