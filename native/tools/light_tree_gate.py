#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Light tree and light linking: state probe, images against the original CPU kernels, host guards and
native command replays of every case (codebrix_cycles_light_tree_gate)."""
import argparse
import contextlib
import json
import subprocess
from pathlib import Path
from build import REPO, WORK, PACKAGE
from device_gate import sha, save
from render_gate import inputs
import render_replay

CASES = 8


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--skip-build', action='store_true')
    parser.add_argument('--skip-replay', action='store_true')
    parser.add_argument('--jobs', default='2')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    status = {'complete': False, 'phase': 'prepare'}
    save(out/'status.json', status)
    source = inputs()
    runtime = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    pins = {str(runtime): sha(runtime)}
    reports = {}
    try:
        if not args.skip_build:
            status['phase'] = 'build'
            save(out/'status.json', status)
            with (out/'build.log').open('w') as log:
                subprocess.run(['python3', str(REPO/'native/tools/build_cycles.py'), '--jobs', args.jobs,
                                '--target', 'codebrix_cycles_light_tree_gate'], stdout=log, stderr=subprocess.STDOUT, check=True)
            if not args.skip_replay:
                render_replay.build('linux-x64')
        binary = WORK/'cycles-build/bin/codebrix_cycles_light_tree_gate'
        pins[str(binary)] = sha(binary)
        if not args.skip_replay:
            pins[str(render_replay.BASE/'linux-x64/render_replay')] = sha(render_replay.BASE/'linux-x64/render_replay')
        save(out/'invocation.json', {'productionSourceSha256': source, 'fileSha256': pins})
        status['phase'] = 'light_tree'
        save(out/'status.json', status)
        directory = out/'light_tree'
        directory.mkdir()
        with (directory/'stdout.txt').open('w') as stdout, (directory/'stderr.txt').open('w') as stderr:
            subprocess.run([str(binary), str(directory/'run')], stdout=stdout, stderr=stderr, check=True, timeout=1800)
        report = json.loads((directory/'stdout.txt').read_text())
        assert report['passed'] and report['cases'] == CASES and report['cyclesAllocationBytesAfterCleanup'] == 0
        assert Path((directory/'run/library.txt').read_text().strip()).resolve() == runtime.resolve()
        reports['light_tree'] = report
        save(out/'results.json', reports)
        if not args.skip_replay:
            # Every capture is replayed and reported; a failed replay fails the gate after the last one.
            failed = []
            for case in range(CASES):
                status['phase'] = 'replay-'+str(case)
                save(out/'status.json', status)
                name = 'light_tree-replay-'+str(case)
                try:
                    with (out/(name+'.log')).open('w') as log, contextlib.redirect_stdout(log):
                        render_replay.run('linux-x64', directory/'run'/('case-'+str(case)), out/name)
                    reports[name] = json.loads((out/name/'validation.json').read_text())
                except Exception as error:
                    reports[name] = {'passed': False, 'error': repr(error)}
                    failed.append(case)
                save(out/'results.json', reports)
            assert not failed, 'Light tree replays failed for cases '+', '.join(map(str, failed))
        assert source == inputs() and all(sha(Path(name)) == digest for name, digest in pins.items())
        status.update(complete=True, passed=True, phase='complete', inputsUnchanged=True,
                      scope='Light tree selection, sampling and forward pdfs (lamps, many-triangle, mixed with HDRI world, instanced, '
                            'moving/deforming emitters, a glass receiver) and light linking on the flat CDF and on the tree, against the original CPU kernels.')
    except Exception as error:
        status.update(complete=True, passed=False, error=repr(error))
        raise
    finally:
        save(out/'status.json', status)
    print(json.dumps(status), flush=True)


if __name__ == '__main__':
    main()
