#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Build and validate the maintained Linux native host and thin .NET consumers."""
import argparse
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

from build import REPO, ROOT, WORK, PACKAGE, REVISION
from device_gate import sha, save
from render_gate import inputs


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=WORK/'host-validation')
    parser.add_argument('--jobs', default='6')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    build = WORK/'cycles-build'
    binary = build/'bin'
    library = build/'lib/libcodebrix_cycles_host.so'
    sources = inputs()
    commands, results, comparisons = [], {}, []
    state = dict(complete=False, phase='build', rid='linux-x64')
    save(out/'status.json', state)
    environment = dict(os.environ)
    environment.pop('OCIO', None)
    environment['LD_LIBRARY_PATH'] = str(library.parent)
    pins = {}

    def call(command, name, timeout=300):
        state['phase'] = name
        save(out/'status.json', state)
        commands.append(dict(command=list(map(str, command)), timeoutSeconds=timeout))
        save(out/'commands.json', commands)
        with (out/(name+'.stdout.txt')).open('w') as stdout, (out/(name+'.stderr.txt')).open('w') as stderr:
            subprocess.run(command, cwd=REPO, env=environment, stdout=stdout, stderr=stderr,
                           check=True, timeout=timeout)
        return (out/(name+'.stdout.txt')).read_text()

    def report(command, name, timeout=300):
        rows = [json.loads(line) for line in call(command, name, timeout).splitlines() if line.strip()]
        assert rows and all(row['passed'] for row in rows)
        results[name] = rows
        return rows

    def compare(actual, reference, bitwise=False, strict=True):
        a_bytes, b_bytes = actual.read_bytes(), reference.read_bytes()
        a, b = struct.unpack('<3072f', a_bytes), struct.unpack('<3072f', b_bytes)
        assert all(math.isfinite(v) for v in a+b)
        errors = [abs(x-y) for x, y in zip(a, b)]
        row = dict(film=str(actual.relative_to(out)), reference=str(reference.relative_to(out)),
                   filmSha256=sha(actual), bitwiseEqual=a_bytes == b_bytes,
                   maximumAbsoluteError=max(errors),
                   practicalFailures=sum(e > 5e-5+2e-4*abs(y) for e, y in zip(errors, b)),
                   strictDifferences=sum(e > 1e-7+1e-7*abs(y) for e, y in zip(errors, b)))
        comparisons.append(row)
        # strict=False (the ABI light films only): the practical criterion is
        # required and strict differences are recorded in the row, as for the render gates.
        assert row['practicalFailures'] == 0 and (row['strictDifferences'] == 0 or not strict)
        if bitwise:
            assert row['bitwiseEqual'], str(actual)

    def destination(name):
        folder = out/name
        folder.mkdir(parents=True, exist_ok=False)
        return folder

    try:
        targets = ['codebrix_cycles_host', *['codebrix_host_'+n for n in
            ['cpu_test', 'gpu_test', 'reload_test', 'concurrency_test',
             'session_reference', 'session64_reference', 'image_reference',
             'abi_test', 'example_test']], 'codebrix_cycles_oracle']
        call([sys.executable, ROOT/'tools/build_cycles.py', '--jobs', args.jobs,
              *[v for target in targets for v in ('--target', target)]], 'build', 1800)
        expected = set(re.findall(r'\b(cbc_host_\w+)\(', (ROOT/'host/cycles_host.h').read_text()))
        exports = call(['nm', '-D', '--defined-only', library], 'exports')
        actual = {line.split()[-1].split('@')[0] for line in exports.splitlines()} - {'CBC_HOST_EXPERIMENT_1'}
        assert actual == expected and len(actual) == 48
        for kind in ['session', 'session64', 'image']:
            link_map = (build/f'codebrix_host_{kind}_reference.map').read_text()
            assert 'CMakeFiles/codebrix_host_reference_color.dir/src/util/colorspace.cpp.o' in link_map
            assert not re.search(r'libcycles_util\.a\([^)]*colorspace', link_map)
        runtime = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
        pins = {str(p):sha(p) for p in [library, runtime, *[binary/t for t in targets[1:]] ]}
        save(out/'invocation.json', dict(sourceSha256=sources, binarySha256=pins, commands=commands))

        cpu = destination('cpu')
        cpu64 = destination('cpu64')
        for folder, name in [(cpu, 'session'), (cpu64, 'session64')]:
            rows = report([binary/f'codebrix_host_{name}_reference', 'CPU', folder/'run'], name+'-reference')
            assert rows[0]['actualSession'] and rows[0]['renders'] == 3 and rows[0]['device'] == 'CPU'
        refs = destination('references')
        for variant in ['a', 'b', 'c']:
            folder = refs/variant/'output'
            rows = report([binary/'codebrix_host_image_reference', ROOT/'tests/host/configs'/f'{variant}.ocio', folder],
                          'image-reference-'+variant)
            assert rows[0]['originalNativeCpuSession'] and rows[0]['cases'] == 260
        reference_hashes = {str(p):sha(p) for folder in [cpu, cpu64, refs] for p in folder.rglob('*') if p.is_file()}

        for mode in ['cpu', 'cpu-exit']:
            folder = destination(mode+'-consumer')
            report([binary/'codebrix_host_cpu_test', folder/'run', *(['--exit-live'] if mode == 'cpu-exit' else [])], mode)
            for i in range(3):
                compare(folder/f'run.{i}.rgba.bin', cpu/f'run.{i}.rgba.bin', bitwise=True)
        reload = destination('reload')
        for i in range(6):
            (reload/f'round.{i}').mkdir()
        rows = report([binary/'codebrix_host_reload_test', refs, reload, '6'], 'reload')
        assert len(rows) == 7 and rows[-1]['films'] == 1560
        for i in range(6):
            for case in range(260):
                name = f'case.{case}.rgba.bin'
                compare(reload/f'round.{i}'/name, refs/chr(ord('a')+i%3)/'output'/name, bitwise=True)

        stress_input, stress_output = destination('stress-input'), destination('stress-output')
        for variant in ['a', 'b']:
            shutil.copy2(ROOT/'tests/host/configs'/f'{variant}.ocio', stress_input)
        shutil.copy2(refs/'a/output/input.5.bin', stress_input)
        rows = report([binary/'codebrix_host_concurrency_test', stress_input, stress_output], 'concurrency')
        for key, count in dict(concurrentHostLifetimes=192, destroyDuringRender=12, racingReaders=48,
                              guardedConfigurationRejections=256, hostLiveConfigurationRejections=192).items():
            assert rows[0][key] == count
        assert rows[0]['cancelledOutputUntouched']
        for name in ['guarded', 'after-invalid']:
            compare(stress_output/(name+'.rgba.bin'), refs/'a/output/case.177.rgba.bin', bitwise=True)

        for project in ['HostLifecycle', 'ImageModes', 'ColorReload']:
            call(['dotnet', 'build', REPO/'tools/hosttests'/project/(project+'.csproj'), '-c', 'Release',
                  '--configfile', REPO/'tools/hosttests/NuGet.Config'], 'managed-build-'+project)
        managed = {name:REPO/'tools/hosttests'/name/'bin/Release/net10.0'/(name+'.dll')
                   for name in ['HostLifecycle', 'ImageModes', 'ColorReload']}
        for path in managed.values():
            pins[str(path)] = sha(path)
            wrapper = path.parent/'CodeBrix.Graphics3D.Cycles.dll'
            pins[str(wrapper)] = sha(wrapper)
        report(['dotnet', managed['HostLifecycle'], cpu, destination('managed-lifecycle')], 'managed-lifecycle')
        managed_reload = destination('managed-reload')
        rows = report(['dotnet', managed['ColorReload'], managed['ImageModes'], refs, managed_reload], 'managed-reload')
        assert rows[-1]['sameManagedProcess'] and rows[-1]['films'] == 1560
        for i in range(6):
            for case in range(260):
                name = f'case.{case}.rgba.bin'
                compare(managed_reload/f'round.{i}'/name, refs/chr(ord('a')+i%3)/'output'/name, bitwise=True)

        for mode, count in [('gpu', 6), ('gpu-exit', 5)]:
            folder = destination(mode)
            rows = report([binary/'codebrix_host_gpu_test', folder/'run', *(['--exit-live'] if mode == 'gpu-exit' else [])], mode, 900)
            if mode == 'gpu':
                assert rows[0]['explicitWebGpu'] and rows[0]['repeatStrictCriterionPassed'] and rows[0]['repeatChecks'] == 3
            for i in range(count):
                reference = cpu64/'run.1.rgba.bin' if i == 3 else cpu/f'run.{i if i < 3 else 1}.rgba.bin'
                # run.3 (the 64-sample film against the session64 reference): the practical criterion is
                # required and strict differences are recorded, as for the ABI light films below. Its WebGPU
                # atomic accumulation order varies between runs of one binary, and with the fixed-baseline CPU
                # reference one component sits one float step from the strict bound (native/DEVICE.md,
                # "Merge wave 7").
                compare(folder/f'run.{i}.rgba.bin', reference, strict=i != 3)
        # Phase D ABI additions: every new export, malformed input, lifetime
        # and struct-size rejection, on the CPU reference and through CBC_HOST_GPU.
        abi = {}
        for mode in ['cpu', 'gpu']:
            folder = destination('abi-'+mode)
            row = report([binary/'codebrix_host_abi_test', folder/'run', mode], 'abi-'+mode, 900)[0]
            assert re.fullmatch(r'1\.0\.\d{8}\.\d+', row['libraryVersion']) and row['cyclesRevision'] == REVISION
            binary_caps = json.loads((folder/'run.capabilities-binary.json').read_text())
            host_caps = json.loads((folder/'run.capabilities-host.json').read_text())
            assert binary_caps['abiVersion'] == 1 and binary_caps['cyclesRevision'] == REVISION and 'host' not in binary_caps
            assert binary_caps['backends'] == dict(compiled=['cpu', 'webgpu'], gpu='webgpu', gpuFallbackToCpu=False)
            assert binary_caps['imageInput']['formats'] == ['float', 'float4', 'half', 'half4', 'byte', 'byte4', 'ushort', 'ushort4']
            assert host_caps['host']['backend'] == ('cpu' if mode == 'cpu' else 'webgpu')
            assert host_caps['host']['deviceReport']['backend'] == host_caps['host']['backend']
            assert row['lightTypes'] == 5 and row['cancelledByCallback'] and row['refusedReentry'] == row['progressEvents'] + row['statusEvents']
            if mode == 'cpu':
                assert all(row[k] for k in ['lightDeletionRestores', 'graphRebuildRestores', 'motionRemovalRestores', 'imageUpdateRestores'])
                assert row['litRenders'] == 4 and row['projections'] == 2 and row['errorEvents'] == 0
                assert all(row[k] == 0 for k in ['depthStatus', 'normalStatus', 'aovStatus', 'dataPassRender', 'lightPassRender',
                                                 'diffuseDirectStatus', 'backgroundStatus'])
                passes = json.loads((folder/'run.passes.json').read_text())['passes']
                assert [(p['name'], p['components'], p['written']) for p in passes] == [
                    ('Combined', 4, True), ('Depth', 1, True), ('Normal', 3, True), ('Mask', 1, True)]
            else:
                assert row['litRenders'] == 4 and row['errorEvents'] == 1 and row['renderAfterDeviceFailure'] == 0
            abi[mode] = folder
        for name in ['base', 'object-motion', 'deforming']:
            compare(abi['gpu']/f'run.{name}.rgba.bin', abi['cpu']/f'run.{name}.rgba.bin')
        # The four light films (point, spot, area and sun in one device lifetime):
        # the practical criterion is required and strict differences are recorded.
        # Sphere point and spot sampling differs from the CPU in the last bits (about
        # 125 and 22 strict differences), and the WebGPU film's atomic accumulation
        # order varies between runs (the area film had 0 or 1 strict difference).
        for name in ['light-pointlight', 'light-spotlight', 'light-arealight', 'light-sunlight']:
            compare(abi['gpu']/f'run.{name}.rgba.bin', abi['cpu']/f'run.{name}.rgba.bin', strict=False)
        for mode in ['cpu', 'gpu']:
            row = report([binary/'codebrix_host_example_test', mode], 'example-'+mode)[0]
            assert row['backend'] == (2 if mode == 'cpu' else 1) and row['progressEvents'] > 0
            assert row['depthStatus'] == (0 if mode == 'cpu' else 1)
        assert inputs() == sources
        assert all(sha(Path(p)) == digest for p, digest in {**pins, **reference_hashes}.items())
        save(out/'invocation.json', dict(sourceSha256=sources, binarySha256=pins,
                                        referenceSha256=reference_hashes, commands=commands))
        state.update(complete=True, passed=True, phase='complete', exportCount=48,
                     independentReferenceFilms=786, comparisons=len(comparisons), results=results,
                     maximumAbsoluteError=max(row['maximumAbsoluteError'] for row in comparisons),
                     practicalFailures=sum(row['practicalFailures'] for row in comparisons),
                     strictDifferences=sum(row['strictDifferences'] for row in comparisons))
        print('Maintained Linux C/.NET host, color reloads, concurrency and GPU lifecycle passed', flush=True)
    except Exception as error:
        state.update(complete=True, passed=False, error=str(error))
        raise
    finally:
        save(out/'comparisons.json', comparisons)
        save(out/'status.json', state)


if __name__ == '__main__':
    main()
