#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Fingerprints of validation jobs (native/cycles/fingerprint.h says what is hashed).

A job's fingerprint is computed where the job runs, with that platform's arithmetic, without rendering. The record
it goes into and the selection of what must run belong to the maintainer's validation process, not to this tool.

  fingerprint.py jobs --kind corpus|gates                    the job names, one per line
  fingerprint.py build-android --rid android-arm64 --out DIR the oracle runner for an Android host build
  fingerprint.py collect --device PLATFORM --out DIR [--kind corpus|gates]... [--job NAME]... [--build DIR] [--assets DIR]
                         [--repeat N] [--binary-root DIR] [--env K=V]... [--replay-root DIR]
  fingerprint.py replays --root DIR --out FILE               every capture folder under DIR: a replay's inputs are files
  fingerprint.py closure TARGET...                           a gate target's own sources, from the Linux build's dependency files
  fingerprint.py host-code                                   the host sources whose change the fingerprint cannot fully see

PLATFORM: linux (x64 arithmetic), linux-arm64-scalar, tablet, hp. Output: DIR/<job, / as __>.json, one per job, and DIR/collection.json
with the identity block. The caller holds the device lock; this tool takes none. Exit status: 0 when every job has a fingerprint or a recorded reason why not; failures are listed in collection.json.
"""
import argparse, datetime, hashlib, json, os, re, shlex, shutil, subprocess, sys, tempfile, time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]; NATIVE = REPO/'native'; SCENES = NATIVE/'oracle/scenes'
sys.path.insert(0, str(REPO/'tools')); sys.path.insert(0, str(NATIVE/'tools'))
ADB = str(Path.home()/'Android/Sdk/platform-tools/adb')
PLATFORMS = {'linux': dict(rid='linux-x64', arithmetic=None), 'linux-arm64-scalar': dict(rid='linux-x64', arithmetic='arm64-scalar'),
             'tablet': dict(rid='android-arm64', serial='R52W207XQ1V', remote='/data/local/tmp/codebrix-fingerprint-tablet'),
             'hp': dict(rid='android-x64', serial='192.168.86.225:5555', remote='/data/local/tmp/codebrix-fingerprint-hp')}
# The code that turns a scene and a readback into commands, and the scheduler it drives (host_files). A change there can alter a job in ways a
# fingerprint sees only at the visited points (fingerprint.h), so the selection treats it as a decision, never as "unchanged".
def host_files():
    """Every source of the device library and the runner, not the gates' own files, plus the overlay and the pinned scheduler's import record."""
    gate = re.compile(r'_gate\.cpp$|_reference\.|_fixture\.h$|_test\.h$|^gate\.cpp$|^(render_capture|scene_capture|surface_dispatch_capture)\.h$')
    names = ['native/cycles/'+p.name for p in sorted((NATIVE/'cycles').iterdir()) if p.is_file() and not gate.search(p.name)]
    names += [str(p.relative_to(REPO)) for pattern in ['native/src/*', 'native/host/*.cpp', 'native/host/*.inc', 'native/host/*.h'] for p in sorted(REPO.glob(pattern)) if p.is_file()]
    return names+['native/oracle/runner.cpp', 'native/oracle/json.h', 'native/tools/cycles_overlay.py', 'third_party/cycles-import.json']


# As a validation render runs.
THREADS = '4'


def sha(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''): h.update(block)
    return h.hexdigest()


def text_sha(text): return hashlib.sha256(text.encode()).hexdigest()


def host_code():
    files = {name: sha(REPO/name) for name in host_files() if (REPO/name).exists()}
    return dict(sha256=text_sha(json.dumps(files, sort_keys=True)), files=files)


def corpus(assets=None):
    names = sorted(str(p.relative_to(SCENES))[:-5] for p in SCENES.rglob('*.json') if p.name != 'manifest.json' and 'assets' not in p.relative_to(SCENES).parts)
    names = [n for n in names if json.loads((SCENES/(n+'.json')).read_text()).get('cbcOracleScene')]
    if assets: names += ['assets/'+p.stem for p in sorted((SCENES/'assets').glob('bench_01*.json'))]
    return names


def gate_targets(build=None):
    """Every gate binary the Linux build defines (inject_cycles.cmake), from the build tree's targets."""
    build = build or NATIVE/'work/cycles-build'
    return sorted(p.name[:-4] for p in (build/'CMakeFiles').glob('codebrix_cycles_*_gate.dir'))


def closure(target, build=None):
    """The target's own sources: every file of the repository outside third_party that its object files depend on, from the compiler's dependency files.
    The device library every gate links is the host code above."""
    build = build or NATIVE/'work/cycles-build'; files = set(); hosts = set(host_files())
    for dep in (build/'CMakeFiles'/(target+'.dir')).rglob('*.o.d'):
        for word in dep.read_text(errors='replace').replace('\\\n', ' ').split():
            if word.endswith(':') or not word.startswith('/'): continue
            p = Path(word)
            try: rel = p.resolve().relative_to(REPO)
            except ValueError: continue
            if rel.parts[0] == 'third_party' or rel.parts[:2] == ('native', 'work') or str(rel) in hosts: continue
            files.add(str(rel))
    tools = gate_tools(target)
    return {name: sha(REPO/name) for name in sorted(files | set(tools))}


def gate_tools(target):
    short = target[len('codebrix_cycles_'):-len('_gate')]; hits = []
    tools = sorted((NATIVE/'tools').glob('*_gate.py'))
    for t in tools:
        text = t.read_text(errors='replace')
        if target in text or re.search(r"['\"/_]"+re.escape(short)+r"['\"_]", text): hits.append(str(t.relative_to(REPO)))
    return hits or [str(t.relative_to(REPO)) for t in tools]  # no tool names it: any gate tool may run it


def read_job(out, job, wall, process, extra=None):
    """The fingerprint file the process wrote, or the reason there is none."""
    try: rec = json.loads(Path(out).read_text())
    except (OSError, ValueError): rec = dict(fingerprint=None, reason='no fingerprint written')
    rec.update(job=job, wallSeconds=round(wall, 3), exit=process.returncode)
    if process.returncode and rec.get('fingerprint'): rec['processError'] = (process.stderr or '').strip().splitlines()[-1:] or ['exit %d' % process.returncode]
    if not rec.get('fingerprint'): rec['reason'] = rec.get('reason', '')+': '+' '.join((process.stderr or '').strip().splitlines()[-2:])[:400]
    rec.update(extra or {})
    return rec


def job_name(job): return job.replace('/', '__').replace(' ', '_')


class Linux:
    def __init__(self, args, platform):
        self.args = args; self.arithmetic = platform['arithmetic']; self.root = (args.binary_root or NATIVE/'work/cycles-build').resolve()
        self.env = dict(os.environ); self.env.pop('OCIO', None)
        if self.arithmetic: self.env['CODEBRIX_CYCLES_WGSL_ARITHMETIC'] = self.arithmetic
        for e in args.env: k, v = e.split('=', 1); self.env[k] = v
    def scene(self, job, out):
        with tempfile.TemporaryDirectory(dir=str(out)) as d:
            # The bench bundle's raw files are generated, not committed: its scene documents run from the bundle folder.
            scene = Path(self.args.assets)/(job.split('/', 1)[1]+'.json') if job.startswith('assets/') and self.args.assets else SCENES/(job+'.json')
            cmd = [str(self.root/'bin/codebrix_cycles_oracle'), '--scene', str(scene), '--backend', 'webgpu', '--label', 'fingerprint', '--output', d+'/o', '--threads', THREADS]
            f = Path(out)/(job_name(job)+'.json'); f.unlink(missing_ok=True); t = time.time()
            e = dict(self.env, CODEBRIX_CYCLES_FINGERPRINT=str(f), XDG_CACHE_HOME=d+'/cache', MESA_SHADER_CACHE_DIR=str(Path(out)/'mesa'))
            r = subprocess.run(cmd, env=e, capture_output=True, text=True, errors='replace', timeout=self.args.job_timeout)
            return read_job(f, job, time.time()-t, r, dict(sceneSha256=sha(scene)))
    def gate(self, job, out):
        with tempfile.TemporaryDirectory(dir=str(out)) as d:
            f = Path(out)/(job_name(job)+'.json'); f.unlink(missing_ok=True); t = time.time()
            e = dict(self.env, CODEBRIX_CYCLES_FINGERPRINT=str(f), CODEBRIX_CYCLES_FINGERPRINT_KIND='prefix', XDG_CACHE_HOME=d+'/cache', MESA_SHADER_CACHE_DIR=str(Path(out)/'mesa'))
            r = subprocess.run([str(self.root/'bin'/job), d+'/run'], env=e, capture_output=True, text=True, errors='replace', timeout=self.args.job_timeout, cwd=d)
            return read_job(f, job, time.time()-t, r)
    def identity(self):
        probe = subprocess.run(['vulkaninfo', '--summary'], capture_output=True, text=True, errors='replace') if shutil.which('vulkaninfo') else None
        from build import PACKAGE
        return dict(deviceModel='laptop', osBuild=' '.join(os.uname()[2:4]), arithmetic=self.arithmetic or 'x64-sse4.2', runtimeSha256=sha(PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'),
                    driverSummary=[l.strip() for l in (probe.stdout.splitlines() if probe else []) if 'driverInfo' in l or 'driverName' in l], hostLibrarySha256=sha(self.root/'lib/libcodebrix_cycles_host.so'))
    def close(self): return ''


class Android:
    """One staged copy per collection. Every job is one adb shell; the fingerprint file comes back over the same shell."""
    def __init__(self, args, platform):
        self.args = args; self.rid = platform['rid']; self.serial = platform['serial']; self.remote = platform['remote']; self.staged = False; self.gates = set()
    def sh(self, cmd, timeout=600, check=True):
        r = subprocess.run([ADB, '-s', self.serial, 'shell', cmd], capture_output=True, text=True, errors='replace', timeout=timeout)
        if check and r.returncode: raise RuntimeError(f'adb shell failed ({r.returncode}): {cmd}: {r.stderr.strip()}')
        return r
    def push(self, local, remote, timeout=1800): subprocess.run([ADB, '-s', self.serial, 'push', str(local), remote], check=True, capture_output=True, timeout=timeout)
    def stage(self, kinds):
        import android_host_gate
        assert self.sh('test -e '+shlex.quote(self.remote)+' && echo exists || echo free').stdout.strip() == 'free', 'remote folder exists: '+self.remote
        with tempfile.TemporaryDirectory(dir=str(self.args.out)) as d:
            stage = android_host_gate.inspect(self.rid, Path(d))
        self.host = stage
        self.sh(f'mkdir -p {self.remote}/lib {self.remote}/bin {self.remote}/out')
        for name, path in stage.items():
            if '.so' in name: self.push(path, f'{self.remote}/lib/{name}')
        runner = Path(self.args.build).resolve()/'codebrix_cycles_oracle'; self.push(runner, f'{self.remote}/bin/codebrix_cycles_oracle')
        self.sh(f'chmod 755 {self.remote}/bin/*')
        if 'corpus' in kinds:
            self.push(SCENES, self.remote+'/scenes')
            if self.args.assets:
                for f in sorted(Path(self.args.assets).resolve().iterdir()):
                    if f.is_file() and f.suffix != '.json': self.push(f, self.remote+'/scenes/assets/'+f.name)
        self.staged = True
    def run(self, job, out, command, kind_env, extra):
        name = job_name(job); f = Path(out)/(name+'.json'); remote_f = f'{self.remote}/out/{name}.json'; work = f'{self.remote}/out/{name}.d'; pid = work+'.pid'
        cmd = (f'mkdir -p {work}; echo $$ > {pid}; cd {work}; exec env -u OCIO LD_LIBRARY_PATH={self.remote}/lib XDG_CACHE_HOME={self.remote}/cache {kind_env}'
               f'CODEBRIX_CYCLES_FINGERPRINT={remote_f} '+command.format(work=work))
        t = time.time()
        try: r = subprocess.run([ADB, '-s', self.serial, 'shell', cmd], capture_output=True, text=True, errors='replace', timeout=self.args.job_timeout)
        except subprocess.TimeoutExpired:
            p = self.sh('cat '+pid, check=False).stdout.strip()
            if p.isdecimal(): self.sh('kill -KILL '+p, check=False)
            self.sh(f'rm -rf {work} {pid} {remote_f}*', check=False)
            return dict(job=job, fingerprint=None, reason=f'timeout after {self.args.job_timeout} s', wallSeconds=round(time.time()-t, 3), **extra)
        wall = time.time()-t
        got = subprocess.run([ADB, '-s', self.serial, 'exec-out', 'cat '+remote_f], capture_output=True, timeout=120)
        f.write_bytes(got.stdout if got.returncode == 0 else b'')
        self.sh(f'rm -rf {work} {pid} {remote_f}*', check=False)
        return read_job(f, job, wall, r, extra)
    def scene(self, job, out):
        scene = SCENES/(job+'.json')
        return self.run(job, out, f"{self.remote}/bin/codebrix_cycles_oracle --scene {self.remote}/scenes/{job}.json --backend webgpu --label fingerprint --output {{work}}/o --threads "+THREADS, '', dict(sceneSha256=sha(scene)))
    def gate(self, job, out):
        from build import WORK
        local = WORK/'android'/self.rid/'host/build/bin'/job
        if not local.exists(): return dict(job=job, fingerprint=None, reason='not built for '+self.rid)
        self.push(local, f'{self.remote}/bin/{job}'); self.sh(f'chmod 755 {self.remote}/bin/{job}')
        rec = self.run(job, out, f'{self.remote}/bin/{job} {{work}}/run', 'CODEBRIX_CYCLES_FINGERPRINT_KIND=prefix ', {})
        self.sh(f'rm -f {self.remote}/bin/{job}', check=False)
        return rec
    def identity(self):
        prop = lambda n: self.sh('getprop '+n).stdout.strip()
        libs = {n: sha(p) for n, p in getattr(self, 'host', {}).items() if '.so' in n}
        from build import WORK
        pin = json.loads((WORK/'android'/self.rid/'host/invocation.json').read_text())
        return dict(deviceModel=prop('ro.product.model'), osBuild=prop('ro.build.fingerprint'), api=prop('ro.build.version.sdk'), runtimeSha256=pin['runtimeSha256'], dependencyMarkerSha256=pin['dependencyMarkerSha256'],
                    libraries={k: v for k, v in libs.items() if k not in ('libcodebrix_cycles_host.so',)}, hostLibrarySha256=libs.get('libcodebrix_cycles_host.so'))
    def close(self):
        if self.staged: self.sh('rm -rf '+shlex.quote(self.remote), timeout=300, check=False)
        return self.sh('ls -d '+shlex.quote(self.remote)+' 2>/dev/null; ps -A -o PID,ARGS | grep -E "codebrix_cycles_(oracle|[a-z0-9_]+_gate)" | grep -v grep', check=False).stdout.strip()


def collect(args):
    out = args.out.resolve(); out.mkdir(parents=True, exist_ok=True)
    platform = PLATFORMS[args.device]; android = 'serial' in platform
    kinds = args.kind or ['corpus']
    jobs = []
    if args.job:
        for j in args.job: jobs.append(('gates' if j.startswith('codebrix_') else 'corpus', j))
    else:
        if 'corpus' in kinds: jobs += [('corpus', j) for j in corpus(args.assets)]
        if 'gates' in kinds:
            targets = gate_targets()
            if android:
                from build_cycles_android import GATE_TARGETS
                targets = [t for t in targets if t in GATE_TARGETS]
            jobs += [('gates', j) for j in targets]
    ops = Android(args, platform) if android else Linux(args, platform)
    started = datetime.datetime.now().isoformat(timespec='seconds'); t0 = time.time(); results = {}; failures = {}
    gates = {j: closure(j) for k, j in jobs if k == 'gates'}
    code = host_code(); left = ''
    try:
        if android: ops.stage({k for k, _ in jobs})
        for kind, job in jobs:
            if args.budget and time.time()-t0 > args.budget: failures[job] = 'collection budget reached'; continue
            for attempt in range(args.repeat):
                job_out = out/('run-%d' % attempt) if args.repeat > 1 else out; job_out.mkdir(exist_ok=True)
                rec = (ops.gate if kind == 'gates' else ops.scene)(job, job_out)
                if kind == 'gates' and rec.get('fingerprint'):
                    rec['binaryFingerprint'] = rec['fingerprint']; rec['sourceClosureSha256'] = text_sha(json.dumps(gates[job], sort_keys=True))
                    rec['fingerprint'] = text_sha('cycles-fingerprint-gate-v1 '+rec['binaryFingerprint']+' '+rec['sourceClosureSha256'])
                rec['kind'] = kind
                (job_out/(job_name(job)+'.json')).write_text(json.dumps(rec, indent=1)+'\n')
                if not rec.get('fingerprint'): failures[job] = rec.get('reason')
                results.setdefault(job, []).append(dict(fingerprint=rec.get('fingerprint'), wallSeconds=rec.get('wallSeconds'), modulesSha256=rec.get('modulesSha256'), dataSha256=rec.get('dataSha256'), commandsSha256=rec.get('commandsSha256'), sourceClosureSha256=rec.get('sourceClosureSha256'),
                                                       sceneEvaluations=rec.get('sceneEvaluations'), refusals=rec.get('refusals'), stoppedAt=rec.get('stoppedAt'), kind=kind, driver=rec.get('driver'), processError=rec.get('processError')))
            if args.verbose: print(f'[{datetime.datetime.now():%H:%M:%S}] {job} {results[job][-1]["fingerprint"] and results[job][-1]["fingerprint"][:16]} {results[job][-1]["wallSeconds"]} s', flush=True)
        identity = ops.identity()
        driver = next((r['driver'] for v in results.values() for r in v if r.get('driver')), None); identity['driver'] = driver
    finally:
        left = ops.close()
    # Replays: the same hash on every platform, taken here; a capture exists only after the gate that writes it has run.
    if args.replay_root:
        for name, row in replay_rows(args.replay_root.resolve()).items():
            results['replay/'+name] = [dict(fingerprint=row.get('fingerprint'), kind='replays', wallSeconds=0)]
            if not row.get('fingerprint'): failures['replay/'+name] = row.get('reason')
    collection = dict(format='cycles-fingerprint-collection-v1', platform=args.device, started=started, wallSeconds=round(time.time()-t0, 1), identity=identity, hostCode=code, jobs=results, failures=failures, deviceLeft=left, gateClosures=gates)
    (out/'collection.json').write_text(json.dumps(collection, indent=1)+'\n')
    n = sum(1 for v in results.values() if v[-1]['fingerprint']); walls = [r['wallSeconds'] for v in results.values() for r in v if r.get('wallSeconds') is not None]
    print(f'{args.device}: {n} of {len(jobs)} jobs fingerprinted, {len(failures)} without, {round(time.time()-t0, 1)} s, mean {sum(walls)/max(1, len(walls)):.2f} s per job, max {max(walls, default=0):.2f} s; device left: {left!r}')


def build_android(args):
    from build import WORK
    from android_toolchain import AndroidToolchain, TARGETS
    rid = args.rid; tc = AndroidToolchain(rid); out = args.out.resolve(); out.mkdir(parents=True, exist_ok=True); host = WORK/'android'/rid/'host/build'
    command = [str(tc.bin/(TARGETS[rid][1]+'33-clang++')), '-std=c++20', '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(NATIVE/'host'), '-I', str(NATIVE/'oracle'), str(NATIVE/'oracle/runner.cpp'),
               '-L', str(host/'lib'), '-lcodebrix_cycles_host', '-Wl,-z,max-page-size=16384', '-o', str(out/'codebrix_cycles_oracle')]
    subprocess.run(command, check=True)
    print(out/'codebrix_cycles_oracle', sha(out/'codebrix_cycles_oracle'))


def replays(args):
    """A replay runs a recorded command stream: its three inputs are the capture's files, which no device arithmetic touches, so its fingerprint is the hash of those files."""
    rows = replay_rows(args.root.resolve())
    Path(args.out).write_text(json.dumps(dict(format='cycles-fingerprint-replays-v1', root=str(args.root.resolve()), jobs=rows), indent=1)+'\n')
    print(len(rows), 'replay captures', sum(1 for v in rows.values() if v['fingerprint']), 'fingerprinted')


def replay_rows(root):
    rows = {}
    for meta in sorted(root.rglob('run.capture.json')):
        d = meta.parent; names = sorted(p.name for p in d.iterdir() if p.is_file() and (p.name.startswith('run.') or p.suffix == '.wgsl') and not p.name.endswith(('.stdout.txt', '.stderr.txt', '.adapter.txt', '.library.txt', '.result.bin')))
        # A render capture holds a command stream (render_replay); a kernel capture holds one module and its parameters (arena_replay).
        driver = NATIVE/('tests/render_replay.cpp' if 'run.commands.bin' in names else 'tests/arena_replay.cpp')
        if 'run.arena.bin' not in names or not ({'run.commands.bin', 'run.params.bin'} & set(names)): rows[str(d.relative_to(root))] = dict(fingerprint=None, reason='capture without arena or commands'); continue
        files = {n: sha(d/n) for n in names}
        rows[str(d.relative_to(root))] = dict(fingerprint=text_sha('cycles-fingerprint-replay-v1 '+json.dumps(files, sort_keys=True)+' '+sha(driver)), files=files, driverSourceSha256=sha(driver))
    return rows


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter); s = p.add_subparsers(dest='action', required=True)
    a = s.add_parser('jobs'); a.add_argument('--kind', required=True, choices=['corpus', 'gates'])
    a = s.add_parser('build-android'); a.add_argument('--rid', required=True); a.add_argument('--out', type=Path, required=True)
    a = s.add_parser('collect'); a.add_argument('--device', required=True, choices=list(PLATFORMS)); a.add_argument('--out', type=Path, required=True); a.add_argument('--kind', action='append', choices=['corpus', 'gates']); a.add_argument('--job', action='append')
    a.add_argument('--build', type=Path); a.add_argument('--assets', type=Path); a.add_argument('--repeat', type=int, default=1); a.add_argument('--binary-root', type=Path); a.add_argument('--env', action='append', default=[]); a.add_argument('--job-timeout', type=int, default=900); a.add_argument('--budget', type=int); a.add_argument('--verbose', action='store_true'); a.add_argument('--replay-root', type=Path)
    a = s.add_parser('replays'); a.add_argument('--root', type=Path, required=True); a.add_argument('--out', type=Path, required=True)
    a = s.add_parser('closure'); a.add_argument('target', nargs='+')
    s.add_parser('host-code')
    args = p.parse_args()
    if args.action == 'jobs': print('\n'.join(corpus() if args.kind == 'corpus' else gate_targets()))
    elif args.action == 'build-android': build_android(args)
    elif args.action == 'collect': collect(args)
    elif args.action == 'replays': replays(args)
    elif args.action == 'closure': print(json.dumps({t: closure(t) for t in args.target}, indent=1))
    else: print(json.dumps(host_code(), indent=1))


if __name__ == '__main__': main()
