#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Correctly rounded validation references for Android device gates (native/DEVICE.md,
"Validation reference policy").

export   Runs Linux x64 (glibc) gate binaries with CODEBRIX_CYCLES_REFERENCE_EXPORT so each
         independent CPU reference (sampler arrays, whole CPU films) is written beside the
         run, and records provenance: gate binary SHA-256, glibc version, source pins,
         every reference file's SHA-256 and the set digest ("oracle hash"). The Linux run
         itself must pass (it is also the Linux gate result for those binaries).
scene-reference
         The authoritative flow for device jobs. A device pass of the gate wrote the scene
         tables its own host code prepared (CODEBRIX_CYCLES_SCENE_DUMP); this runs the Linux
         x64 gate binary on exactly those tables (CODEBRIX_CYCLES_SCENE_LOAD, CPU only) and
         exports the correctly rounded reference for them, so the device GPU is judged on its
         own inputs. It also dumps the scene Linux prepares itself and records, per table, how
         the device-prepared inputs differ (diagnostic: the device C library and arithmetic
         reach the GPU through scene preparation).
compare  Recomputes, on the host, a device job's comparisons from the files the job
         pulled back: the device GPU against the imported glibc reference (authoritative,
         must agree with the device's own verdict), the device's local CPU reference
         against the glibc reference (diagnostic: the device C library's deviation) and
         the device GPU against its local CPU reference (diagnostic: the comparison the
         gate used before this policy).
stage-judge
         render_gate's lockstep stages in the device-scene flow (stage-state exchange): the
         device run records every stage's GPU input and output state (PREFIX.stages.bin); this
         runs the Linux x64 gate binary on the device's dumped scene with
         CODEBRIX_CYCLES_STAGE_JUDGE (CPU only), which evaluates the glibc CPU oracle on each
         recorded input and compares the recorded GPU output with the same criteria as the
         device's local lockstep check. Must pass for the device job to pass.

Usage:
  android_reference.py export --bin DIR --output DIR --job LABEL=GATE[:ARG,...][@ENV=VALUE;...] ...
  android_reference.py scene-reference --bin DIR --scenes DIR --output DIR --label LABEL --gate GATE [--arg A ...]
  android_reference.py compare --kind samples|lighting|render --run DIR --reference DIR --output FILE
  android_reference.py stage-judge --bin DIR --scenes DIR --stages FILE --gate GATE [--arg A ...] --output FILE
"""
import argparse
import array
import datetime
import hashlib
import json
import math
import os
import platform
import struct
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
PRACTICAL = '5e-5 + 2e-4*abs(reference)'
STRICT = '1e-7 + 1e-7*abs(reference)'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def floats(path):
    values = array.array('f')
    values.frombytes(Path(path).read_bytes())
    return values


def ordered(value):
    bits = struct.unpack('<i', struct.pack('<f', value))[0]
    return -2**31-bits if bits < 0 else bits


def compare_arrays(actual, reference, mask=None):
    """Practical/strict comparison of ACTUAL against REFERENCE (mask: indices to compare)."""
    if len(actual) != len(reference):
        raise RuntimeError('Length differs: %d versus %d' % (len(actual), len(reference)))
    indices = range(len(actual)) if mask is None else mask
    count = differing = practical = strict = nonfinite = max_ulps = 0
    maximum = ratio = 0.0
    first = None
    for i in indices:
        a, e = actual[i], reference[i]
        count += 1
        if struct.pack('<f', a) == struct.pack('<f', e):
            continue
        differing += 1
        if not (math.isfinite(a) and math.isfinite(e)):
            nonfinite += 1
            first = first or dict(index=i, actual=a, reference=e)
            continue
        error = abs(a-e)
        bound = 5e-5+2e-4*abs(e)
        if error > bound:
            practical += 1
            first = first or dict(index=i, actual=a, reference=e)
        strict += error > 1e-7+1e-7*abs(e)
        maximum = max(maximum, error)
        ratio = max(ratio, error/bound)
        max_ulps = max(max_ulps, abs(ordered(a)-ordered(e)))
    return dict(compared=count, bitwiseDiffering=differing, nonFinite=nonfinite, practicalFailures=practical,
                strictDifferences=strict, maximumAbsoluteError=maximum, maximumPracticalRatio=ratio,
                maximumUlps=max_ulps, firstPracticalFailure=first,
                passed=practical == 0 and nonfinite == 0)


def glibc_version():
    try:
        return os.confstr('CS_GNU_LIBC_VERSION')
    except (ValueError, OSError):
        return None


def export(args):
    sys.path.insert(0, str(HERE))
    from render_gate import inputs
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    sources = inputs()
    libm = None
    if args.libm_record:
        record = json.loads(args.libm_record.read_text())
        libm = dict(path=str(args.libm_record), sha256=sha(args.libm_record), libc=record['libc'],
                    libcVersion=record['libcVersion'],
                    correctlyRounded={r['function']: r['differFromCorrectlyRounded'] == 0 for r in record['functions']})
    jobs = {}
    for spec in args.job:
        label, rest = spec.split('=', 1)
        rest, _, env_text = rest.partition('@')
        gate, _, extra = rest.partition(':')
        env = dict(item.split('=', 1) for item in env_text.split(';') if item)
        binary = args.bin/('codebrix_cycles_'+gate+'_gate')
        folder = out/'runs'/label
        folder.mkdir(parents=True)
        references = out/'references'/label
        command = [str(binary), str(folder/'run'), *[x for x in extra.split(',') if x]]
        environment = {**os.environ, **env, 'CODEBRIX_CYCLES_REFERENCE_EXPORT': str(references)}
        environment.pop('CODEBRIX_CYCLES_REFERENCE_IMPORT', None)
        started = time.time()
        with (folder/'stdout.txt').open('w') as stdout, (folder/'stderr.txt').open('w') as stderr:
            try:
                code = subprocess.run(command, stdout=stdout, stderr=stderr, env=environment,
                                      timeout=args.budget).returncode
            except subprocess.TimeoutExpired:
                code = None
        report = None
        try:
            report = json.loads((folder/'stdout.txt').read_text())
        except ValueError:
            pass
        files = {p.name: sha(p) for p in sorted(references.glob('*.f32'))} if references.exists() else {}
        jobs[label] = dict(gate=gate, command=command, env=env, exit=code, seconds=round(time.time()-started, 1),
                           passed=code == 0 and bool(report and report.get('passed')), report=report,
                           gateBinarySha256=sha(binary), referenceSha256=files)
        print('%s exit=%s passed=%s references=%d %.0fs' % (label, code, jobs[label]['passed'], len(files),
                                                           jobs[label]['seconds']), flush=True)
    digest = hashlib.sha256(json.dumps({k: v['referenceSha256'] for k, v in sorted(jobs.items())},
                                       sort_keys=True).encode()).hexdigest()
    provenance = dict(created=datetime.datetime.now().isoformat(timespec='seconds'), role='authoritative',
                      policy='native/DEVICE.md "Validation reference policy"',
                      platform=dict(system=platform.system(), machine=platform.machine(), kernel=platform.release(),
                                    libc=glibc_version()),
                      linuxBuild=dict(binDirectory=str(args.bin),
                                      buildRecordSha256=sha(args.build_record) if args.build_record else None),
                      sourceSha256=sources, libmRecord=libm, oracleSha256=digest, jobs=jobs,
                      passed=all(j['passed'] for j in jobs.values()))
    (out/'provenance.json').write_text(json.dumps(provenance, indent=2)+'\n')
    print('oracleSha256', digest, 'passed', provenance['passed'])
    return 0 if provenance['passed'] else 1


def scene_tables(path):
    """Parse a reference_policy.h scene exchange file into {name: (element_size, bytes)}."""
    data = Path(path).read_bytes()
    if data[:9] != b'CBCSCENE1':
        raise RuntimeError('Not a scene exchange file: '+str(path))
    at, tables = 9, {}
    while at < len(data):
        length, element, count = struct.unpack_from('<QQQ', data, at)
        at += 24
        name = data[at:at+length].decode()
        at += length
        tables[name] = (element, data[at:at+element*count])
        at += element*count
    return tables


def scene_diff(device_path, linux_path, linux_repeat_path):
    """Per-table 32-bit words where the device-prepared scene differs from Linux's. Words that
    differ between two Linux preparations of the same scene are uninitialized record padding and
    are excluded (approximate: padding that happens to repeat on Linux is still counted)."""
    device, linux, repeat = scene_tables(device_path), scene_tables(linux_path), scene_tables(linux_repeat_path)
    rows = {}
    for name in sorted(set(device) | set(linux)):
        a, b = device.get(name, (0, b''))[1], linux.get(name, (0, b''))[1]
        c = repeat.get(name, (0, b''))[1]
        if a == b:
            continue
        words = min(len(a), len(b), len(c))//4
        noise = {i for i in range(words) if b[4*i:4*i+4] != c[4*i:4*i+4]}
        differing = sum(a[4*i:4*i+4] != b[4*i:4*i+4] for i in range(words) if i not in noise)
        if differing or len(a) != len(b):
            rows[name] = dict(deviceBytes=len(a), linuxBytes=len(b), differing32BitWords=differing,
                              excludedPaddingWords=len(noise))
    return rows


def scene_reference(args):
    sys.path.insert(0, str(HERE))
    from render_gate import inputs
    out = args.output.resolve()
    references, linux_scenes, folder = out/'references'/args.label, out/'linux-scenes'/args.label, out/'runs'/args.label
    linux_repeat = out/'linux-scenes-repeat'/args.label
    folder.mkdir(parents=True, exist_ok=False)
    binary = args.bin/('codebrix_cycles_'+args.gate+'_gate')
    scenes = sorted(args.scenes.resolve().glob('*.scene'))
    if not scenes:
        raise RuntimeError('No device scene exchange files in '+str(args.scenes))
    rows = {}
    for mode, env in [('reference', {'CODEBRIX_CYCLES_SCENE_LOAD': str(args.scenes.resolve()),
                                     'CODEBRIX_CYCLES_REFERENCE_EXPORT': str(references)}),
                      ('linux-scene', {'CODEBRIX_CYCLES_SCENE_DUMP': str(linux_scenes)}),
                      ('linux-scene-repeat', {'CODEBRIX_CYCLES_SCENE_DUMP': str(linux_repeat)})]:
        environment = {k: v for k, v in os.environ.items() if not k.startswith('CODEBRIX_CYCLES_SCENE_') and
                       not k.startswith('CODEBRIX_CYCLES_REFERENCE_')}
        environment.update(env)
        command = [str(binary), str(folder/(mode+'-run')), *args.arg]
        started = time.time()
        result = subprocess.run(command, capture_output=True, text=True, env=environment, timeout=args.budget)
        (folder/(mode+'.stdout.txt')).write_text(result.stdout)
        (folder/(mode+'.stderr.txt')).write_text(result.stderr)
        report = json.loads(result.stdout) if result.returncode == 0 else None
        rows[mode] = dict(command=command, exit=result.returncode, seconds=round(time.time()-started, 1), report=report)
        if result.returncode or not report.get('passed'):
            raise RuntimeError(mode+' pass failed: '+result.stderr.strip()[-300:])
    files = {p.name: sha(p) for p in sorted(references.glob('*.f32'))}
    provenance = dict(created=datetime.datetime.now().isoformat(timespec='seconds'), role='authoritative',
                      policy='native/DEVICE.md "Validation reference policy"', label=args.label, gate=args.gate, args=args.arg,
                      platform=dict(system=platform.system(), machine=platform.machine(), kernel=platform.release(),
                                    libc=glibc_version()),
                      gateBinarySha256=sha(binary), sourceSha256=inputs(), runs=rows,
                      deviceSceneSha256={p.name: sha(p) for p in scenes},
                      linuxSceneSha256={p.name: sha(p) for p in sorted(linux_scenes.glob('*.scene'))},
                      inputDeviationDiagnostic={p.name: scene_diff(p, linux_scenes/p.name, linux_repeat/p.name) for p in scenes},
                      referenceSha256=files,
                      oracleSha256=hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest(), passed=bool(files))
    (out/(args.label+'.provenance.json')).write_text(json.dumps(provenance, indent=2)+'\n')
    print(args.label, 'references', len(files), 'oracleSha256', provenance['oracleSha256'])
    for name, diff in provenance['inputDeviationDiagnostic'].items():
        print('  input deviation', name, {k: v['differing32BitWords'] for k, v in diff.items()})
    return 0 if provenance['passed'] else 1


def compare(args):
    run, ref = args.run.resolve(), args.reference.resolve()
    rows = []
    if args.kind == 'samples':
        glibc = floats(ref/'samples.f32')
        gpu, local = floats(run.parent/(run.name+'.actual.bin')), floats(run.parent/(run.name+'.reference.bin'))
        valid = [i for i in range(0, len(glibc), 12) if glibc[i]]
        validity = sum(gpu[i] != glibc[i] for i in range(0, len(glibc), 12))
        local_validity = sum(local[i] != glibc[i] for i in range(0, len(glibc), 12))
        fields = [i+j for i in valid for j in range(1, 12)]
        authoritative = compare_arrays(gpu, glibc, fields)
        authoritative.update(validityDifferences=validity, validSamples=len(valid))
        authoritative['passed'] = authoritative['passed'] and validity == 0
        local_fields = [i+j for i in valid if local[i] for j in range(1, 12)]
        rows.append(dict(key='samples', authoritative=authoritative,
                         diagnosticLocalVersusGlibc=dict(compare_arrays(local, glibc, local_fields),
                                                         validityDifferences=local_validity, role='diagnostic'),
                         diagnosticGpuVersusLocal=dict(compare_arrays(gpu, local, local_fields), role='diagnostic')))
    else:
        cases = sorted(ref.glob('*.f32'))
        for reference in cases:
            key = reference.stem
            if args.kind == 'lighting':
                folder = run/key.replace('.film', '')
                gpu_path, local_path = folder/'run.gpu.film.bin', folder/'run.cpu.film.bin'
            else:
                gpu_path, local_path = run.parent/(run.name+'.gpu.film.bin'), run.parent/(run.name+'.cpu.film.bin')
            glibc = floats(reference)
            row = dict(key=key, referenceSha256=sha(reference))
            if local_path.exists():
                local = floats(local_path)
                row['diagnosticLocalVersusGlibc'] = dict(compare_arrays(local, glibc), role='diagnostic')
            if gpu_path.exists():
                gpu = floats(gpu_path)
                row['authoritative'] = compare_arrays(gpu, glibc)
                if local_path.exists():
                    row['diagnosticGpuVersusLocal'] = dict(compare_arrays(gpu, local), role='diagnostic')
            else:
                row['authoritative'] = dict(passed=False, notReached=True)
            rows.append(row)
    result = dict(kind=args.kind, run=str(run), reference=str(ref), practicalCriterion=PRACTICAL, strictCriterion=STRICT,
                  passed=bool(rows) and all(r['authoritative']['passed'] for r in rows), rows=rows)
    args.output.write_text(json.dumps(result, indent=2)+'\n')
    for r in rows:
        a = r['authoritative']
        d = r.get('diagnosticLocalVersusGlibc', {})
        print('%-16s gpu-vs-glibc passed=%s practical=%s strict=%s max=%.4g | local-vs-glibc differing=%s maxUlps=%s practical=%s' % (
            r['key'], a.get('passed'), a.get('practicalFailures'), a.get('strictDifferences'), a.get('maximumAbsoluteError', float('nan')),
            d.get('bitwiseDiffering'), d.get('maximumUlps'), d.get('practicalFailures')))
    return 0 if result['passed'] else 1


def stage_judge(args):
    sys.path.insert(0, str(HERE))
    from render_gate import inputs
    binary = args.bin/('codebrix_cycles_'+args.gate+'_gate')
    stages, scenes = args.stages.resolve(), args.scenes.resolve()
    folder = args.output.resolve().parent/(args.output.name.removesuffix('.json')+'-run')
    folder.mkdir(parents=True, exist_ok=True)
    environment = {k: v for k, v in os.environ.items() if not k.startswith('CODEBRIX_CYCLES_SCENE_') and
                   not k.startswith('CODEBRIX_CYCLES_REFERENCE_') and k != 'CODEBRIX_CYCLES_STAGE_JUDGE'}
    environment.update({'CODEBRIX_CYCLES_SCENE_LOAD': str(scenes), 'CODEBRIX_CYCLES_STAGE_JUDGE': str(stages)})
    command = [str(binary), str(folder/'judge'), *args.arg]
    started = time.time()
    result = subprocess.run(command, capture_output=True, text=True, env=environment, timeout=args.budget, cwd=folder)
    (folder/'judge.stdout.txt').write_text(result.stdout)
    (folder/'judge.stderr.txt').write_text(result.stderr)
    report = None
    if result.returncode == 0:
        report = json.loads(result.stdout)
    errors = [l for l in result.stderr.strip().splitlines() if l.strip() and not l.startswith('[OpenColorIO')]
    record = dict(role='authoritative', comparison='device GPU lockstep stages versus the Linux x64 glibc CPU oracle on the device scene',
                  practicalCriterion=PRACTICAL, strictCriterion=STRICT,
                  platform=dict(system=platform.system(), machine=platform.machine(), libc=glibc_version()),
                  command=command, exit=result.returncode, seconds=round(time.time()-started, 1),
                  gateBinarySha256=sha(binary), stagesSha256=sha(stages), stagesBytes=stages.stat().st_size,
                  deviceSceneSha256={p.name: sha(p) for p in sorted(scenes.glob('*.scene'))}, sourceSha256=inputs(),
                  report=report, failure=None if result.returncode == 0 else (errors[-1] if errors else 'exit %d' % result.returncode),
                  passed=result.returncode == 0 and bool(report and report.get('passed') and report.get('stageJudge')))
    args.output.write_text(json.dumps(record, indent=2)+'\n')
    print('stage-judge', args.gate, 'passed', record['passed'], record['failure'] or '', '%.0fs' % record['seconds'])
    return 0 if record['passed'] else 1


def main():
    parser = argparse.ArgumentParser()
    sub = parser.add_subparsers(dest='action', required=True)
    e = sub.add_parser('export')
    e.add_argument('--bin', type=Path, required=True)
    e.add_argument('--output', type=Path, required=True)
    e.add_argument('--job', action='append', required=True)
    e.add_argument('--budget', type=float, default=900)
    e.add_argument('--libm-record', type=Path)
    e.add_argument('--build-record', type=Path, help='The Linux build record (native/work/cycles-build.json)')
    s = sub.add_parser('scene-reference')
    s.add_argument('--bin', type=Path, required=True)
    s.add_argument('--scenes', type=Path, required=True)
    s.add_argument('--output', type=Path, required=True)
    s.add_argument('--label', required=True)
    s.add_argument('--gate', required=True)
    s.add_argument('--arg', action='append', default=[])
    s.add_argument('--budget', type=float, default=900)
    c = sub.add_parser('compare')
    c.add_argument('--kind', choices=['samples', 'lighting', 'render'], required=True)
    c.add_argument('--run', type=Path, required=True,
                   help='samples/render: the job output prefix (.../run); lighting: the job output directory')
    c.add_argument('--reference', type=Path, required=True)
    c.add_argument('--output', type=Path, required=True)
    j = sub.add_parser('stage-judge')
    j.add_argument('--bin', type=Path, required=True)
    j.add_argument('--scenes', type=Path, required=True, help='The device scene dump of the job (pass A)')
    j.add_argument('--stages', type=Path, required=True, help='The pulled PREFIX.stages.bin of the device run')
    j.add_argument('--gate', required=True)
    j.add_argument('--arg', action='append', default=[])
    j.add_argument('--output', type=Path, required=True)
    j.add_argument('--budget', type=float, default=900)
    args = parser.parse_args()
    return {'export': export, 'scene-reference': scene_reference, 'compare': compare,
            'stage-judge': stage_judge}[args.action](args)


if __name__ == '__main__':
    raise SystemExit(main())
