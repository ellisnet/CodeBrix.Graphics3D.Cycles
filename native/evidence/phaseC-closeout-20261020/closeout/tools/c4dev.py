#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase C close-out device confirmation runner (Samsung, Pixel, HP).

Adapted from the Phase C2 ARM64 tools (stage.py, run_corpus.py): same ELF/NEEDED
review, runtime-hash checks, staged-hash verification, per-job pipeline log,
exact-PID timeout cleanup and report assertions. Differences: one plan file per run
(jobs with their own environment, expected outcome and pre/post shell probes for
the device record), devices of both RIDs, no replays.

Usage:
  c4dev.py review RID BUILD_HOST_DIR
  c4dev.py push DEVICE
  c4dev.py run DEVICE PLAN.json LABEL WALL_BUDGET_S JOB_TIMEOUT_S [--pipeline-budget-ms N]
"""
import argparse, datetime, json, re, shlex, shutil, struct, subprocess, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent; C4 = HERE.parent; REPO = HERE.parents[3]
sys.path.insert(0, str(REPO/'native/tools'))
from build import PACKAGE
from device_gate import sha, save
from render_gate import inputs
from android_toolchain import AndroidToolchain
from build_cycles_android import dependencies
from android_host_gate import PLATFORM

ADB = '/home/jeremy/Android/Sdk/platform-tools/adb'
DEVICES = {'samsung': ('R52W207XQ1V', 'android-arm64', 'SM-X700'), 'pixel': ('98271FFBA0048D', 'android-arm64', 'Pixel 4 XL'),
           'hp': ('192.168.86.225:5555', 'android-x64', None)}
RUNTIME_SHA = {'android-arm64': '8113cbfa287b5ffc3d9b3c092ddc5ce543b8390abf05eefdde86cf69792fed52'}
MACHINE = {'android-arm64': (183, 'arm64-v8a'), 'android-x64': (62, 'x86_64')}
STAGE_REMOTE = '/data/local/tmp/codebrix-c4-stage'
PRACTICAL = '5e-5 + 2e-4*abs(reference)'; STRICT = '1e-7 + 1e-7*abs(reference)'
IMAGE_GATES = {'triangle_lighting', 'transparent_lighting', 'lighting', 'lighting_mis', 'broad_lighting', 'feature_combination',
               'object_motion_render', 'deforming_motion_render'}

def ts(): return datetime.datetime.now().strftime('%H:%M:%S')

# Report checks mirror the maintained Linux gate scripts / C2 runner (unchanged criteria).
def img(cases):
    def check(r): assert r['cases'] == cases and r['nativeSceneAndScheduler'] and r['independentOriginalCpuImage']
    return check
def c_render(r): assert r['nativeSceneAndScheduler'] and r['independentCpuImage'] and r['repeatBitwiseIdentical']
CHECKS = {'glass_render': c_render, 'transparent_lighting': img(8), 'lighting_mis': img(8)}

def staged_path(rid): return C4/('staged-'+rid+'.json')

def review(rid, build_host):
    build_host = Path(build_host).resolve()
    sources = inputs()
    status = json.loads((build_host/'status.json').read_text()); inv = json.loads((build_host/'invocation.json').read_text())
    assert status['complete'] and status['passed'] and inv['sourceSha256'] == sources, 'Android build does not match sources'
    toolchain = AndroidToolchain(rid); assert toolchain.identity() == inv['toolchain']
    prefix = dependencies.prefix(rid)
    runtime = PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so'; assert sha(runtime) == inv['runtimeSha256']
    if rid in RUNTIME_SHA: assert sha(runtime) == RUNTIME_SHA[rid]
    exes = {p.name: p for p in (build_host/'build/bin').glob('codebrix_cycles_*_gate')}
    for name, p in exes.items(): assert sha(p) == status['artifacts']['bin/'+name]
    libs = {p.name: p for p in (prefix/'lib').glob('*.so*') if p.is_file()}
    libs.update({runtime.name: runtime, toolchain.runtime.name: toolchain.runtime})
    stage = {**{n: ('lib', p) for n, p in libs.items()}, **{n: ('bin', p) for n, p in exes.items()}}
    machine = MACHINE[rid][0]; files = []
    for name, (kind, p) in sorted(stage.items()):
        data = p.resolve().read_bytes(); assert data[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<H', data, 18)[0] == machine
        dynamic = subprocess.check_output([str(toolchain.bin/'llvm-readelf'), '-d', '--version-info', str(p)], text=True)
        needed = re.findall(r'\(NEEDED\).*?\[(.*?)\]', dynamic)
        assert not set(needed)-set(libs)-PLATFORM, (name, set(needed)-set(libs)-PLATFORM); assert 'GLIBC_' not in dynamic
        files.append(dict(path=str(p), remote=kind+'/'+name, sha256=sha(p), needed=needed, bytes=len(data)))
    save(staged_path(rid), dict(created=datetime.datetime.now().isoformat(timespec='seconds'), rid=rid, build=str(build_host),
        ndk=toolchain.identity(), minimumAndroidApi=33,
        runtime=dict(package='CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433', sha256=sha(runtime)),
        sourceSha256=sources, buildInvocationCommands=inv['commands'], platformPatchSha256=inv['platformPatchSha256'],
        dependencyMarkerSha256=inv['dependencyMarkerSha256'], overlaySha256=inv['overlaySha256'],
        elfReview='ELF64 little-endian, e_machine %d, NEEDED closure within staged libs + Android platform set, no GLIBC symbol versions' % machine,
        files=files))
    print(rid, len(files), 'files reviewed')

def push(device):
    serial, rid, _ = DEVICES[device]; staged = json.loads(staged_path(rid).read_text())
    assert staged['sourceSha256'] == inputs()
    local = C4/('stage-tmp-'+device)
    if local.exists(): shutil.rmtree(local)
    (local/'lib').mkdir(parents=True); (local/'bin').mkdir()
    expected = {}
    for f in staged['files']:
        assert sha(Path(f['path'])) == f['sha256']
        shutil.copy2(f['path'], local/f['remote']); expected[STAGE_REMOTE+'/'+f['remote']] = f['sha256']
    adb = [ADB, '-s', serial]
    subprocess.run(adb+['shell', 'rm -rf /data/local/tmp/codebrix-c4-stage'], check=True)
    subprocess.run(adb+['push', str(local), STAGE_REMOTE], check=True, capture_output=True, timeout=1200)
    subprocess.run(adb+['shell', 'chmod 755 '+STAGE_REMOTE+'/bin/*'], check=True)
    out = subprocess.run(adb+['shell', shlex.join(['sha256sum', *expected])], check=True, capture_output=True, text=True).stdout
    assert {l.split()[1]: l.split()[0] for l in out.splitlines()} == expected
    shutil.rmtree(local)
    save(C4/('staged-'+device+'.json'), dict(serial=serial, rid=rid, remote=STAGE_REMOTE, verified=datetime.datetime.now().isoformat(timespec='seconds'), sha256=expected))
    print(device, len(expected), 'files staged and verified')

def summarize_pipelines(pipelines):
    by = {}
    for p in pipelines:
        key = p.get('entry', '?')+('' if p.get('specialized', True) else ' (unspecialized)')
        by.setdefault(key, []).append(round(p.get('pipelineMs', 0)/1000, 2))
    return dict(count=len(pipelines), tiers=sorted({p.get('tier', '?') for p in pipelines}),
                tierReasons=sorted({p.get('tierReason', '?') for p in pipelines}),
                workgroupSizes=sorted({p.get('workgroupSize', -1) for p in pipelines}),
                adapters=sorted({p.get('adapter', '?') for p in pipelines}),
                maxSeconds=max([p.get('pipelineMs', 0) for p in pipelines], default=0)/1000, secondsByEntry=by)

def run(args):
    serial, rid, model = DEVICES[args.device]
    plan = json.loads(Path(args.plan).read_text()); jobs = plan['jobs']
    OUT = C4/'runs'/args.device/args.label; OUT.mkdir(parents=True, exist_ok=False)
    shutil.copy2(args.plan, OUT/'plan.json')
    REMOTE = '/data/local/tmp/codebrix-c4-'+args.label
    T0 = time.time(); deadline = T0+args.budget
    def log(msg):
        line = '['+ts()+'] '+args.device+' '+args.label+': '+msg
        print(line, flush=True)
        with (C4/'device-timeline.log').open('a') as f: f.write(line+'\n')
    with (C4/'budgets.txt').open('a') as f:
        f.write('[%s] DECLARED device=%s serial=%s run=%s wallBudget=%ds perJobTimeout=%ds pipelineBudgetMs=%s jobs=%s\n' % (
            ts(), args.device, serial, args.label, args.budget, args.job_timeout, args.pipeline_budget_ms or 'policy',
            ','.join(j['label'] for j in jobs)))
    state = dict(complete=False, phase='stage', device=args.device, serial=serial, rid=rid, label=args.label, purpose=plan.get('purpose'),
                 declaredBudgetSeconds=args.budget, perJobTimeoutSeconds=args.job_timeout, pipelineBudgetMs=args.pipeline_budget_ms,
                 practicalCriterion=PRACTICAL, strictCriterion=STRICT, startedLocal=ts())
    save(OUT/'status.json', state)
    commands = []
    def call(argv, name, timeout=120, check=True):
        command = [ADB, '-s', serial, *argv]; commands.append(dict(command=command, timeout=timeout)); save(OUT/'commands.json', commands)
        with (OUT/(name+'.stdout.txt')).open('w') as so, (OUT/(name+'.stderr.txt')).open('w') as se:
            r = subprocess.run(command, stdout=so, stderr=se, timeout=timeout)
        if check: r.check_returncode()
        return r.returncode, (OUT/(name+'.stdout.txt')).read_text()
    def shell(argv, name, **kw): return call(['shell', shlex.join(argv)], name, **kw)
    def kill_exact(pidfile, program, name):
        pid = shell(['cat', pidfile], name+'-timeout-pid', check=False)[1].strip()
        killed = False
        if pid.isdecimal():
            rc, path = shell(['readlink', '/proc/'+pid+'/exe'], name+'-timeout-executable', check=False)
            if rc == 0 and path.strip() == program: shell(['kill', '-KILL', pid], name+'-timeout-cleanup', check=False); killed = True
        left = shell(['ps', '-A', '-o', 'PID,ARGS'], name+'-post-timeout-processes', check=False)[1]
        return dict(pid=pid, killed=killed, remaining=[r for r in left.splitlines() if 'codebrix-c4' in r])
    sources = inputs(); pins = {}; results = {}; failures = []
    try:
        staged = json.loads(staged_path(rid).read_text())
        assert staged['sourceSha256'] == sources, 'Production sources changed since staging'
        expected = {STAGE_REMOTE+'/'+f['remote']: f['sha256'] for f in staged['files']}
        for f in staged['files']: pins[f['path']] = f['sha256']
        meta = {k: shell(c, k)[1].strip() for k, c in {'api': ['getprop', 'ro.build.version.sdk'], 'abi': ['getprop', 'ro.product.cpu.abi'],
                'model': ['getprop', 'ro.product.model'], 'fingerprint': ['getprop', 'ro.build.fingerprint']}.items()}
        assert meta['abi'] == MACHINE[rid][1] and (model is None or meta['model'] == model); state['deviceProperties'] = meta
        processes = shell(['ps', '-A', '-o', 'PID,ARGS'], 'prior-processes')[1]
        assert not any('codebrix_' in l or 'codebrix-c' in l or 'render_replay' in l for l in processes.splitlines()), 'Another codebrix process is running'
        hashes = shell(['sha256sum', *expected], 'device-hashes')[1]
        assert {l.split()[1]: l.split()[0] for l in hashes.splitlines()} == expected, 'Staged device binaries differ'
        shell(['test', '!', '-e', REMOTE], 'unused-remote'); shell(['mkdir', '-p', REMOTE], 'remote-dir')
        state['stagedVerified'] = True
        subst = lambda s: s.replace('{REMOTE}', REMOTE)
        for job in jobs:
            label = job['label']; remaining = deadline-time.time()
            if remaining < 30:
                failures.append(label+': not run (budget exhausted)'); results[label] = dict(passed=False, notRun='budget'); continue
            timeout = min(job.get('timeout', args.job_timeout), remaining)
            for i, probe in enumerate(job.get('pre', [])): call(['shell', subst(probe)], label+'-pre-'+str(i), 120, False)
            state['phase'] = label; save(OUT/'status.json', state); log('START '+label+' timeout=%ds' % timeout)
            program = STAGE_REMOTE+'/bin/codebrix_cycles_'+job['gate']+'_gate'
            jd = REMOTE+'/'+label
            env = ['env', '-u', 'OCIO', 'LD_LIBRARY_PATH='+STAGE_REMOTE+'/lib', 'CODEBRIX_CYCLES_PIPELINE_LOG='+jd+'/pipelines.jsonl']
            if rid == 'android-x64': env.append('MESA_SHADER_CACHE_DISABLE=true')
            if args.pipeline_budget_ms: env.append('CODEBRIX_CYCLES_PIPELINE_BUDGET_MS='+str(args.pipeline_budget_ms))
            env += [k+'='+subst(v) for k, v in job.get('env', {}).items()]
            command = 'mkdir -p '+shlex.quote(jd)+'; echo $$ > '+shlex.quote(jd+'/active.pid')+'; exec '+shlex.join([*env, program, jd+'/run', *job.get('args', [])])
            started = time.time()
            try:
                code, stdout = call(['shell', command], label, timeout, False)
            except subprocess.TimeoutExpired:
                info = kill_exact(jd+'/active.pid', program, label); seconds = round(time.time()-started, 1)
                results[label] = dict(passed=False, timeout=True, wallSeconds=seconds, cleanup=info); save(OUT/'results.json', results)
                failures.append(label+': TIMEOUT after %.0fs (budget)' % seconds); log('TIMEOUT '+label+' %.0fs; killed=%s' % (seconds, info['killed']))
                break
            seconds = round(time.time()-started, 1)
            for i, probe in enumerate(job.get('post', [])): call(['shell', subst(probe)], label+'-post-'+str(i), 120, False)
            (OUT/'output').mkdir(exist_ok=True)
            # Pull small outputs only (logs, reports, films); skip capture arenas.
            call(['shell', 'find '+shlex.quote(jd)+' -name "*.arena.bin" -delete'], label+'-rm-arena', 120, False)
            call(['pull', jd, str(OUT/'output')], label+'-pull', 900, False)
            local = OUT/'output'/label
            plog = local/'pipelines.jsonl'
            pipelines = [json.loads(l) for l in plog.read_text().splitlines() if l.strip()] if plog.exists() else []
            entry = dict(gate=job['gate'], args=job.get('args', []), env=job.get('env', {}), expect=job.get('expect', 'pass'),
                         exit=code, wallSeconds=seconds, pipelineSummary=summarize_pipelines(pipelines))
            stderr = (OUT/(label+'.stderr.txt')).read_text()
            if job.get('expect') == 'crash':
                ok = code != 0 and job.get('crashText', '') in stderr
                if code != 0: call(['logcat', '-d', '-t', '200'], label+'-logcat', 120, False)
                entry.update(passed=ok, stderrTail=stderr.strip().splitlines()[-4:])
                (log('PASS (expected crash) %s exit=%d' % (label, code)) if ok else (failures.append(label+': expected crash not observed'), log('FAIL '+label+' expected crash, exit=%d' % code)))
                results[label] = entry; save(OUT/'results.json', results); continue
            if code != 0:
                call(['logcat', '-d', '-t', '300'], label+'-logcat', 120, False)
                err = stderr.strip().splitlines()
                entry.update(passed=False, error=err[-3:] if err else 'exit '+str(code)); results[label] = entry; save(OUT/'results.json', results)
                failures.append(label+': '+(err[-1] if err else 'exit '+str(code))); log('FAIL '+label+' exit=%d %.0fs: %s' % (code, seconds, err[-1] if err else '')); continue
            report = None
            try:
                report = json.loads(stdout)
                assert report['passed'] and report.get('cyclesAllocationBytesAfterCleanup', 0) == 0
                library = local/'run/library.txt' if job['gate'] in IMAGE_GATES else local/'run.library.txt'
                assert library.read_text().strip() == STAGE_REMOTE+'/lib/libcodebrix_wgpu.so', 'unexpected runtime'
                CHECKS[job['gate']](report)
                pr = report.get('pipelineReport') or {}
                d = pr.get('decision') or {}
                entry.update(passed=True, pipelineReport={k: v for k, v in pr.items() if k != 'decision'}, decision=d,
                             maxError=report.get('maximumAbsoluteImageError', report.get('maximumAbsoluteError')),
                             strict=report.get('strictImageFailures', report.get('strictFailures')))
                for k in ('expectTier', 'expectReason'):
                    if k in job:
                        got = d.get('tier' if k == 'expectTier' else 'reason')
                        assert got == job[k], '%s %r != %r' % (k, got, job[k])
                if job.get('expectNoTierWarning'):
                    assert not pr.get('degraded') and not pr.get('warnings') and not d.get('warnings'), 'tier/budget warning present'
                log('PASS %s %.0fs tier=%s/%s maxPipeline=%.1fs degraded=%s warnings=%d' % (label, seconds, d.get('tier'), d.get('reason'),
                    entry['pipelineSummary']['maxSeconds'], pr.get('degraded'), len(pr.get('warnings') or [])))
            except Exception as error:
                entry.update(passed=False, report=report, assertion=repr(error))
                failures.append(label+': assertion '+repr(error)); log('FAIL '+label+' assertion '+repr(error))
            results[label] = entry; save(OUT/'results.json', results)
        state.update(phase='complete', passed=not failures, failures=failures)
    except BaseException as error:
        state.update(passed=False, error=repr(error)); log('ERROR '+repr(error)); raise
    finally:
        state['complete'] = True; state['wallSeconds'] = round(time.time()-T0, 1); state['withinBudget'] = state['wallSeconds'] <= args.budget+60
        after = subprocess.run([ADB, '-s', serial, 'shell', 'ps -A -o PID,ARGS'], capture_output=True, text=True, timeout=60).stdout
        state['remoteProcessesAfter'] = [l for l in after.splitlines() if 'codebrix-c4' in l or 'codebrix_cycles' in l]
        state['productionSourcesUnchanged'] = sources == inputs()
        state['binaryInputsUnchanged'] = all(sha(Path(p)) == d for p, d in pins.items())
        state['inputsUnchanged'] = state['productionSourcesUnchanged'] and state['binaryInputsUnchanged']
        if not state['inputsUnchanged']: state['passed'] = False
        save(OUT/'status.json', state); save(OUT/'results.json', results)
        log('END passed=%s wall=%.0fs/%ds failures=%d' % (state.get('passed'), state['wallSeconds'], args.budget, len(failures)))
    print(json.dumps({k: state.get(k) for k in ['passed', 'failures', 'error', 'wallSeconds']}), flush=True)

if __name__ == '__main__':
    if sys.argv[1] == 'review': review(sys.argv[2], sys.argv[3])
    elif sys.argv[1] == 'push': push(sys.argv[2])
    else:
        ap = argparse.ArgumentParser(); ap.add_argument('cmd'); ap.add_argument('device', choices=DEVICES); ap.add_argument('plan'); ap.add_argument('label')
        ap.add_argument('budget', type=int); ap.add_argument('job_timeout', type=int); ap.add_argument('--pipeline-budget-ms', type=int)
        run(ap.parse_args())
