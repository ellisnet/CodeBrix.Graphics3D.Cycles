#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase X1 copy (via Phase E1) of the Phase C2 (ARM64) host-capture replayer (Samsung/HP); replay Linux-x64 v6/v7 captures on a device (GPU portability against the x64 oracle film).

Same staging and validation as render_replay.run() (maintained validate() reused unchanged); the capture inputs are
pushed to /data/local/tmp/codebrix-m8-hostcap-LABEL and the staged render_replay is used.
Usage: replay_host_captures.py DEVICE LABEL BUDGET_S JOB_TIMEOUT_S name=capture_dir ...
"""
import json, shlex, subprocess, sys, time, datetime
from pathlib import Path
HERE = Path(__file__).resolve().parent; C2 = HERE.parent; REPO = HERE.parents[3]; BUILD_ROOT = REPO/'native/work/merge5'  # merge wave 6 part 2
sys.path.insert(0, str(REPO/'native/tools'))
from device_gate import sha, save
from render_gate import inputs as source_inputs
import render_replay
ADB = '/home/jeremy/Android/Sdk/platform-tools/adb'
DEVICES = {'samsung': 'R52W207XQ1V', 'hp': '192.168.86.225:5555', 'pixel': '98271FFBA0048D'}
RIDS = {'samsung': 'android-arm64', 'hp': 'android-x64', 'pixel': 'android-arm64'}
STAGE = '/data/local/tmp/codebrix-m8-stage'
RUNTIME_SHAS = {'android-arm64': '8113cbfa287b5ffc3d9b3c092ddc5ce543b8390abf05eefdde86cf69792fed52', 'android-x64': 'c2b0105f28ba01068cd9b6af6d560315af90b4b2cab6c40fcb78813bd9bb4380'}
_C = Path(__file__).resolve().parent.parent
while (_C/'HOLD-ALL').exists(): time.sleep(20)  # merge wave 8 part 2: part 1c changed production sources; wait for the coordinator's release
device, label, budget, job_timeout = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])
caps = [a.split('=', 1) for a in sys.argv[5:]]
serial = DEVICES[device]; RID = RIDS[device]; RUNTIME_SHA = RUNTIME_SHAS[RID]; remote = '/data/local/tmp/codebrix-m8-hostcap-'+label
OUT = C2/'runs'/device/('hostcap-'+label); OUT.mkdir(parents=True, exist_ok=False)
def ts(): return datetime.datetime.now().strftime('%H:%M:%S')
def log(m):
    line = '['+ts()+'] '+device+' hostcap-'+label+': '+m; print(line, flush=True)
    with (C2/'timeline.log').open('a') as f: f.write(line+'\n')
with (C2/'budgets.txt').open('a') as f:
    f.write('[%s] DECLARED device=%s serial=%s corpus=hostcap label=%s wallBudget=%ds perJobTimeout=%ds pipelineBudgetMs=default(60000) replays=%d\n' % (ts(), device, serial, label, budget, job_timeout, len(caps)))
adb = [ADB, '-s', serial]
def sh(cmd, timeout=180, check=True): return subprocess.run(adb+['shell', cmd], capture_output=True, text=True, timeout=timeout, check=check)
T0 = time.time(); results = {}; sources = source_inputs(); state = dict(device=device, serial=serial, declaredBudgetSeconds=budget, captures=dict(caps))
try:
    staged = json.loads((C2/('staged-binaries-'+RID+'.json')).read_text())
    expected = {STAGE+'/'+f['remote']: f['sha256'] for f in staged['files'] if f['remote'] in ('bin/render_replay', 'lib/libcodebrix_wgpu.so', 'lib/libc++_shared.so')}
    got = sh(shlex.join(['sha256sum', *expected])).stdout
    assert {l.split()[1]: l.split()[0] for l in got.splitlines()} == expected
    ps = sh('ps -A -o PID,ARGS').stdout
    assert not any('codebrix_cycles' in l or 'render_replay' in l for l in ps.splitlines()), 'device busy'
    for name, cap in caps:
        cap = Path(cap).resolve(); left = T0+budget-time.time()
        if left < 30: results[name] = dict(passed=False, notRun='budget'); continue
        rd = remote+'/'+name; sh('mkdir -p '+shlex.quote(rd))
        for f in render_replay.inputs(cap): subprocess.run(adb+['push', str(cap/f), rd+'/'+f], check=True, capture_output=True, timeout=600)
        out = OUT/name; out.mkdir()
        env = 'env LD_LIBRARY_PATH='+STAGE+'/lib CODEBRIX_CYCLES_PIPELINE_LOG='+rd+'/pipelines.jsonl'+(' MESA_SHADER_CACHE_DISABLE=true' if RID == 'android-x64' else '')
        cmd = 'echo $$ > '+rd+'/active.pid; exec '+env+' '+STAGE+'/bin/render_replay '+rd+' '+rd+'/result'
        log('START '+name+' ('+str(cap)+')'); t = time.time()
        try: r = subprocess.run(adb+['shell', cmd], capture_output=True, text=True, timeout=min(job_timeout, left))
        except subprocess.TimeoutExpired:
            pid = sh('cat '+rd+'/active.pid', check=False).stdout.strip()
            if pid.isdecimal() and sh('readlink /proc/'+pid+'/exe', check=False).stdout.strip() == STAGE+'/bin/render_replay': sh('kill -KILL '+pid, check=False)
            results[name] = dict(passed=False, timeout=True); log('TIMEOUT '+name); break
        (out/'run.stdout.txt').write_text(r.stdout); (out/'run.stderr.txt').write_text(r.stderr)
        if r.returncode: results[name] = dict(passed=False, exit=r.returncode, error=r.stderr.strip().splitlines()[-3:]); log('FAIL '+name+' exit %d' % r.returncode); continue
        for s in ['arena.bin', 'adapter.txt', 'library.txt']: subprocess.run(adb+['pull', rd+'/result.'+s, str(out/('run.'+s))], check=True, capture_output=True, timeout=600)
        sh('rm -rf '+shlex.quote(rd))
        try:
            assert (out/'run.library.txt').read_text().strip() == STAGE+'/lib/libcodebrix_wgpu.so'
            rep = render_replay.validate(cap, out); rep.update(serial=serial, wallSeconds=round(time.time()-t, 1), captureFormat=json.loads((cap/'run.capture.json').read_text())['format'], hostCapture=str(cap), webGpuSha256=RUNTIME_SHA)
            save(out/'validation.json', rep); (out/'run.arena.bin').unlink()
            results[name] = dict(passed=True, report=rep); log('PASS %s readbacks=%d max=%.3g strict=%d fmt=%s' % (name, rep['results']['exactSchedulingReadbacks'], rep['maximumAbsoluteImageError'], rep['strictImageFailures'], rep['captureFormat']))
        except Exception as e:
            results[name] = dict(passed=False, assertion=repr(e)); log('FAIL '+name+' '+repr(e))
        save(OUT/'results.json', results)
finally:
    sh('rm -rf '+remote, check=False)
    ps = sh('ps -A -o PID,ARGS', check=False).stdout
    state.update(complete=True, passed=bool(results) and all(r.get('passed') for r in results.values()), wallSeconds=round(time.time()-T0, 1),
                 remoteProcessesAfter=[l for l in ps.splitlines() if 'render_replay' in l or 'codebrix-m8-' in l], inputsUnchanged=sources == source_inputs(),
                 failures=[k for k, r in results.items() if not r.get('passed')])
    save(OUT/'status.json', state); save(OUT/'results.json', results)
    log('END passed=%s wall=%.0fs/%ds' % (state['passed'], state['wallSeconds'], budget))
