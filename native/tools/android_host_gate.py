#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Validate maintained Android host binaries and run pure C consumers over adb.

CPU image comparisons use the established practical criterion and retain strict
diagnostics. GPU and CPU lifecycle films must also meet the strict criterion, except
the GPU 64-sample film (run.3), whose atomic accumulation order varies between runs:
practical required, strict recorded.
This does not replace the original bitwise Session gate or validate an APK.
"""
import argparse
import concurrent.futures
import json
import math
import re
import shlex
import shutil
import struct
import subprocess
from pathlib import Path

from build import REPO, ROOT, WORK, PACKAGE
from build_cycles_android import dependencies, HOST_TARGETS, GATE_TARGETS
from android_toolchain import AndroidToolchain, TARGETS
from device_gate import sha, save
from render_gate import inputs

PLATFORM = {'libc.so', 'libm.so', 'libdl.so', 'liblog.so', 'libandroid.so',
            'libvulkan.so', 'libEGL.so', 'libGLESv2.so'}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def inspect(rid, folder):
    work = WORK/'android'/rid/'host'
    build = work/'build'
    status = json.loads((work/'status.json').read_text())
    pin = json.loads((work/'invocation.json').read_text())
    require(status['complete'] and status['passed'], rid+': native build did not pass')
    for name, digest in pin['sourceSha256'].items():
        require(sha(REPO/name) == digest, 'Build input changed: '+name)
    for name, digest in pin['sourceCopySha256'].items():
        require(sha(work/'cycles-source'/name) == digest, 'Copied Cycles source changed: '+name)
    for name, digest in pin['overlaySha256'].items():
        require(sha(work/'overlay'/name) == digest, 'Generated overlay changed: '+name)
    toolchain = AndroidToolchain(rid)
    require(toolchain.identity() == pin['toolchain'], 'Android toolchain changed')
    prefix = dependencies.prefix(rid)
    require(sha(prefix/'codebrix-android-build.json') == pin['dependencyMarkerSha256'],
            'Android dependency build changed')
    runtime = PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so'
    require(sha(runtime) == pin['runtimeSha256'], 'Shipped WebGPU runtime changed')
    artifacts = [build/name for name in status['artifacts']]
    require(all(sha(build/name) == digest for name, digest in status['artifacts'].items()),
            'A built Android artifact changed')
    require(len(artifacts) == len(HOST_TARGETS)+len(GATE_TARGETS), 'Build the default full set of native targets first')
    stage = {p.name:p for p in (prefix/'lib').glob('*.so*') if p.is_file()}
    stage.update({p.name:p for p in [runtime, toolchain.runtime, *artifacts]})
    rows = []
    for path in sorted({p.resolve() for p in stage.values()}):
        data = path.read_bytes()
        require(data[:6] == b'\x7fELF\x02\x01', 'Expected ELF64 little endian: '+str(path))
        require(struct.unpack_from('<H', data, 18)[0] == (183 if rid == 'android-arm64' else 62),
                'Wrong ELF architecture: '+str(path))
        start = struct.unpack_from('<Q', data, 32)[0]
        size, count = struct.unpack_from('<HH', data, 54)
        loads = []
        for i in range(count):
            kind, flags, offset, address, physical, file_size, memory_size, alignment = struct.unpack_from(
                '<IIQQQQQQ', data, start+size*i)
            if kind == 1:
                require(alignment >= 16384 and offset % 16384 == address % 16384,
                        'Insufficient segment alignment: '+str(path))
                loads.append(dict(alignment=alignment, offset=offset, address=address))
        require(bool(loads), 'Missing ELF load segments: '+str(path))
        command = [str(toolchain.bin/'llvm-readelf'), '-d', '-n', '--version-info', str(path)]
        report = subprocess.check_output(command, text=True)
        needed = re.findall(r'\(NEEDED\).*?\[(.*?)\]', report)
        require(not set(needed)-set(stage)-PLATFORM, 'Unresolved Android dependencies: '+str(path))
        require('GLIBC_' not in report, 'Host glibc dependency in Android ELF')
        rows.append(dict(path=str(path), sha256=sha(path), needed=needed, loadSegments=loads,
                         inspectionCommand=command, dynamicAndVersionReport=report))
    library = build/'lib/libcodebrix_cycles_host.so'
    exports = subprocess.check_output([str(toolchain.bin/'llvm-nm'), '-D', '--defined-only', str(library)], text=True)
    actual = {line.split()[-1].split('@')[0] for line in exports.splitlines()}-{'CBC_HOST_EXPERIMENT_1'}
    expected = set(re.findall(r'\b(cbc_host_\w+)\(', (ROOT/'host/cycles_host.h').read_text()))
    require(actual == expected and len(actual) == 48, 'Unexpected public C exports')
    compiled, headers = [], {}
    for row in json.loads((build/'compile_commands.json').read_text()):
        path = Path(row['file']).resolve()
        require(path.is_relative_to(REPO), 'External native implementation: '+str(path))
        require('/third_party/atomic/' not in str(path), 'Excluded atomic implementation')
        compiled.append(dict(path=str(path), sha256=sha(path), command=row['command']))
    for file in build.rglob('*.o.d'):
        for name in file.read_text().replace('\\\n', ' ').split(':', 1)[-1].split():
            path = Path(name)
            if not path.is_absolute():
                path = build/path
            if not path.is_file():
                continue
            path = path.resolve()
            require(any(path.is_relative_to(p) for p in [REPO, toolchain.root, PACKAGE]),
                    'Unreviewed external Android header: '+str(path))
            require('/third_party/atomic/' not in str(path), 'Excluded atomic header')
            headers[str(path)] = sha(path)
    archives = {str(p):sha(p) for p in build.rglob('*.a')}
    save(folder/'binary-review.json', dict(passed=True, rid=rid, exports=sorted(actual),
        files=rows, compiledSources=compiled, compilerHeaders=headers, staticArchives=archives,
        buildInvocationSha256=sha(work/'invocation.json'), buildStatusSha256=sha(work/'status.json'),
        scope='Native compiler/header/archive inputs and declared ELF closure; not APK, release or 16-KiB-device validation'))
    consumers = {'codebrix_host_'+name+'_test' for name in ['cpu', 'gpu', 'reload', 'concurrency']}
    return {name:path for name, path in stage.items() if '.so' in name or name in consumers}


def compare(path, reference, strict_required):
    actual, expected = path.read_bytes(), reference.read_bytes()
    a, b = struct.unpack('<3072f', actual), struct.unpack('<3072f', expected)
    require(all(math.isfinite(v) for v in a+b), 'Nonfinite film: '+str(path))
    errors = [abs(x-y) for x, y in zip(a, b)]
    return dict(film=str(path), reference=str(reference), filmSha256=sha(path),
        bitwiseEqual=actual == expected, maximumAbsoluteError=max(errors), strictRequired=strict_required,
        practicalFailures=sum(e > 5e-5+2e-4*abs(y) for e, y in zip(errors, b)),
        strictDifferences=sum(e > 1e-7+1e-7*abs(y) for e, y in zip(errors, b)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--references', type=Path, required=True)
    parser.add_argument('--device', action='append', required=True, metavar='LABEL,RID,SERIAL')
    parser.add_argument('--adb', default=str(Path.home()/'Android/Sdk/platform-tools/adb'))
    parser.add_argument('--gpu-timeout', type=int, default=2400)
    args = parser.parse_args()
    devices = [tuple(d.split(',')) for d in args.device]
    require(all(len(d) == 3 and re.fullmatch('[a-z0-9-]+', d[0]) and d[1] in TARGETS for d in devices),
            'Expected LABEL,RID,SERIAL for each device')
    require(len({d[0] for d in devices}) == len(devices) and len({d[2] for d in devices}) == len(devices),
            'Duplicate label or device serial')
    out, refs = args.output.resolve(), args.references.resolve()
    require(re.fullmatch('[a-z0-9-]+', out.name), 'Output directory name must use lower-case letters, digits or hyphens')
    out.mkdir(parents=True, exist_ok=False)
    sources, pins, stages = inputs(), {}, {}
    state = dict(complete=False, phase='binary-review')
    save(out/'status.json', state)
    try:
        ref_status = json.loads((refs/'status.json').read_text())
        require(ref_status['passed'] and ref_status['independentReferenceFilms'] == 786,
                'Expected a completed independent maintained Linux reference run')
        reference_pin = json.loads((refs/'invocation.json').read_text())
        pins.update(reference_pin['referenceSha256'])
        for name in ['native/tests/host/session_reference.cpp', 'native/tests/host/image_reference.cpp',
                     'native/tests/host/image_inputs.h']:
            require(sha(REPO/name) == reference_pin['sourceSha256'][name], 'Reference fixture changed: '+name)
        for rid in sorted({d[1] for d in devices}):
            folder = out/rid
            folder.mkdir()
            stages[rid] = inspect(rid, folder)
            pins.update({str(p):sha(p) for p in stages[rid].values()})
        require(all(sha(Path(p)) == digest for p, digest in pins.items()), 'Reference/binary input changed')
        save(out/'invocation.json', dict(sourceSha256=sources, inputSha256=pins, devices=devices,
            practicalCriterion='5e-5 + 2e-4*abs(reference)', strictCriterion='1e-7 + 1e-7*abs(reference)',
            scope=__doc__))

        def run(label, rid, serial):
            folder = out/label
            folder.mkdir()
            commands, comparisons, results = [], [], {}
            current = dict(complete=False, phase='stage', rid=rid, serial=serial)
            remote = '/data/local/tmp/codebrix-maintained-host-'+out.name+'-'+label

            def call(arguments, name, timeout=120, check=True):
                command = [args.adb, '-s', serial, *arguments]
                commands.append(dict(command=command, timeoutSeconds=timeout))
                save(folder/'commands.json', commands)
                with (folder/(name+'.stdout.txt')).open('w') as stdout, (folder/(name+'.stderr.txt')).open('w') as stderr:
                    result = subprocess.run(command, stdout=stdout, stderr=stderr, timeout=timeout)
                if check:
                    result.check_returncode()
                return result.returncode, (folder/(name+'.stdout.txt')).read_text()

            def shell(arguments, name, **kw):
                return call(['shell', shlex.join(arguments)], name, **kw)

            try:
                save(folder/'status.json', current)
                processes = shell(['ps', '-A', '-o', 'PID,ARGS'], 'prior-processes')[1]
                require(not any('/data/local/tmp/codebrix-' in line for line in processes.splitlines()),
                        'Earlier CodeBrix device workload still running')
                metadata = {key:shell(command, key)[1].strip() for key, command in {
                    'api':['getprop', 'ro.build.version.sdk'], 'abi':['getprop', 'ro.product.cpu.abi'],
                    'pageSize':['getconf', 'PAGESIZE'], 'fingerprint':['getprop', 'ro.build.fingerprint']}.items()}
                require(int(metadata['api']) >= 33 and metadata['abi'] == TARGETS[rid][0], 'Device target mismatch')
                current['device'] = metadata
                stage = folder/'stage'
                (stage/'lib').mkdir(parents=True)
                (stage/'bin').mkdir()
                staged = {}
                for name, path in stages[rid].items():
                    destination = stage/('lib' if '.so' in name else 'bin')/name
                    shutil.copy2(path, destination)
                    staged[str(destination.relative_to(stage))] = sha(destination)
                for variant in ['a', 'b', 'c']:
                    target = stage/'references'/variant/'output'
                    target.mkdir(parents=True)
                    for path in (refs/'references'/variant/'output').iterdir():
                        if path.name == 'config.ocio' or path.name.startswith('input.'):
                            shutil.copy2(path, target/path.name)
                            staged[str((target/path.name).relative_to(stage))] = sha(path)
                (stage/'stress-input').mkdir()
                for name, path in {'a.ocio':ROOT/'tests/host/configs/a.ocio',
                                   'b.ocio':ROOT/'tests/host/configs/b.ocio',
                                   'input.5.bin':refs/'references/a/output/input.5.bin'}.items():
                    shutil.copy2(path, stage/'stress-input'/name)
                    staged['stress-input/'+name] = sha(path)
                for name in ['cpu', 'cpu-exit', 'gpu', 'gpu-exit', 'stress', *['reload/round.'+str(i) for i in range(6)]]:
                    (stage/'output'/name).mkdir(parents=True)
                shell(['test', '!', '-e', remote], 'unused-remote')
                call(['push', str(stage), remote], 'push', timeout=600)
                # adb push omits empty directories; create output folders explicitly.
                shell(['mkdir', '-p', *[remote+'/'+str(p.relative_to(stage))
                      for p in (stage/'output').rglob('*') if p.is_dir()]], 'output-directories')
                expected = {remote+'/'+name:digest for name, digest in staged.items()}
                hashes = shell(['sha256sum', *expected], 'staged-hashes')[1]
                require({line.split()[1]:line.split()[0] for line in hashes.splitlines()} == expected,
                        'Device payload hashes do not match')
                shell(['chmod', '755', *[remote+'/'+name for name in staged if name.startswith('bin/')]], 'chmod')
                save(folder/'staged.json', dict(remote=remote, sha256=expected))
                modes = [('cpu', 'cpu_test', [remote+'/output/cpu/run'], 300),
                         ('cpu-exit', 'cpu_test', [remote+'/output/cpu-exit/run', '--exit-live'], 300),
                         ('reload', 'reload_test', [remote+'/references', remote+'/output/reload', '6'], 1800),
                         ('stress', 'concurrency_test', [remote+'/stress-input', remote+'/output/stress'], 600),
                         ('gpu', 'gpu_test', [remote+'/output/gpu/run'], args.gpu_timeout),
                         ('gpu-exit', 'gpu_test', [remote+'/output/gpu-exit/run', '--exit-live'], args.gpu_timeout)]
                for mode, executable, arguments, timeout in modes:
                    current['phase'] = mode
                    save(folder/'status.json', current)
                    program = remote+'/bin/codebrix_host_'+executable
                    command = 'echo $$ > '+shlex.quote(remote+'/active.pid')+'; exec '+shlex.join(
                        ['env', '-u', 'OCIO', 'LD_LIBRARY_PATH='+remote+'/lib', program, *arguments])
                    try:
                        code, stdout = call(['shell', command], mode, timeout=timeout, check=False)
                    except subprocess.TimeoutExpired:
                        # Only terminate the exact still-running process from this invocation.
                        pid = shell(['cat', remote+'/active.pid'], mode+'-timeout-pid')[1].strip()
                        if pid.isdecimal():
                            # Android ps may show only the executable basename.
                            code, executable = shell(['readlink', '/proc/'+pid+'/exe'],
                                mode+'-timeout-executable', check=False)
                            if code == 0 and executable.strip() == program:
                                shell(['kill', '-KILL', pid], mode+'-timeout-cleanup')
                        raise
                    current[mode+'Returncode'] = code
                    save(folder/'status.json', current)
                    call(['pull', remote+'/output/'+mode, str(folder)], mode+'-pull', timeout=300, check=code == 0)
                    if mode in ['cpu', 'cpu-exit', 'gpu', 'gpu-exit']:
                        count = 3 if mode.startswith('cpu') else (6 if mode == 'gpu' else 5)
                        # GPU run.3 (64 samples, session64 reference): practical required, strict recorded, as
                        # in host_gate.py (atomic accumulation order varies between runs of one binary).
                        pairs = [(folder/mode/f'run.{i}.rgba.bin',
                            refs/'cpu64/run.1.rgba.bin' if i == 3 else refs/'cpu'/f'run.{i if i < 3 else 1}.rgba.bin',
                            not (mode.startswith('gpu') and i == 3))
                            for i in range(count)]
                    elif mode == 'reload':
                        pairs = [(folder/'reload'/f'round.{i}/case.{j}.rgba.bin',
                            refs/'references'/chr(ord('a')+i%3)/'output'/f'case.{j}.rgba.bin', False)
                            for i in range(6) for j in range(260)]
                    else:
                        pairs = [(folder/'stress'/(name+'.rgba.bin'), refs/'references/a/output/case.177.rgba.bin', False)
                                 for name in ['guarded', 'after-invalid']]
                    completed = [compare(p, r, strict) for p, r, strict in pairs if p.exists()]
                    comparisons.extend(completed)
                    save(folder/'comparisons.json', comparisons)
                    require(code == 0 and len(completed) == len(pairs), mode+': consumer failed or film missing')
                    reports = [json.loads(line) for line in stdout.splitlines() if line.strip()]
                    require(bool(reports) and all(row['passed'] for row in reports), mode+': failed report')
                    if mode == 'reload':
                        require(reports[-1]['films'] == 1560 and len(reports) == 7, 'Incomplete reload coverage')
                    if mode == 'stress':
                        require(all(reports[0][key] == value for key, value in
                            dict(concurrentHostLifetimes=192, destroyDuringRender=12, racingReaders=48,
                                 guardedConfigurationRejections=256, hostLiveConfigurationRejections=192).items()),
                            'Incomplete concurrency coverage')
                    if mode == 'gpu':
                        require(reports[0]['explicitWebGpu'] and reports[0]['repeatStrictCriterionPassed']
                                and reports[0]['repeatChecks'] == 3, 'Incomplete GPU lifecycle coverage')
                    results[mode] = reports
                    save(folder/'results.json', results)
                practical = sum(row['practicalFailures'] for row in comparisons)
                strict = sum(row['strictDifferences'] for row in comparisons if row['strictRequired'])
                current.update(complete=True, phase='complete', passed=practical == 0 and strict == 0,
                    films=len(comparisons), practicalFailures=practical, requiredStrictDifferences=strict,
                    imageStrictDiagnostics=sum(row['strictDifferences'] for row in comparisons if not row['strictRequired']),
                    maximumAbsoluteError=max(row['maximumAbsoluteError'] for row in comparisons))
                print(label+': '+json.dumps(current), flush=True)
                return current['passed']
            except Exception as error:
                current.update(complete=True, passed=False, error=repr(error))
                print(label+': '+repr(error), flush=True)
                return False
            finally:
                save(folder/'status.json', current)

        state['phase'] = 'devices'
        save(out/'status.json', state)
        with concurrent.futures.ThreadPoolExecutor(max_workers=len(devices)) as pool:
            futures = [pool.submit(run, *device) for device in devices]
            passed = [f.result() for f in futures]
        require(inputs() == sources, 'Production inputs changed during validation')
        require(all(sha(Path(p)) == digest for p, digest in pins.items()), 'Binary/reference inputs changed during validation')
        state.update(complete=True, passed=all(passed), phase='complete', inputsUnchanged=True)
    except Exception as error:
        state.update(complete=True, passed=False, error=repr(error))
        raise
    finally:
        save(out/'status.json', state)
    return 0 if state['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
