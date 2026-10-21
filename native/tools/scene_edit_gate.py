#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Scene edits after a render and device-error recovery through the C ABI.

native/tests/host/scene_edit_consumer.c renders a base scene, edits it after
that render, renders again and saves the film; the same sequence runs again in
a new host (replay), and a fresh CPU host renders the edited scene from scratch
as the oracle. CPU: edited, replay and fresh films must be bit-identical. WebGPU:
edited and replay films must meet the practical criterion against the fresh CPU
film (strict differences are reported). WebGPU also provokes the render-time
rejections of a depth pass and an orthographic camera, removes the cause and
renders again on the same host (recovery).
"""
import argparse
import json
import math
import os
import struct
import subprocess
import sys
from pathlib import Path
from build import REPO, ROOT, WORK, PACKAGE
from device_gate import sha, save
from render_gate import inputs


def film(path):
    data = path.read_bytes()
    values = struct.unpack('<%df' % (len(data)//4), data)
    assert values and all(math.isfinite(v) for v in values), path
    return data, values


def compare(actual, reference):
    a_bytes, a = film(actual)
    b_bytes, b = film(reference)
    assert len(a) == len(b)
    errors = [abs(x-y) for x, y in zip(a, b)]
    return dict(film=actual.name, reference=reference.name, bitwiseEqual=a_bytes == b_bytes,
                maximumAbsoluteError=max(errors),
                practicalFailures=sum(e > 5e-5+2e-4*abs(y) for e, y in zip(errors, b)),
                strictDifferences=sum(e > 1e-7+1e-7*abs(y) for e, y in zip(errors, b)))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--skip-build', action='store_true')
    parser.add_argument('--jobs', default='6')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    status = dict(complete=False, phase='prepare')
    save(out/'status.json', status)
    sources = inputs()
    library = WORK/'cycles-build/lib/libcodebrix_cycles_host.so'
    binary = WORK/'cycles-build/bin/codebrix_host_scene_edit_test'
    runtime = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    environment = dict(os.environ)
    environment.pop('OCIO', None)
    environment['LD_LIBRARY_PATH'] = str(library.parent)
    results = {}
    try:
        if not args.skip_build:
            status['phase'] = 'build'
            save(out/'status.json', status)
            with (out/'build.log').open('w') as log:
                subprocess.run([sys.executable, str(ROOT/'tools/build_cycles.py'), '--jobs', args.jobs,
                                '--target', 'codebrix_cycles_host', '--target', 'codebrix_host_scene_edit_test'],
                               stdout=log, stderr=subprocess.STDOUT, check=True)
        pins = {str(p): sha(p) for p in [library, binary, runtime]}
        save(out/'invocation.json', dict(productionSourceSha256=sources, fileSha256=pins))
        for mode in ['cpu', 'gpu']:
            status['phase'] = mode
            save(out/'status.json', status)
            folder = out/mode
            folder.mkdir()
            with (folder/'stdout.txt').open('w') as stdout, (folder/'stderr.txt').open('w') as stderr:
                subprocess.run([str(binary), str(folder/'run'), mode], cwd=REPO, env=environment,
                               stdout=stdout, stderr=stderr, check=True, timeout=900)
            report = json.loads((folder/'stdout.txt').read_text())
            assert report['passed'] and report['backend'] == mode and report['scenarios'] == 13
            assert report['changedScenarios'] == 12
            rows = {}
            for name in report['names']:
                path = lambda kind: folder/f'run.{name}.{kind}.rgba.bin'
                row = dict(edited=compare(path('edited'), path('fresh-cpu')), replay=compare(path('replay'), path('fresh-cpu')),
                           before=compare(path('before'), path('base-cpu')))
                if mode == 'cpu':
                    # The CPU reference renders the edited host bit for bit like a new host.
                    assert row['edited']['bitwiseEqual'] and row['replay']['bitwiseEqual'] and row['before']['bitwiseEqual'], name
                else:
                    assert all(row[k]['practicalFailures'] == 0 for k in row), name
                rows[name] = row
            report['comparisons'] = rows
            if mode == 'gpu':
                assert report['recoveryPassRejected'] == 3 and report['recoveryPassRender'] == 0
                assert report['recoveryCameraRejected'] == 3 and report['recoveryCameraRender'] == 0
                assert report['recoveryErrorEvents'] == 2 and report['recoveredStatusEvents'] == 2
                recovery = {name: compare(folder/f'run.{name}.rgba.bin', folder/'run.recovery.fresh-cpu.rgba.bin')
                            for name in ['recovery-pass', 'recovery-camera']}
                assert all(row['practicalFailures'] == 0 for row in recovery.values())
                report['recovery'] = recovery
            results[mode] = report
            save(out/'results.json', results)
        assert inputs() == sources and all(sha(Path(p)) == digest for p, digest in pins.items())
        gpu = [row for rows in [results['gpu']['comparisons']] for value in rows.values() for row in value.values()]
        status.update(complete=True, passed=True, phase='complete', inputsUnchanged=True,
                      scenarios=results['cpu']['names'],
                      cpuBitIdentical=True,
                      gpuMaximumAbsoluteError=max(row['maximumAbsoluteError'] for row in gpu),
                      gpuPracticalFailures=sum(row['practicalFailures'] for row in gpu),
                      gpuStrictDifferences=sum(row['strictDifferences'] for row in gpu),
                      recovery=results['gpu']['recovery'],
                      scope='Scene edits after a render (object tfm, object/camera/deformation motion, motion blur, '
                            'material value, light, mesh replacement, instancing, attributes, smooth flags) through the '
                            'C ABI versus a fresh CPU render of the edited scene; replay = the same edit sequence in a new '
                            'host; WebGPU device-error recovery after a rejected depth pass and orthographic camera.')
    except Exception as error:
        status.update(complete=True, passed=False, error=repr(error))
        raise
    finally:
        save(out/'status.json', status)
    print(json.dumps({k: v for k, v in status.items() if k != 'recovery'}), flush=True)


if __name__ == '__main__':
    main()
