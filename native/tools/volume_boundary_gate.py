#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Finite homogeneous volumes: native state/density, closed-scene images and replay."""
import argparse
import itertools
import json
import math
import os
import shutil
import struct
import subprocess
from pathlib import Path
from build import REPO, WORK, PACKAGE
from device_gate import sha, save
from render_gate import inputs
import render_replay


def image_values(folder):
    meta = json.loads((folder/'run.capture.json').read_text())
    assert meta['format'] == 'native-command-stream-v5'
    assert meta['pipelineDispatches'][11] > 0
    stride = meta['filmStride']
    film = [v[0] for v in struct.iter_unpack('<f', (folder/'run.gpu.film.bin').read_bytes())]
    assert len(film) == meta['width']*meta['height']*stride
    rgb = [film[p*stride+c]/meta['samples'] for p in range(meta['width']*meta['height']) for c in range(3)]
    # The existing procedural material corpus includes signed closure/emission
    # weights. Preserve native signed radiance; the CPU comparison is decisive.
    assert all(math.isfinite(v) for v in rgb)
    assert max(rgb) > .1 and max(rgb)-min(rgb) > .1, 'Trivial volume image'
    assert sum(v > .001 for v in rgb) > len(rgb)//4, 'Insufficient visible radiance'
    return rgb


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve();out.mkdir(parents=True, exist_ok=False)
    sources = inputs();runtime = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    pins = {str(runtime):sha(runtime)}
    status = {'complete':False, 'phase':'prepare'}
    reports = {};images = {}
    save(out/'status.json', status)
    try:
        if not args.skip_build:
            command = ['python3', str(REPO/'native/tools/build_cycles.py'), '--jobs', '2']
            for name in ('volume_boundary', 'closed_volume_render', 'volume', 'camera'):
                command += ['--target', 'codebrix_cycles_'+name+'_gate']
            with (out/'build.log').open('w') as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        render_replay.build('linux-x64')
        binaries = {name:WORK/'cycles-build/bin'/('codebrix_cycles_'+name+'_gate') for name in ('volume_boundary', 'closed_volume_render', 'volume', 'camera')}
        for binary in [*binaries.values(), render_replay.BASE/'linux-x64/render_replay']:
            pins[str(binary)] = sha(binary)
        save(out/'invocation.json', {'productionSourceSha256':sources, 'fileSha256':pins})
        cases = [('camera-static', 'camera', {}),
                 ('camera-motion', 'camera', {}),
                 ('state', 'volume_boundary', {}),
                 ('compaction-guard', 'volume_boundary', {'CODEBRIX_VOLUME_COMPACTION_CASE':'1'}),
                 ('world-regression', 'volume', {}),
                 ('single', 'closed_volume_render', {'CODEBRIX_VOLUME_CLOSED_COUNT':'1'}),
                 ('overlap', 'closed_volume_render', {'CODEBRIX_VOLUME_CLOSED_COUNT':'4'}),
                 ('inside', 'closed_volume_render', {'CODEBRIX_VOLUME_CLOSED_COUNT':'4', 'CODEBRIX_VOLUME_CAMERA_INSIDE':'1'})]
        for name, binary, settings in cases:
            status['phase'] = name;save(out/'status.json', status)
            folder = out/name
            env = dict(os.environ)
            for key in ('CODEBRIX_VOLUME_ABSORPTION_ONLY', 'CODEBRIX_VOLUME_G', 'CODEBRIX_VOLUME_TRACE', 'CODEBRIX_VOLUME_CLOSED_COUNT', 'CODEBRIX_VOLUME_CAMERA_INSIDE', 'CODEBRIX_VOLUME_STACK_TRACE', 'CODEBRIX_VOLUME_COMPACTION_CASE'):
                env.pop(key, None)
            env.update(settings)
            command = [str(binaries[binary]), str(folder/'run')]+(['--motion'] if name == 'camera-motion' else [])
            folder.mkdir()
            save(folder/'invocation.json', {'command':command, 'environment':settings, 'timeoutSeconds':900})
            with (folder/'stdout.txt').open('w') as stdout, (folder/'stderr.txt').open('w') as stderr:
                subprocess.run(command, env=env, stdout=stdout, stderr=stderr, check=True, timeout=900)
            report = json.loads((folder/'stdout.txt').read_text())
            assert report['passed'] and report['cyclesAllocationBytesAfterCleanup'] == 0
            assert Path((folder/'run.library.txt').read_text().strip()).resolve() == runtime.resolve()
            if name.startswith('camera-'):
                assert report['originalCpuCameraOracle'] and report['cases'] == 24
                assert report['invalidCasesRejected'] == (19 if name == 'camera-motion' else 12)
            elif name == 'state':
                assert report['originalCpuVolumeBoundaries'] and report['cases'] == 24
                assert report['densityInputs'] == 800 and report['malformedCasesRejected'] == 6
                assert all(report[k] > 0 for k in ('enteredVolumes','exitedVolumes','initializedInside','initializedOutside','initializedOverlap','volumeOnlyBoundaries','glossyReflections','glossyTransmissions'))
            elif name == 'compaction-guard':
                assert report['malformedCasesRejected'] == 1 and report['cases'] == 0
            elif name == 'world-regression':
                assert report['originalCpuVolumeIntegrator'] and report['cases'] == 24 and report['invalidCasesRejected'] == 18
            else:
                assert report['independentCpuImage'] and report['repeatBitwiseIdentical']
                assert report['volumeTransmitEvents'] > 0 and report['volumeScatterEvents'] > 0
                assert report['maxVolumeBoundaryBounce'] >= 2
                assert bool(report['volumeStackInitializations']) == (name == 'inside')
                images[name] = image_values(folder)
                render_replay.run('linux-x64', folder, out/'replay'/name, timeout=900)
            reports[name] = report;save(out/'results.json', reports)
        differences = {}
        for a, b in itertools.combinations(images, 2):
            errors = [abs(x-y) for x, y in zip(images[a], images[b], strict=True)]
            assert max(errors) > .01 and sum(e > .001 for e in errors) > 100, 'Volume controls had no measurable image effect'
            differences[a+'-'+b] = {'maximumAbsoluteDifference':max(errors), 'meanAbsoluteDifference':sum(errors)/len(errors), 'componentsAboveOneMilliunit':sum(e > .001 for e in errors)}
        save(out/'image-controls.json', differences)
        status.update(complete=True, passed=True, phase='complete', scope='Static closed homogeneous triangle volumes: native density preparation, entry/exit, stack initialization and aggregation, camera inside/outside, nested and overlapping media. Volume emission, NEE/MIS, shadow attenuation, moving boundaries, heterogeneous volumes, mixed-anisotropy duplicate compaction and volume light passes remain unsupported.')
    except Exception as error:
        status.update(complete=True, passed=False, error=repr(error))
        raise
    finally:
        status['inputsUnchanged'] = sources == inputs() and all(sha(Path(name)) == digest for name, digest in pins.items())
        if not status['inputsUnchanged']:
            status['passed'] = False
        save(out/'status.json', status)
    assert status['inputsUnchanged'], 'Inputs changed during volume validation'
    print(json.dumps(status), flush=True)


if __name__ == '__main__':
    main()
