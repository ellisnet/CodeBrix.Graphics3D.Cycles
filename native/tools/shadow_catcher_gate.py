#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Shadow catcher: state lockstep and images against the original CPU kernels, the
volume refusal, ambient occlusion images, feature-combination images, and command replays of the recorded renders
(shadow_catcher_gate.cpp)."""
import argparse
import contextlib
import json
import subprocess
from pathlib import Path
from build import REPO, WORK, PACKAGE
from device_gate import sha, save
from render_gate import inputs
import render_replay

STATE_CASES, IMAGE_CASES, AO_CASES, COMBINATION_CASES = 4, 6, 3, 9
REPLAYS = (0, 1, 2, 4, 7, 8, 11, 12, 13)+tuple(range(14, 23))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--skip-build', action='store_true')
    parser.add_argument('--jobs', default='2')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    status = {'complete': False, 'phase': 'prepare'}
    save(out/'status.json', status)
    source = inputs()
    runtime = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    binary = WORK/'cycles-build/bin/codebrix_cycles_shadow_catcher_gate'
    pins = {str(runtime): sha(runtime)}
    try:
        if not args.skip_build:
            status['phase'] = 'build'
            save(out/'status.json', status)
            with (out/'build.log').open('w') as log:
                subprocess.run(['python3', str(REPO/'native/tools/build_cycles.py'), '--jobs', args.jobs,
                                '--target', 'codebrix_cycles_shadow_catcher_gate'], stdout=log, stderr=subprocess.STDOUT, check=True)
            render_replay.build('linux-x64')
        pins[str(binary)] = sha(binary)
        pins[str(render_replay.BASE/'linux-x64/render_replay')] = sha(render_replay.BASE/'linux-x64/render_replay')
        save(out/'invocation.json', {'productionSourceSha256': source, 'fileSha256': pins})
        status['phase'] = 'gate'
        save(out/'status.json', status)
        with (out/'stdout.txt').open('w') as stdout, (out/'stderr.txt').open('w') as stderr:
            subprocess.run([str(binary), str(out/'run')], stdout=stdout, stderr=stderr, check=True, timeout=1800)
        report = json.loads((out/'stdout.txt').read_text())
        assert report['passed'] and report['cyclesAllocationBytesAfterCleanup'] == 0 and report['volumeRefused']
        assert Path((out/'run/library.txt').read_text().strip()).resolve() == runtime.resolve()
        cases = {c['case']: c for c in report['cases']}
        assert sorted(cases) == list(range(STATE_CASES+IMAGE_CASES+1+AO_CASES+COMBINATION_CASES))
        for case in range(STATE_CASES):
            assert cases[case]['lockstep'] == 'local' and cases[case]['stateStages'] > 0 and cases[case]['repeatBitwiseDifferences'] == 0
        assert cases[1]['backgroundSplits'] > 0 and cases[8]['backgroundSplits'] > 0
        assert 'shadow catcher with volumes' in cases[10]['message']
        for case in range(STATE_CASES+IMAGE_CASES+1, STATE_CASES+IMAGE_CASES+1+AO_CASES):
            assert cases[case]['aoShadowPaths'] > 100 and cases[case]['aoUnoccluded'] > 10
        # Combination cases: shadow catcher with motion, the light tree, light linking, a spot lamp,
        # glass/principled transmission, shadow terminator offsets and a non-constant emitter.
        for case in range(STATE_CASES+IMAGE_CASES+1+AO_CASES, STATE_CASES+IMAGE_CASES+1+AO_CASES+COMBINATION_CASES):
            assert cases[case]['splits'] > 100 and cases[case]['shadows'] > 100 and cases[case]['visible'] > 100
        results = {'gate': report}
        for case in REPLAYS:
            name = 'replay-'+str(case)
            status['phase'] = name
            save(out/'status.json', status)
            with (out/(name+'.log')).open('w') as log, contextlib.redirect_stdout(log):
                render_replay.run('linux-x64', out/'run'/('case-'+str(case)), out/name)
            results[name] = json.loads((out/name/'validation.json').read_text())
            save(out/'results.json', results)
        assert source == inputs() and all(sha(Path(name)) == digest for name, digest in pins.items())
        status = {'complete': True, 'phase': 'complete', 'passed': True}
    except BaseException as error:
        status.update({'complete': False, 'passed': False, 'error': repr(error)})
        raise
    finally:
        save(out/'status.json', status)


if __name__ == '__main__':
    main()
