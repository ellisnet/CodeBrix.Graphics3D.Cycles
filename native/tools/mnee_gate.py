#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Validate manifold next-event estimation (caustics) on the WebGPU device.

Runs the lockstep state gate (codebrix_cycles_mnee_gate: INTERSECT_MNEE, the
surface stage's MNEE consumption, the MNEE shadow paths and the closest-stage
MNEE flags against the pinned CPU kernels, with the walk's outcome and iteration
count exact), the image gate (codebrix_cycles_mnee_render_gate: nine caustic
fixtures, independent CPU films, per-sample, captured and batched repeats), the
queue control gate (MNEE compaction ownership) and the command replays of every
captured image case."""
import argparse, contextlib, json, subprocess
from pathlib import Path
from build import REPO, WORK, PACKAGE
from device_gate import sha, save
from render_gate import inputs
import render_replay

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args(); out = args.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    status = {'complete': False, 'phase': 'prepare'}; save(out/'status.json', status)
    source = inputs(); runtime = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    pins = {str(runtime): sha(runtime)}; reports = {}
    kinds = ['mnee', 'mnee_render', 'device']
    try:
        if not args.skip_build:
            status['phase'] = 'build'; save(out/'status.json', status)
            command = ['python3', str(REPO/'native/tools/build_cycles.py'), '--jobs', '6']
            for kind in kinds: command += ['--target', 'codebrix_cycles_'+kind+'_gate']
            with (out/'build.log').open('w') as log: subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
            render_replay.build('linux-x64')
        for kind in kinds:
            path = WORK/'cycles-build/bin'/('codebrix_cycles_'+kind+'_gate'); pins[str(path)] = sha(path)
        path = render_replay.BASE/'linux-x64/render_replay'; pins[str(path)] = sha(path)
        save(out/'invocation.json', {'productionSourceSha256': source, 'fileSha256': pins})
        for kind in kinds:
            status['phase'] = kind; save(out/'status.json', status)
            directory = out/kind; directory.mkdir(); prefix = directory/'run'
            with (directory/'stdout.txt').open('w') as stdout, (directory/'stderr.txt').open('w') as stderr:
                subprocess.run([str(WORK/'cycles-build/bin'/('codebrix_cycles_'+kind+'_gate')), str(prefix)], stdout=stdout, stderr=stderr, check=True, timeout=1800)
            report = json.loads((directory/'stdout.txt').read_text()); assert report['passed']
            if kind == 'mnee':
                assert report['originalCpuIntersectMnee'] and report['movingCasterRefused'] and report['paths'] > 1000 and report['sampled'] >= 20 and len(report['cases']) == 8
            if kind == 'mnee_render':
                assert report['cases'] == 9 and report['nativeSceneAndScheduler'] and report['independentOriginalCpuImage'] and report['cyclesAllocationBytesAfterCleanup'] == 0
                for case in range(9):
                    result = json.loads((prefix/('case-'+str(case))/'result.json').read_text())
                    assert (result['mneePaths'] == 0) == (case == 3) and (case in (2, 3, 4, 8) or result['mneeSampled'] >= 5) and (result['aoPaths'] > 100) == (case == 5)
            if kind == 'device': assert report['compactionCases'] == 4 and report['cyclesAllocationBytesAfterCleanup'] == 0
            reports[kind] = report; save(out/'results.json', reports)
        status['phase'] = 'replay'; save(out/'status.json', status)
        for case in range(9):
            destination = out/('replay-'+str(case)); capture = out/'mnee_render/run'/('case-'+str(case))
            with (out/('replay-'+str(case)+'.log')).open('w') as log, contextlib.redirect_stdout(log): render_replay.run('linux-x64', capture, destination)
            reports['replay-'+str(case)] = json.loads((destination/'validation.json').read_text()); save(out/'results.json', reports)
        assert source == inputs() and all(sha(Path(name)) == digest for name, digest in pins.items())
        status.update(complete=True, passed=True, phase='complete', inputsUnchanged=True,
                      scope='MNEE through refractive (GGX glass and refraction) caustic casters from point and sun lamps, opaque shadows, '
                            'distribution light sampling, with fast GI (additive AO) and with motion blur (moving and deforming receivers, '
                            'deforming casters); moving casters, spot, area and background caustic lamps, the light tree, linking and '
                            'volumes with caustics are refused.')
    except Exception as error:
        status.update(complete=True, passed=False, error=repr(error)); raise
    finally: save(out/'status.json', status)
    print(json.dumps(status), flush=True)

if __name__ == '__main__': main()
