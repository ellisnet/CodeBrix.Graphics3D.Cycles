#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Lane P: run Pixel 4 XL triage steps one job at a time under the 3x cutoff (plan.json), holding the Pixel lock only
for the batch; stage once per batch, clean the device and release the lock at the end; checkpoint after every step.
Usage: pixel_batch.py STEP [STEP ...]   (step ids below; 'list' prints them)"""
import json, subprocess, sys, time, datetime, os
from pathlib import Path
P = Path(__file__).resolve().parents[1]; T = P/'tools'
ADB = '/home/jeremy/Android/Sdk/platform-tools/adb'; SERIAL = '98271FFBA0048D'
LOCKS = Path('/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/device-locks'); LOCK = LOCKS/'pixel'
REF = str(P/'reference/linux-x64')
F = ['--reference', REF, '--device-scene', '--pipeline-budget-ms', '20000']   # merge wave 3 Pixel image/sample flags
FM = ['--reference', REF, '--pipeline-budget-ms', '20000']                   # merge wave 3 Pixel materials flags
FN = ['--pipeline-budget-ms', '20000']                                       # no reference (device's own oracle)
# step id: (kind, corpus, jobs, replays, flags, extra)
STEPS = {
 'item2-chain': ('job', 'motion', ['object-moving-images'], 'perjob1', F, []),
 'item2-device-oracle': ('job', 'motion', ['object-moving-images'], '0', FN, []),
 'item2-x64-oracle': ('job', 'motion', ['object-moving-images'], '0', FN, ['--extra-env', 'CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2']),
 'principled_svm': ('job', 'materials', ['principled_svm'], '0', FM, []),
 'principled_bounce': ('job', 'materials', ['principled_bounce'], '0', FM, []),
 'principled_render': ('job', 'materials', ['principled_render'], 'all', FM, []),
 'nodes_svm': ('job', 'materials', ['nodes_svm'], '0', FM, []),
 'nodes_world': ('job', 'materials', ['nodes_world'], '0', FM, []),
 'nodes_render': ('job', 'materials', ['nodes_render'], 'all', FM, []),
 'triangle_sample': ('job', 'triangle', ['triangle_sample'], '0', F, []),
 'surface': ('job', 'triangle', ['surface'], '0', F, []),
 'triangle_lighting': ('job', 'triangle', ['triangle_lighting'], 'all', F, []),
 'emitter_sample': ('job', 'emitter', ['emitter_sample'], '0', F, []),
 'feature_combination': ('job', 'lighting', ['feature_combination'], 'perjob2', F, []),
 'lighting_mis': ('job', 'lighting', ['lighting_mis'], 'perjob2', F, []),
 'broad_lighting': ('job', 'lighting', ['broad_lighting'], 'perjob2', F, []),
 'spec-refusal': ('job', 'spec-refusal', ['glass_render-spec', 'glass_render-nospec-refused'], '0', F, []),
 'transparent_shadow': ('job', 'transparent', ['transparent_shadow'], 'all', F, []),
 'transparent_lighting': ('job', 'transparent', ['transparent_lighting'], 'all', F, []),
 'shadow_terminator': ('job', 'terminator', ['shadow_terminator'], 'all', F, []),
 'object-stationary-images': ('job', 'motion', ['object-stationary-images'], '0', F, []),
 'deforming-moving-images': ('job', 'motion', ['deforming-moving-images'], 'perjob1', F, []),
 'deforming-stationary-images': ('job', 'motion', ['deforming-stationary-images'], '0', F, []),
 'camera-static': ('job', 'motion', ['camera-static'], '0', F, []),
 'camera-motion': ('job', 'motion', ['camera-motion'], '0', F, []),
 'closest': ('job', 'precision', ['closest'], '0', F, []),
 'shadow': ('job', 'precision', ['shadow'], 'all', F, []),
 **{v: ('job', 'volume', [v], 'all', F, []) for v in ['volume-state', 'volume-absorption', 'volume-isotropic', 'volume-forward', 'volume-backward',
     'boundary-state', 'boundary-compaction-guard', 'closed-single', 'closed-overlap', 'closed-inside']},
 'hostcap-triangle': ('hostcap', 'linux-triangle', [('tri%d' % c, 'linux/l2-triangle/triangle_lighting/run/case-%d' % c) for c in range(6)], None, None, []),
 'hostcap-terminator': ('hostcap', 'linux-terminator', [('term%d' % c, 'linux/l2-shadow-terminator/shadow_terminator/run/case-%d' % c) for c in range(10)], None, None, []),
 'app': ('app', None, None, None, None, []),
}
def ts(): return datetime.datetime.now().strftime('%H:%M:%S')
def lg(m):
    with (P/'timeline.log').open('a') as f: f.write('[%s] pixel %s\n' % (ts(), m))
def cutoffs():
    return {(r['corpus'], r['job']): r for r in json.loads((P/'plan.json').read_text())['rows']}
def cut(corpus, job):
    c = cutoffs()
    for key in [(corpus, job), *[(k, job) for k, _ in c if _ == job]]:
        if key in c: return c[key]['cutoff']
    return None
def adb(*a, timeout=120): return subprocess.run([ADB, '-s', SERIAL, *a], capture_output=True, text=True, timeout=timeout)
def declare(line):
    with (P/'budgets.txt').open('a') as f: f.write('[%s] DECLARED %s\n' % (ts(), line))
def run_step(step, n):
    kind, corpus, jobs, replays, flags, extra = STEPS[step]
    label = 'p-%s-%d' % (step, n)
    if kind == 'job':
        c = cut(corpus, jobs[0]) if len(jobs) == 1 else max(cut(corpus, j) or 0 for j in jobs)
        if c is None: lg('SKIP %s: no Samsung baseline (deferred to merge)' % step); return 'no-baseline'
        sel = json.loads(subprocess.check_output([sys.executable, str(T/'run_job.py'), 'pixel', corpus, 'list', '1', '1', '--jobs', ','.join(jobs), '--replays', replays, '--list-replays']))
        rc = {r[0]: cut(corpus, r[0]) for r in sel}
        missing = [k for k, v in rc.items() if v is None]
        rc = {k: (v if v is not None else c) for k, v in rc.items()}
        budget = c*len(jobs) + sum(rc.values()) + 900*len(jobs) + 300
        declare('step=%s corpus=%s jobs=%s cutoff=%ss replays=%s replayCutoffs=%s%s budget=%ds' % (step, corpus, ','.join(jobs), c, len(sel), json.dumps(rc),
                (' (no Samsung wall for %s; source-job cutoff used as placeholder)' % missing) if missing else '', budget))
        cmd = [sys.executable, str(T/'run_job.py'), 'pixel', corpus, label, str(budget), str(c), '--jobs', ','.join(jobs), '--replays', replays,
               '--replay-cutoffs', ','.join('%s=%d' % kv for kv in rc.items()), *flags, *extra]
        if len(jobs) > 1:  # per-job cutoffs inside one invocation: run each job separately instead
            for j in jobs:
                cj = cut(corpus, j); declare('step=%s job=%s cutoff=%ss' % (step, j, cj))
                cmdj = [sys.executable, str(T/'run_job.py'), 'pixel', corpus, label+'-'+j, str(cj+1200), str(cj), '--jobs', j, '--replays', '0', *flags, *extra]
                lg('START step %s job %s cutoff %ss' % (step, j, cj))
                with (P/'device'/(label+'-'+j+'.log')).open('w') as f: r = subprocess.run(cmdj, stdout=f, stderr=subprocess.STDOUT, timeout=cj+1500)
                lg('END step %s job %s exit=%d' % (step, j, r.returncode))
            return 'done'
    elif kind == 'hostcap':
        args = ['%s=%s=%d' % (n, P/c, cut('hostcap-'+corpus, n)) for n, c in jobs]
        budget = sum(cut('hostcap-'+corpus, n) for n, _ in jobs) + 900
        declare('step=%s hostcap %s cutoffs=%s budget=%ds' % (step, corpus, {n: cut('hostcap-'+corpus, n) for n, _ in jobs}, budget))
        cmd = [sys.executable, str(T/'replay_host_captures.py'), 'pixel', corpus+'-'+str(n), str(budget), '900', *args]
    else:
        c = cut('app', 'managed-app-3-scenes')
        declare('step=app managed WebGPU app (3 scenes) cutoff=%ss (--result-timeout), gate wall budget %ds' % (c, c+900))
        out = P/'android-app'/('webgpu-pixel-%d' % n)
        cmd = [sys.executable, str(P.parents[2]/'native/tools/android_app_gate.py'), '--build-output', str(P/'android-app/validation-p'), '--output', str(out),
               '--device', 'pixel,android-arm64,'+SERIAL, '--mode', 'webgpu', '--linux-reference', str(P/'reference/managed-linux-x64'),
               '--result-timeout', str(c), '--uninstall']
        budget = c+900
    lg('START step %s (%s)' % (step, label))
    with (P/'device'/(label+'.log')).open('w') as f:
        try: r = subprocess.run(cmd, stdout=f, stderr=subprocess.STDOUT, timeout=budget+300, cwd=str(P.parents[2]))
        except subprocess.TimeoutExpired: lg('END step %s HOST TIMEOUT (budget)' % step); return 'host-timeout'
    lg('END step %s exit=%d' % (step, r.returncode)); return 'done'

if __name__ == '__main__':
    if sys.argv[1:] == ['list']: print('\n'.join(STEPS)); sys.exit()
    steps = sys.argv[1:]; assert all(s in STEPS for s in steps), [s for s in steps if s not in STEPS]
    for x in ['pixel', SERIAL]:
        if (LOCKS/x).exists(): lg('lock %s HELD by another owner; batch not started' % x); sys.exit(2)
    LOCK.mkdir()
    (LOCK/'owner.txt').write_text('lane P (p-pixel-triage) pid %d %s pixel_batch.py %s\n' % (os.getpid(), datetime.datetime.now().isoformat(timespec='seconds'), ' '.join(steps)))
    lg('lock taken for batch: '+' '.join(steps))
    try:
        subprocess.run([sys.executable, str(T/'checkpoint.py'), 'lock-held'], timeout=120)
        declare('pixel staging push (stage.py push pixel) budget 600 s')
        r = subprocess.run([sys.executable, str(T/'stage.py'), 'push', 'pixel'], capture_output=True, text=True, timeout=600)
        lg('staged exit=%d %s' % (r.returncode, r.stdout.strip()[-80:])); assert r.returncode == 0, r.stderr[-500:]
        n = int(time.time())
        for s in steps:
            run_step(s, n)
            subprocess.run([sys.executable, str(T/'checkpoint.py'), 'lock-held'], timeout=120)
    finally:
        adb('shell', 'rm -rf /data/local/tmp/codebrix-p-*')
        left = adb('shell', 'ls -d /data/local/tmp/codebrix-p-* 2>/dev/null; pm list packages | grep -i codebrix; ps -A -o PID,ARGS | grep -E "codebrix|render_replay" | grep -v grep').stdout.strip()
        lg("cleaned (left: '%s')" % left)
        (LOCK/'owner.txt').unlink(); LOCK.rmdir(); lg('lock released')
        subprocess.run([sys.executable, str(T/'checkpoint.py'), 'released'], timeout=120)
