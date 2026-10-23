#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Homogeneous world volume state, image controls and native-command replay proof."""
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
    assert meta['format'] in ('native-command-stream-v5', 'native-command-stream-v6', 'native-command-stream-v7', 'native-command-stream-v8')
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
    parser.add_argument('--reuse-from', type=Path, help='Reuse completed cases with identical native source, binary and runtime pins')
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
            for name in ('volume', 'volume_render'):
                command += ['--target', 'codebrix_cycles_'+name+'_gate']
            with (out/'build.log').open('w') as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
        render_replay.build('linux-x64')
        binaries = {name:WORK/'cycles-build/bin'/('codebrix_cycles_'+name+'_gate') for name in ('volume', 'volume_render')}
        for binary in [*binaries.values(), render_replay.BASE/'linux-x64/render_replay']:
            pins[str(binary)] = sha(binary)
        save(out/'invocation.json', {'productionSourceSha256':sources, 'fileSha256':pins})
        reuse = args.reuse_from.resolve() if args.reuse_from else None
        if reuse:
            prior = json.loads((reuse/'invocation.json').read_text())
            assert json.loads((reuse/'status.json').read_text())['inputsUnchanged']
            changed = {name for name in sources.keys() | prior['productionSourceSha256'].keys() if sources.get(name) != prior['productionSourceSha256'].get(name)}
            assert changed <= {'native/tools/volume_gate.py'}, 'Native source changed since reusable case'
            assert pins == prior['fileSha256'], 'Binary/runtime changed since reusable case'
        cases = [('state', 'volume', {}),
                 ('absorption', 'volume_render', {'CODEBRIX_VOLUME_ABSORPTION_ONLY':'1'}),
                 ('isotropic', 'volume_render', {'CODEBRIX_VOLUME_G':'0'}),
                 ('forward', 'volume_render', {'CODEBRIX_VOLUME_G':'.45'}),
                 ('backward', 'volume_render', {'CODEBRIX_VOLUME_G':'-.65'})]
        for name, binary, settings in cases:
            status['phase'] = name;save(out/'status.json', status)
            folder = out/name
            env = dict(os.environ)
            for key in ('CODEBRIX_VOLUME_ABSORPTION_ONLY', 'CODEBRIX_VOLUME_G', 'CODEBRIX_VOLUME_TRACE'):
                env.pop(key, None)
            env.update(settings)
            command = [str(binaries[binary]), str(folder/'run')]
            reusable = reuse and (reuse/name/'stdout.txt').exists() and (reuse/name/'stdout.txt').stat().st_size
            if reusable:
                prior_report = json.loads((reuse/name/'stdout.txt').read_text())
                assert prior_report['passed']
                assert json.loads((reuse/name/'invocation.json').read_text())['environment'] == settings
                shutil.copytree(reuse/name, folder)
                save(folder/'reuse.json', {'origin':str(reuse/name), 'originalInvocationSha256':sha(reuse/'invocation.json'), 'identicalNativeInputs':True})
            else:
                folder.mkdir()
                save(folder/'invocation.json', {'command':command, 'environment':settings, 'timeoutSeconds':900})
                with (folder/'stdout.txt').open('w') as stdout, (folder/'stderr.txt').open('w') as stderr:
                    subprocess.run(command, env=env, stdout=stdout, stderr=stderr, check=True, timeout=900)
            report = json.loads((folder/'stdout.txt').read_text())
            assert report['passed'] and report['cyclesAllocationBytesAfterCleanup'] == 0
            assert Path((folder/'run.library.txt').read_text().strip()).resolve() == runtime.resolve()
            if name == 'state':
                assert report['originalCpuVolumeIntegrator'] and report['cases'] == 24 and report['invalidCasesRejected'] == 18
                assert min(report[k] for k in ('scatterEvents', 'transmitEvents', 'terminatedEvents')) > 0
            else:
                assert report['independentCpuImage'] and report['repeatBitwiseIdentical']
                assert report['volumeTransmitEvents'] > 0
                assert (report['volumeScatterEvents'] == 0) == (name == 'absorption')
                images[name] = image_values(folder)
                render_replay.run('linux-x64', folder, out/'replay'/name, timeout=900)
            reports[name] = report;save(out/'results.json', reports)
        differences = {}
        for a, b in itertools.combinations(images, 2):
            errors = [abs(x-y) for x, y in zip(images[a], images[b], strict=True)]
            assert max(errors) > .01 and sum(e > .001 for e in errors) > 100, 'Volume controls had no measurable image effect'
            differences[a+'-'+b] = {'maximumAbsoluteDifference':max(errors), 'meanAbsoluteDifference':sum(errors)/len(errors), 'componentsAboveOneMilliunit':sum(e > .001 for e in errors)}
        save(out/'image-controls.json', differences)
        status.update(complete=True, passed=True, phase='complete', scope='Homogeneous world absorption and native Henyey-Greenstein scattering. Object boundaries are validated separately by volume_boundary_gate.py. Volume emission, NEE/MIS, shadow attenuation, heterogeneous volumes and volume light passes remain unsupported.')
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
