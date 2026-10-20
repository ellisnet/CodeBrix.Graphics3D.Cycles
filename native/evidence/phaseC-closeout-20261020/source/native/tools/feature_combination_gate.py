#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Exercise glass/refraction, camera motion and point/sun MIS in the same scenes."""
import argparse
import contextlib
import json
import subprocess
from pathlib import Path
from build import REPO, WORK, PACKAGE
from device_gate import sha, save
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
    status = {'complete': False, 'phase': 'prepare'}
    save(out/'status.json', status)
    runtime = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    binary = WORK/'cycles-build/bin/codebrix_cycles_feature_combination_gate'
    try:
        if not args.skip_build:
            with (out/'build.log').open('w') as log:
                subprocess.run(['python3', str(REPO/'native/tools/build_cycles.py'),
                                '--jobs', '2', '--target', binary.name],
                               stdout=log, stderr=subprocess.STDOUT, check=True)
            render_replay.build('linux-x64')
        replay_binary = render_replay.BASE/'linux-x64/render_replay'
        pins = {str(p): sha(p) for p in (runtime, binary, replay_binary)}
        save(out/'invocation.json', {'productionSourceSha256': source, 'fileSha256': pins})
        status['phase'] = 'images'
        save(out/'status.json', status)
        with (out/'stdout.txt').open('w') as stdout, (out/'stderr.txt').open('w') as stderr:
            subprocess.run([str(binary), str(out/'run')], stdout=stdout,
                           stderr=stderr, check=True, timeout=900)
        report = json.loads((out/'stdout.txt').read_text())
        assert report['passed'] and report['nativeSceneAndScheduler']
        assert report['independentOriginalCpuImage'] and report['pointSunMisEnabled']
        assert report['cases'] == 8 and report['transmissionPaths'] > 100
        assert report['forwardLightPaths'] > 100 and report['cyclesAllocationBytesAfterCleanup'] == 0
        assert Path((out/'run/library.txt').read_text().strip()).resolve() == runtime.resolve()
        results = {'images': report}
        save(out/'results.json', results)
        status['phase'] = 'replay'
        save(out/'status.json', status)
        for case in range(8):
            name = 'replay-'+str(case)
            with (out/(name+'.log')).open('w') as log, contextlib.redirect_stdout(log):
                render_replay.run('linux-x64', out/'run'/('case-'+str(case)), out/name)
            results[name] = json.loads((out/name/'validation.json').read_text())
            save(out/'results.json', results)
        assert source == inputs()
        assert all(sha(Path(p)) == digest for p, digest in pins.items())
        status.update(complete=True, passed=True, phase='complete', inputsUnchanged=True,
                      scope='Eight native scenes combining camera motion, GGX glass/refraction, '
                            'constant point/sun MIS and opaque shadows; original CPU films and command replay.')
    except BaseException as error:
        status.update(complete=True, passed=False, error=repr(error))
        raise
    finally:
        save(out/'status.json', status)
    print(json.dumps(status), flush=True)


if __name__ == '__main__':
    main()
