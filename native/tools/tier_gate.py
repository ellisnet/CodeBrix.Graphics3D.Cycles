#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Linux gate for WebGPU device-tier selection (native/DEVICE.md, "Device tiers").

Every scenario is a separate process of the public C consumer
(tests/host/tier_consumer.c) because the persisted record and crash breadcrumbs
span processes. Other adapters are simulated only for the tier decision
(CODEBRIX_CYCLES_TEST_ADAPTER); the real Linux adapter always compiles and
renders. Crashes are real process terminations inside the pipeline-creation
breadcrumb scope (CODEBRIX_CYCLES_TEST_CRASH_PIPELINE); canary failures are real
WGSL rejections (CODEBRIX_CYCLES_TEST_CANARY=fail). Films are checked against
the original native CPU session with the practical criterion; strict
differences are reported only (this small fixture is not bitwise repeatable
across processes because film accumulation order varies).
"""
import argparse
import json
import os
import re
import shutil
import signal
import struct
import subprocess
import sys
import time
from pathlib import Path

from build import REPO, ROOT, WORK
from device_gate import sha, save

UNKNOWN = 'TestVendor|testarch|CodeBrix Test GPU|Test driver 1.0'
UNKNOWN_B = 'TestVendor|testarch|CodeBrix Test GPU B|Test driver 1.0'
UNKNOWN_C = 'TestVendor|testarch|CodeBrix Test GPU C|Test driver 1.0'
UNKNOWN_D = 'TestVendor|testarch|CodeBrix Test GPU D|Test driver 1.0'
ADRENO_640 = 'Qualcomm|adreno|Adreno (TM) 640|Driver Build: 85da404, Compiler Version: EV031.31.04.01|0x5143|0x6040001'
ADRENO_730 = 'Qualcomm|adreno|Adreno (TM) 730|Driver Build: test|0x5143|0x7030001'
ADRENO_UNKNOWN = 'Qualcomm|adreno|Adreno (TM) 999|Driver Build: test|0x5143|0x9990001'
INTEL_XE = 'Intel|xe|Intel(R) Xe Graphics (TGL GT2)|Mesa 24.0.8|0x8086|0x9a49'
GATE_ENV = ['CODEBRIX_CYCLES_PIPELINE_TIER', 'CODEBRIX_CYCLES_DEVICE_RECORD_DIR', 'CODEBRIX_CYCLES_DEVICE_RECORD',
            'CODEBRIX_CYCLES_PIPELINE_CANARY', 'CODEBRIX_CYCLES_PIPELINE_CANARY_BUDGET_MS', 'CODEBRIX_CYCLES_TEST_ADAPTER',
            'CODEBRIX_CYCLES_TEST_CRASH_PIPELINE', 'CODEBRIX_CYCLES_TEST_CANARY', 'CODEBRIX_CYCLES_PIPELINE_BUDGET_MS',
            'CODEBRIX_CYCLES_PIPELINE_BUDGET_ACTION', 'CODEBRIX_CYCLES_SHADER_SPECIALIZATION', 'CODEBRIX_CYCLES_PIPELINE_LOG']


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, default=WORK/'tier-validation')
    parser.add_argument('--jobs', default='6')
    parser.add_argument('--skip-build', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    build = WORK/'cycles-build'
    binary = build/'bin'
    library = build/'lib/libcodebrix_cycles_host.so'
    base = dict(os.environ)
    for name in GATE_ENV + ['OCIO']:
        base.pop(name, None)
    base['LD_LIBRARY_PATH'] = str(library.parent)
    state = dict(complete=False, passed=False, scenarios={})
    results = state['scenarios']
    films = {}

    def run(name, mode='render', tier='auto', record='-', flags='-', env=None, expect_crash=False, timeout=600):
        folder = out/name
        folder.mkdir()
        environment = dict(base)
        environment['CODEBRIX_CYCLES_PIPELINE_LOG'] = str(folder/'pipelines.jsonl')
        environment.update(env or {})
        command = [str(binary/'codebrix_host_tier_test'), mode, tier, str(record), flags, str(folder/'film.rgba.bin')]
        started = time.monotonic()
        with (folder/'stdout.txt').open('w') as stdout, (folder/'stderr.txt').open('w') as stderr:
            result = subprocess.run(command, cwd=REPO, env=environment, stdout=stdout, stderr=stderr, timeout=timeout)
        seconds = time.monotonic() - started
        save(folder/'invocation.json', dict(command=command, environment={k: v for k, v in environment.items()
                                            if k.startswith('CODEBRIX_') or k == 'MESA_SHADER_CACHE_DIR'},
                                            returncode=result.returncode, seconds=seconds))
        stderr_text = (folder/'stderr.txt').read_text()
        if expect_crash:
            assert result.returncode == -signal.SIGABRT, (name, result.returncode)
            assert 'the Vulkan driver terminated while creating WebGPU compute pipeline' in stderr_text, name
            results[name] = dict(crashed=True, returncode=result.returncode, seconds=seconds)
            save(out/'status.json', state)
            return None
        assert result.returncode == 0, (name, result.returncode, stderr_text[-2000:])
        report = json.loads((folder/'stdout.txt').read_text())
        log = [json.loads(line) for line in (folder/'pipelines.jsonl').read_text().splitlines()] \
            if (folder/'pipelines.jsonl').exists() else []
        before = report['before']['pipelines']['decision']
        row = dict(seconds=round(seconds, 1), tier=before['tier'], reason=before['reason'], detail=before['detail'],
                   recordStatus=before['record']['status'], recordMode=before['record']['mode'],
                   recordPath=before['record']['path'], budgetMs=report['before']['pipelines']['budgetMs'],
                   budgetSource=report['before']['pipelines']['budgetSource'], canary=before['canary'],
                   warnings=before['warnings'], pipelineTiers=sorted({r['tier'] for r in log}),
                   compiledPipelines=len(log))
        assert report['before']['backend'] == 'webgpu' and report['before']['abiVersion'] == 1
        if mode == 'render':
            after = report['after']['pipelines']
            row.update(afterTier=after['decision']['tier'], afterReason=after['decision']['reason'],
                       degraded=after['degraded'], pipelineWarnings=len(after['warnings']),
                       filmSha256=sha(folder/'film.rgba.bin'))
            films[name] = folder/'film.rgba.bin'
            assert log, name
        results[name] = row
        save(folder/'report.json', report)
        save(out/'status.json', state)
        return row, report, log

    def record_file(directory):
        files = sorted(Path(directory).glob('codebrix-cycles-device-*.txt'))
        assert len(files) == 1, files
        return files[0]

    def pending(directory):
        return sorted(Path(directory).glob('codebrix-cycles-device-*.pending-*'))

    try:
        if not args.skip_build:
            with (out/'build.stdout.txt').open('w') as stdout, (out/'build.stderr.txt').open('w') as stderr:
                subprocess.run([sys.executable, ROOT/'tools/build_cycles.py', '--jobs', args.jobs, '--target', 'codebrix_cycles_host',
                                '--target', 'codebrix_host_tier_test', '--target', 'codebrix_host_session_reference'],
                               stdout=stdout, stderr=stderr, check=True, timeout=1800)
        pins = {str(p): sha(p) for p in [library, binary/'codebrix_host_tier_test', binary/'codebrix_host_session_reference']}
        save(out/'binary-pins.json', pins)
        # Independent original native CPU session for the same fixture (round 0).
        cpu = out/'cpu-reference'
        cpu.mkdir()
        cpu_env = dict(base)
        with (cpu/'stdout.txt').open('w') as stdout, (cpu/'stderr.txt').open('w') as stderr:
            subprocess.run([str(binary/'codebrix_host_session_reference'), 'CPU', str(cpu/'run')], env=cpu_env,
                           stdout=stdout, stderr=stderr, check=True, timeout=600)
        reference = cpu/'run.0.rgba.bin'
        records = out/'records'

        # 1. Fresh device, no record: full fidelity; the record is created.
        a = records/'a'
        row, report, log = run('fresh-no-record', record=a, tier='auto', env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN})
        assert row['tier'] == 'full' and row['reason'] == 'default' and row['recordStatus'] == 'absent'
        assert row['detail'].startswith('first run') and row['pipelineTiers'] == ['full'] and row['afterTier'] == 'full'
        text = record_file(a).read_text()
        assert text.startswith('codebrix-cycles-device-record 1\n') and re.search(r'^stage=full\|surface\|prepare_surface\|[1-9]\d*\|0\|', text, re.M)
        assert 'breadcrumb=' not in text and 'failure=' not in text and not pending(a)
        shutil.copy2(record_file(a), out/'fresh-no-record/record-after.txt')
        # 2. Same device again: the record decides (full) before any compile.
        row, *_ = run('second-run-record-full', record=a, env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN})
        assert row['tier'] == 'full' and row['reason'] == 'record' and row['recordStatus'] == 'valid'
        assert row['pipelineTiers'] == ['full']

        # 3. Real crash breadcrumb, then the next run starts conservative.
        b = records/'b'
        run('crash-in-prepare-surface', record=b, expect_crash=True,
            env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN_B, 'CODEBRIX_CYCLES_TEST_CRASH_PIPELINE': 'prepare_surface'})
        left = pending(b)
        assert len(left) == 1 and 'signal=6\n' in left[0].read_text() and 'entry=prepare_surface\n' in left[0].read_text()
        shutil.copy2(left[0], out/'crash-in-prepare-surface/pending-breadcrumb.txt')
        row, report, log = run('breadcrumb-selects-conservative', record=b, env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN_B})
        crumbs = report['before']['pipelines']['decision']['record']['breadcrumbs']
        assert row['tier'] == 'conservative' and row['reason'] == 'record' and 'prepare_surface' in row['detail']
        assert crumbs and crumbs[-1]['kind'] == 'signal' and crumbs[-1]['signal'] == 6 and crumbs[-1]['tier'] == 'full'
        assert row['pipelineTiers'] == ['conservative'] and all(r['workgroupSize'] == 1 for r in log)
        assert row['degraded'] and not pending(b)
        text = record_file(b).read_text()
        assert 'breadcrumb=full|surface|prepare_surface|signal|6|' in text
        assert re.search(r'^stage=conservative\|surface\|prepare_surface\|[1-9]\d*\|0\|', text, re.M)
        shutil.copy2(record_file(b), out/'breadcrumb-selects-conservative/record-after.txt')
        # Gate override: force full despite the record; ignore mode reads but does not apply it.
        row, *_ = run('override-full-over-record', record=b, env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN_B,
                                                                 'CODEBRIX_CYCLES_PIPELINE_TIER': 'full'}, mode='create')
        assert row['tier'] == 'full' and row['reason'] == 'override'
        row, *_ = run('ignore-record', record=b, env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN_B,
                                                     'CODEBRIX_CYCLES_DEVICE_RECORD': 'ignore'}, mode='create')
        assert row['tier'] == 'full' and row['reason'] == 'default' and row['recordMode'] == 'ignore' and row['recordStatus'] == 'valid'
        row, *_ = run('record-off', record=b, env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN_B,
                                                  'CODEBRIX_CYCLES_DEVICE_RECORD': 'off'}, mode='create')
        assert row['tier'] == 'full' and row['recordMode'] == 'off' and row['recordPath'] == ''
        row, *_ = run('host-record-off-flag', record=b, flags='o', env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN_B}, mode='create')
        assert row['tier'] == 'full' and row['recordMode'] == 'off'
        row, *_ = run('environment-record-dir', record='-', mode='create',
                      env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN_B, 'CODEBRIX_CYCLES_DEVICE_RECORD_DIR': str(b)})
        assert row['tier'] == 'conservative' and row['reason'] == 'record'

        # 4./5. Stale records (different shader hash or runtime) are never applied.
        for name, key in [('stale-shader-hash', 'shaderHash='), ('stale-runtime', 'runtime=')]:
            directory = records/name
            directory.mkdir(parents=True)
            source = record_file(b)
            lines = source.read_text().splitlines()
            lines = [line + '-different' if line.startswith(key) else line for line in lines]
            (directory/source.name).write_text('\n'.join(lines) + '\n')
            row, report, log = run(name, record=directory, env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN_B})
            assert row['tier'] == 'full' and row['reason'] == 'default' and row['recordStatus'] == name
            assert any(name in w for w in row['warnings']) and row['pipelineTiers'] == ['full']
            text = record_file(directory).read_text()
            assert 'breadcrumb=' not in text and '-different' not in text  # replaced by this run's record

        # Known-adapter table (decision only; the real adapter compiles nothing more).
        for name, adapter, tier, reason, budget, source in [
                ('table-adreno-640', ADRENO_640, 'full', 'table', 20000, 'table'),
                ('table-adreno-730', ADRENO_730, 'full', 'table', 60000, 'default'),
                ('table-intel-xe-mesa-24.0.8', INTEL_XE, 'full', 'table', 60000, 'default'),
                ('table-real-linux-adapter', None, 'full', 'table', 60000, 'default'),
                ('unknown-mobile-tighter-budget', ADRENO_UNKNOWN, 'full', 'default', 20000, 'unknown-mobile'),
                ('unknown-desktop', UNKNOWN, 'full', 'default', 60000, 'default')]:
            env = {'CODEBRIX_CYCLES_TEST_ADAPTER': adapter} if adapter else {}
            row, *_ = run(name, mode='create', env=env)
            assert (row['tier'], row['reason'], row['budgetMs'], row['budgetSource']) == (tier, reason, budget, source), row
        assert 'Linux Intel UHD ADL-S' in results['table-real-linux-adapter']['detail']
        # Adreno 640 renders at full fidelity by table with its own 20 s budget; the
        # record then decides, and a full-tier crash breadcrumb overrides the table.
        c = records/'c'
        row, *_ = run('table-adreno-640-render', record=c, env={'CODEBRIX_CYCLES_TEST_ADAPTER': ADRENO_640})
        assert row['tier'] == 'full' and row['reason'] == 'table' and row['pipelineTiers'] == ['full']
        assert (row['budgetMs'], row['budgetSource']) == (20000, 'table')
        row, *_ = run('table-adreno-640-record-full', record=c, mode='create',
                      env={'CODEBRIX_CYCLES_TEST_ADAPTER': ADRENO_640})
        assert row['tier'] == 'full' and row['reason'] == 'record'
        run('adreno-640-crash-in-prepare-surface', record=c, expect_crash=True,
            env={'CODEBRIX_CYCLES_TEST_ADAPTER': ADRENO_640, 'CODEBRIX_CYCLES_TEST_CRASH_PIPELINE': 'prepare_surface'})
        row, *_ = run('record-overrides-table', record=c, mode='create', env={'CODEBRIX_CYCLES_TEST_ADAPTER': ADRENO_640})
        assert row['tier'] == 'conservative' and row['reason'] == 'record' and 'prepare_surface' in row['detail']
        row, *_ = run('host-opt-in-conservative', tier='conservative', mode='create', env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN})
        assert row['tier'] == 'conservative' and row['reason'] == 'opt-in'

        # Canary paths.
        d = records/'d'
        row, *_ = run('canary-pass', record=d, flags='c', env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN_C})
        assert row['tier'] == 'full' and row['canary']['enabled'] and row['canary']['result'] == 'passed'
        assert row['pipelineTiers'] == ['full']
        row, *_ = run('canary-disabled-by-environment', record=d, flags='c', mode='create',
                      env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN_C, 'CODEBRIX_CYCLES_PIPELINE_CANARY': '0'})
        assert not row['canary']['enabled'] and row['canary']['result'] == 'not-run'
        row, *_ = run('canary-slow-warns-only', record=d, flags='c', env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN_C,
                                                                         'CODEBRIX_CYCLES_PIPELINE_CANARY_BUDGET_MS': '1'})
        assert row['tier'] == 'full' and row['canary']['result'] == 'slow' and row['pipelineTiers'] == ['full'] and row['degraded']
        e = records/'e'
        row, *_ = run('canary-failure', record=e, flags='c', env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN_D,
                                                                 'CODEBRIX_CYCLES_TEST_CANARY': 'fail'})
        assert row['tier'] == 'conservative' and row['reason'] == 'canary' and row['canary']['result'] == 'failed'
        assert row['pipelineTiers'] == ['conservative']
        assert 'failure=full|canary|prepare_surface|' in record_file(e).read_text()
        row, *_ = run('canary-failure-recorded', record=e, mode='create', env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN_D})
        assert row['tier'] == 'conservative' and row['reason'] == 'record' and 'canary' in row['detail']
        f = records/'f'
        run('canary-crash', record=f, flags='c', expect_crash=True,
            env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN_D, 'CODEBRIX_CYCLES_TEST_CRASH_PIPELINE': 'canary:prepare_surface'})
        row, report, log = run('canary-breadcrumb-selects-conservative', record=f, flags='c',
                               env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN_D})
        assert row['tier'] == 'conservative' and row['reason'] == 'record' and row['canary']['result'] == 'skipped'
        assert row['pipelineTiers'] == ['conservative']

        # A slow compile never switches tiers (budget 1 ms: every pipeline is over budget).
        g = records/'g'
        row, report, log = run('slow-compile-no-switch', record=g, env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN,
                                                                       'CODEBRIX_CYCLES_PIPELINE_BUDGET_MS': '1'})
        assert row['tier'] == 'full' and row['afterTier'] == 'full' and row['pipelineTiers'] == ['full']
        over = sum(r['pipelineMs'] > 1 for r in log)
        assert row['degraded'] and over >= 1 and row['pipelineWarnings'] == over and row['budgetSource'] == 'environment'
        row['pipelinesOverBudget'] = over
        text = record_file(g).read_text()
        assert 'failure=' not in text and 'breadcrumb=' not in text
        row, *_ = run('slow-compile-next-run', record=g, mode='create', env={'CODEBRIX_CYCLES_TEST_ADAPTER': UNKNOWN})
        assert row['tier'] == 'full' and row['reason'] == 'record'

        # Films: every film meets the practical criterion against the original
        # CPU session; full-tier films are bitwise equal to each other.
        reference_values = struct.unpack('<3072f', reference.read_bytes())
        comparisons = {}
        for name, path in films.items():
            values = struct.unpack('<3072f', path.read_bytes())
            errors = [abs(x - y) for x, y in zip(values, reference_values)]
            comparisons[name] = dict(tier=results[name]['tier'], filmSha256=sha(path), maximumAbsoluteError=max(errors),
                                     practicalFailures=sum(e > 5e-5 + 2e-4*abs(y) for e, y in zip(errors, reference_values)),
                                     strictDifferences=sum(e > 1e-7 + 1e-7*abs(y) for e, y in zip(errors, reference_values)))
            assert comparisons[name]['practicalFailures'] == 0, name
        full_hashes = {row['filmSha256'] for name, row in comparisons.items() if results[name]['afterTier'] == 'full'}
        conservative_hashes = {row['filmSha256'] for name, row in comparisons.items() if results[name]['afterTier'] == 'conservative'}
        state['filmComparisons'] = comparisons
        state['fullTierFilmHashes'] = sorted(full_hashes)
        state['conservativeTierFilmHashes'] = sorted(conservative_hashes)
        # Films of this fixture are not bitwise repeatable across processes
        # (atomic film accumulation order); report the spread instead.
        state['maximumStrictDifferencesAgainstCpu'] = max(row['strictDifferences'] for row in comparisons.values())
        state['maximumAbsoluteErrorAgainstCpu'] = max(row['maximumAbsoluteError'] for row in comparisons.values())
        assert all(sha(Path(p)) == digest for p, digest in pins.items())
        state.update(complete=True, passed=True, cpuReferenceSha256=sha(reference))
        print('Device-tier gate passed:', len(results), 'scenarios', flush=True)
    except Exception as error:
        state.update(complete=True, passed=False, error=repr(error))
        raise
    finally:
        save(out/'status.json', state)


if __name__ == '__main__':
    main()
