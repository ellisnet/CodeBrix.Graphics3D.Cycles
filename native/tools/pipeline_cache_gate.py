#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Gate for the persistent WebGPU pipeline cache (native/DEVICE.md, "Pipeline cache").

Safety part: one process of the public C consumer (tests/host/pipeline_cache_consumer.c) per scenario.
Missing, truncated, corrupt and mismatched files, another adapter's data as the runtime sees it, a
directory that cannot be used, the size limit, a cache the runtime made invalid, a process that dies
while compiling, processes racing on one directory, the choice of directory and the switch.
Identity part: a state gate and a film gate whose outputs repeat must give the same bits cold (empty cache)
and warm (the cache it filled). Scenes rendered by the oracle runner the same way: their films carry the
run-to-run atomic accumulation (they differ even between repeats in one process), so warm against cold is
judged beside cold against cold of the same binary: 0 practical, the largest difference no larger than the control's
unless every value already meets the strict criterion, and the compiled text identical. Linux runs each with a fresh Mesa cache so that only this cache can make
the warm run warm.

  pipeline_cache_gate.py --output DIR
  pipeline_cache_gate.py --output DIR --device label,rid,serial --runner RUNNER --remote /data/local/tmp/codebrix-...
The caller holds the device lock. Verdict: DIR/status.json ("passed"); the exit code follows it.
"""
import argparse, hashlib, json, os, shlex, shutil, struct, subprocess, sys, time
from array import array
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'tools'))
SCENE_ROOT = ROOT/'oracle/scenes'
SCENES = ['starter/diffuse-triangle', 'linux-gates/broad-lighting-0', 'sampling/adaptive-auto',
          'shadow-catcher/catcher-point-light', 'combinations/camera-motion-ao-node', 'volumes/absorption-point']
ADB = str(Path.home()/'Android/Sdk/platform-tools/adb')
MAGIC = b'CBCPLC01'
HEADER = 8+4+4+8+64


def sha(data): return hashlib.sha256(data).hexdigest()
def parse(blob):
    """The file's parts, or None when it is not intact."""
    if len(blob) < HEADER or blob[:8] != MAGIC: return None
    key, runtime, size = struct.unpack_from('<IIQ', blob, 8)
    if HEADER+key+runtime+size != len(blob): return None
    data = blob[HEADER+key+runtime:]
    if blob[24:88].decode('ascii', 'replace') != sha(data): return None
    return dict(key=blob[HEADER:HEADER+key], runtime=blob[HEADER+key:HEADER+key+runtime], data=data)
def build(key, runtime, data):
    return MAGIC+struct.pack('<IIQ', len(key), len(runtime), len(data))+sha(data).encode()+key+runtime+data


class Local:
    """This machine."""
    def __init__(self, root, build):
        self.root, self.lib = str(root), str(build/'lib')
        self.consumer, self.runner, self.state_gate, self.film_gate = [str(build/'bin'/n) for n in
            ['codebrix_host_pipeline_cache_test', 'codebrix_cycles_oracle', 'codebrix_cycles_shader_data_gate', 'codebrix_cycles_glass_render_gate']]
        self.scenes = str(SCENE_ROOT)
    def run(self, command, env=None, timeout=1800):
        e = {k: v for k, v in os.environ.items() if not k.startswith('CODEBRIX_CYCLES_') and k != 'OCIO'}
        e['LD_LIBRARY_PATH'] = self.lib; e['MESA_SHADER_CACHE_DIR'] = self.path('mesa-'+hashlib.md5(str(time.time_ns()).encode()).hexdigest()[:12])
        for k, v in (env or {}).items():
            if v is None: e.pop(k, None)
            else: e[k] = v
        t = time.time(); r = subprocess.run(command, env=e, capture_output=True, text=True, errors='replace', timeout=timeout)
        return r.returncode, r.stdout, r.stderr, time.time()-t
    def race(self, command, count, env):
        e = {k: v for k, v in os.environ.items() if not k.startswith('CODEBRIX_CYCLES_') and k != 'OCIO'}
        e.update(env, LD_LIBRARY_PATH=self.lib, MESA_SHADER_CACHE_DIR=self.path('mesa-race'))
        running = [subprocess.Popen(command, env=e, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace') for _ in range(count)]
        return [(p.communicate()[0], p.returncode) for p in running]
    def path(self, *parts): return '/'.join([self.root, *parts])
    def read(self, path):
        try: return Path(path).read_bytes()
        except OSError: return None
    def write(self, path, data): Path(path).parent.mkdir(parents=True, exist_ok=True); Path(path).write_bytes(data)
    def names(self, path):
        try: return sorted(p.name for p in Path(path).iterdir())
        except OSError: return []
    def make_old(self, path): os.utime(path, (1000000000, 1000000000))
    def text_hashes(self, path): return {p.name: sha(p.read_bytes()) for p in sorted(Path(path).glob('*.wgsl'))}
    def cache_size(self, path):
        files = [p for p in Path(path).rglob('*.bin') if p.is_file()]
        return len(files), sum(p.stat().st_size for p in files)
    def film_hashes(self, path): return {p.name: sha(p.read_bytes()) for p in sorted(Path(path).glob('film-*'))}
    def remove_all(self): pass


class Device:
    """An attached Android device; everything lives under one remote folder."""
    def __init__(self, serial, rid, remote, local, runner):
        import android_host_gate
        self.adb, self.root, self.local = [ADB, '-s', serial], remote, local
        assert remote.startswith('/data/local/tmp/codebrix-')
        (local/'inspect').mkdir()
        stage = android_host_gate.inspect(rid, local/'inspect')
        assert self.sh('test -e '+shlex.quote(remote)+' && echo exists || echo free')[1].strip() == 'free', 'remote exists'
        pack = local/'stage'; (pack/'lib').mkdir(parents=True); (pack/'bin').mkdir()
        for name, path in stage.items():
            if '.so' in name: shutil.copy2(path, pack/'lib'/name)
        built = ROOT/'work/android'/rid/'host/build/bin'
        for name in ['codebrix_host_pipeline_cache_test', 'codebrix_cycles_shader_data_gate', 'codebrix_cycles_glass_render_gate']: shutil.copy2(built/name, pack/'bin')
        shutil.copy2(runner, pack/'bin/codebrix_cycles_oracle')
        subprocess.run(self.adb+['push', str(pack), remote], check=True, capture_output=True, timeout=1200)
        self.sh('chmod 755 '+remote+'/bin/*')
        expected = {remote+'/lib/'+p.name: sha(p.read_bytes()) for p in (pack/'lib').iterdir()}
        expected.update({remote+'/bin/'+p.name: sha(p.read_bytes()) for p in (pack/'bin').iterdir()})
        got = self.sh('sha256sum '+' '.join(shlex.quote(k) for k in expected), 300)[1]
        assert {l.split()[1]: l.split()[0] for l in got.splitlines()} == expected, 'staged files differ'
        self.staged = expected
        shutil.rmtree(pack)
        subprocess.run(self.adb+['push', str(SCENE_ROOT), remote+'/scenes'], check=True, capture_output=True, timeout=1200)
        self.consumer, self.runner, self.state_gate, self.film_gate = [remote+'/bin/'+n for n in
            ['codebrix_host_pipeline_cache_test', 'codebrix_cycles_oracle', 'codebrix_cycles_shader_data_gate', 'codebrix_cycles_glass_render_gate']]
        self.scenes = remote+'/scenes'
    def sh(self, command, timeout=120):
        r = subprocess.run(self.adb+['shell', command], capture_output=True, text=True, errors='replace', timeout=timeout+60)
        return r.returncode, r.stdout, r.stderr
    def prefix(self, env):
        items = {'LD_LIBRARY_PATH': self.root+'/lib', **(env or {})}
        return 'env -u OCIO '+' '.join(k+'='+shlex.quote(v) for k, v in items.items() if v is not None)+' '
    def run(self, command, env=None, timeout=1800):
        t = time.time()
        r = self.sh(self.prefix(env)+'timeout '+str(timeout)+' '+' '.join(shlex.quote(c) for c in command), timeout)
        return r[0], r[1], r[2], time.time()-t
    def race(self, command, count, env):
        tail = self.prefix(env)+' '.join(shlex.quote(c) for c in command)
        out = self.path('race')
        body = 'mkdir -p '+out+'; '+''.join('('+tail+' > '+out+'/'+str(i)+'.txt 2>&1; echo $? > '+out+'/'+str(i)+'.rc) & ' for i in range(count))+'wait'
        self.sh(body, 600)
        return [(self.sh('cat '+out+'/'+str(i)+'.txt')[1], int(self.sh('cat '+out+'/'+str(i)+'.rc')[1].strip() or 1)) for i in range(count)]
    def path(self, *parts): return '/'.join([self.root, *parts])
    def read(self, path):
        target = self.local/'pulled.bin'
        if target.exists(): target.unlink()
        if subprocess.run(self.adb+['pull', path, str(target)], capture_output=True).returncode: return None
        return target.read_bytes()
    def write(self, path, data):
        target = self.local/'push.bin'; target.write_bytes(data)
        self.sh('mkdir -p '+shlex.quote(os.path.dirname(path)))
        subprocess.run(self.adb+['push', str(target), path], check=True, capture_output=True)
    def names(self, path):
        r = self.sh('ls -1 '+shlex.quote(path)+' 2>/dev/null')
        return sorted(r[1].split()) if r[0] == 0 else []
    def make_old(self, path): self.sh('touch -d 2001-09-09T01:46:40 '+shlex.quote(path))
    def text_hashes(self, path):
        got = self.sh('cd '+shlex.quote(path)+' && sha256sum *.wgsl', 300)[1]
        return {l.split()[1]: l.split()[0] for l in got.splitlines() if l.strip()}
    def cache_size(self, path):
        sizes = self.sh('find '+shlex.quote(path)+' -name "*.bin" -type f -exec stat -c %s {} +', 300)[1].split()
        return len(sizes), sum(int(v) for v in sizes)
    def film_hashes(self, path):
        names = [n for n in self.names(path) if n.startswith('film-')]
        if not names: return {}
        got = self.sh('sha256sum '+' '.join(shlex.quote(path+'/'+n) for n in names), 300)[1]
        return {os.path.basename(l.split()[1]): l.split()[0] for l in got.splitlines()}
    def remove_all(self):
        assert self.root.startswith('/data/local/tmp/codebrix-')
        self.sh('rm -rf '+shlex.quote(self.root))


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--device'); p.add_argument('--runner', type=Path); p.add_argument('--remote')
    p.add_argument('--build', type=Path, default=ROOT/'work/cycles-build')
    p.add_argument('--baseline', type=Path, help='A regression manifest.json whose x64 film hashes the cold and warm films must equal (Linux)')
    p.add_argument('--scene', action='append'); p.add_argument('--no-safety', action='store_true'); p.add_argument('--no-identity', action='store_true')
    p.add_argument('--threads', default='4')
    a = p.parse_args()
    out = a.output.resolve(); out.mkdir(parents=True, exist_ok=False)
    linux = not a.device
    if linux: target = Local(out/'scratch', a.build.resolve()); Path(target.root).mkdir()
    else:
        label, rid, serial = a.device.split(',')
        target = Device(serial, rid, a.remote, out, a.runner.resolve())
    state = dict(complete=False, passed=False, device=a.device or 'linux', checks={}, scenes={}, stateGate={})
    def save(): (out/'status.json').write_text(json.dumps(state, indent=1, default=str)+'\n')
    def log(message):
        print(time.strftime('[%H:%M:%S]'), message, flush=True)
    failures = []
    def check(name, ok, detail=None):
        state['checks'][name] = dict(ok=bool(ok), detail=detail)
        if not ok: failures.append(name)
        log(('ok   ' if ok else 'FAIL ')+name+('' if ok else ' '+json.dumps(detail, default=str)[:400])); save()
    def consumer(how, env=None, backend='gpu', hosts=None, expect=0):
        rc, stdout, stderr, wall = target.run([target.consumer, how, backend]+([str(hosts)] if hosts else []), env, 600)
        reports = []
        for line in stdout.splitlines():
            try: reports.append(json.loads(line)['pipelineCache'])
            except (ValueError, KeyError): pass
        if rc != expect: reports = reports or [dict(error=stderr[-400:])]
        return rc, reports, wall, stderr

    def safety():
        d0 = target.path('s0')
        rc, r, cold_wall, _ = consumer('dir:'+d0)
        offered = rc == 0 and r[0].get('reason') != 'not-offered'
        if not offered:
            check('not offered: same API, not active, no error', rc == 0 and not r[0]['active'], r); return False
        check('absent: active, a miss, saved', rc == 0 and r[0]['active'] and r[0]['misses'] == 1 and r[0]['hits'] == 0 and r[0]['saves'] == 1 and r[0]['directory'] == d0 and r[0]['directorySource'] == 'consumer', r)
        key = r[0]['key']; name = None
        names = target.names(d0+'/'+key)
        if len(names) == 1: name = names[0]
        file = d0+'/'+key+'/'+(name or '')
        check('one file per pipeline, in a folder per adapter', name and key.startswith('wgpu_pipeline_cache_') and name.endswith('.bin'), names)
        good = target.read(file) if name else None
        intact = parse(good) if good else None
        check('the file is whole', intact and intact['key'].decode() == key, name)
        if not intact: return True
        state['controlFileBytes'] = len(good)
        rc, r, warm_wall, _ = consumer('dir:'+d0)
        check('warm: a hit, the file not rewritten', rc == 0 and r[0]['hits'] == 1 and r[0]['misses'] == 0 and r[0]['saves'] == 0 and r[0]['loadedBytes'] == len(intact['data']) and target.read(file) == good, r)
        state['controlWalls'] = dict(cold=cold_wall, warm=warm_wall)
        d = target.path('s1')
        rc, r, _, _ = consumer('dir:'+d, hosts=2)
        check('two hosts in one process: the second hits what the first saved', rc == 0 and len(r) == 2 and r[0]['misses'] == 1 and r[1]['hits'] == 1, r)
        data = intact['data']; runtime = intact['runtime']; keyb = intact['key']
        check('the runtime data carries the header the mismatch cases edit', data[:8] == b'WGPUPLCH', str(data[:8]))
        flip = lambda blob, at: blob[:at]+bytes([blob[at] ^ 0x5a])+blob[at+1:]
        cases = [('zero bytes', b'', 'damaged'), ('truncated in the header', good[:20], 'damaged'), ('truncated in the key', good[:HEADER+3], 'damaged'),
                 ('truncated in the data', good[:len(good)//2], 'damaged'), ('extended', good+b'\0'*16, 'damaged'),
                 ('one flipped data bit', flip(good, len(good)-len(data)//2), 'damaged'), ('flipped magic', flip(good, 0), 'damaged'),
                 ('garbage', hashlib.sha512(good).digest()*(len(good)//64+1), 'damaged'),
                 ('another key in the header', build(keyb[:-1]+b'!', runtime, data), 'otherAdapter'),
                 ('another runtime in the header', build(keyb, runtime+b'x', data), 'otherRuntime'),
                 # Intact files whose runtime data the runtime itself must refuse or drop (wgpu-core's header).
                 ('another adapter as the runtime sees it', build(keyb, runtime, flip(data, 20)), 'rejected'),
                 ('another driver as the runtime sees it', build(keyb, runtime, flip(data, 32)), 'outdated'),
                 ('runtime data truncated', build(keyb, runtime, data[:len(data)-8]), 'outdated'),
                 ('runtime data with another magic', build(keyb, runtime, flip(data, 0)), 'outdated')]
        for number, (label, blob, want) in enumerate(cases):
            d = target.path('case-'+str(number))
            path = d+'/'+key+'/'+name; target.write(path, blob)
            rc, r, _, stderr = consumer('dir:'+d)
            again = target.read(path); fixed = parse(again) if again else None
            ok = rc == 0 and r[0]['active'] and r[0]['ignored'][want] == 1 and r[0]['hits'] == 0 and r[0]['misses'] == 1 and r[0]['saveFailures'] == 0 and fixed and fixed['key'] == keyb and fixed['runtime'] == runtime
            check(label+' is ignored and rebuilt ('+want+')', ok, dict(report=r, stderr=stderr[-300:]))
            rc, r, _, _ = consumer('dir:'+d)
            check(label+': the rebuilt file is a hit', rc == 0 and r[0]['hits'] == 1, r)
        # A file in the way of the directory: no cache, no error.
        target.write(target.path('blocked'), b'x')
        rc, r, _, _ = consumer('dir:'+target.path('blocked', 'under'))
        check('directory that cannot be used: not active, no error', rc == 0 and not r[0]['active'] and r[0]['reason'] == 'directory-unavailable', r)
        # Stale temporaries of a dead process go; a fresh one (another process at work) stays.
        d = target.path('temps'); folder = d+'/'+key
        target.write(folder+'/'+name+'.99.0.tmp', b'old'); target.write(folder+'/'+name+'.98.0.tmp', b'new'); target.make_old(folder+'/'+name+'.99.0.tmp')
        rc, r, _, _ = consumer('dir:'+d)
        left = target.names(folder)
        check('stale temporary removed, fresh one kept', rc == 0 and name+'.99.0.tmp' not in left and name+'.98.0.tmp' in left, left)
        # Size limit: a pipeline bigger than the limit is not kept; the oldest pipelines go first.
        d = target.path('limit'); folder = d+'/'+key
        rc, r, _, _ = consumer('dir:'+d, {'CODEBRIX_CYCLES_PIPELINE_CACHE_MAX_BYTES': '100'})
        check('a pipeline over the limit: not kept, no error', rc == 0 and r[0]['misses'] == 1 and r[0]['saves'] == 0 and not target.names(folder), dict(report=r, left=target.names(folder)))
        target.write(folder+'/'+'0'*32+'.bin', b'x'*len(good)); target.make_old(folder+'/'+'0'*32+'.bin')
        target.write(folder+'/'+'1'*32+'.bin', b'x'*len(good))
        rc, r, _, _ = consumer('dir:'+d, {'CODEBRIX_CYCLES_PIPELINE_CACHE_MAX_BYTES': str(len(good)*5//2)})
        left = target.names(folder)
        check('over the limit: the oldest goes first', rc == 0 and '0'*32+'.bin' not in left and '1'*32+'.bin' in left and name in left, left)
        # A cache that fails the pipeline through it: the device compiles without it.
        d = target.path('invalid')
        rc, r, _, stderr = consumer('dir:'+d, {'CODEBRIX_CYCLES_TEST_PIPELINE_CACHE': 'invalid'})
        check("a failure through the cache is not the consumer's: compiled without it", rc == 0 and not r[0]['active'] and r[0]['reason'] == 'faulted', dict(report=r, stderr=stderr[-300:]))
        # A process that dies while creating a pipeline takes that pipeline's file with it.
        d = target.path('crash'); folder = d+'/'+key; target.write(folder+'/'+name, good); target.write(folder+'/'+'2'*32+'.bin', good)
        rc, r, _, _ = consumer('dir:'+d, {'CODEBRIX_CYCLES_TEST_CRASH_PIPELINE': 'control'}, expect=None)
        left = target.names(folder)
        check('a process dying while compiling removes that pipeline\'s file only', rc != 0 and name not in left and '2'*32+'.bin' in left, dict(rc=rc, left=left))
        rc, r, _, _ = consumer('dir:'+d)
        check('the next process compiles it again', rc == 0 and r[0]['misses'] == 1, r)
        # Processes sharing a directory, all cold, all writing.
        for round in range(5):
            d = target.path('race-'+str(round)); folder = d+'/'+key
            results = target.race([target.consumer, 'dir:'+d, 'gpu'], 4, {})
            blob = target.read(folder+'/'+name); left = target.names(folder)
            whole = parse(blob) if blob else None
            codes = [c for _, c in results]
            check('four processes racing, round '+str(round)+': no error, one whole file, no temporaries', all(c == 0 for c in codes) and whole and left == [name], dict(codes=codes, left=left))
        # The switch and the choice of directory.
        d = target.path('switch')
        rc, r, _, _ = consumer('off'); check('off: not active', rc == 0 and not r[0]['active'] and r[0]['reason'] == 'off', r)
        rc, r, _, _ = consumer('dir:'+d, {'CODEBRIX_CYCLES_PIPELINE_CACHE': 'off'})
        check('the environment switch beats the API directory', rc == 0 and r[0]['reason'] == 'off' and not target.names(d), r)
        rc, r, _, _ = consumer('off', {'CODEBRIX_CYCLES_PIPELINE_CACHE_DIR': d})
        check('the environment directory beats the API switch', rc == 0 and r[0]['active'] and r[0]['directorySource'] == 'environment' and r[0]['directory'] == d, r)
        rec, own = target.path('rec'), target.path('own')
        rc, r, _, _ = consumer('record:'+rec); check('the record directory is the cache directory', rc == 0 and r[0]['directory'] == rec and r[0]['directorySource'] == 'record-directory', r)
        rc, r, _, _ = consumer('record-dir:'+rec+':'+own); check('a cache directory beats the record directory', rc == 0 and r[0]['directory'] == own and r[0]['directorySource'] == 'consumer', r)
        rc, r, _, _ = consumer('record-off:'+rec); check('off beats the record directory', rc == 0 and r[0]['reason'] == 'off', r)
        rc, r, _, _ = consumer('none', backend='cpu'); check('the CPU device has no cache', rc == 0 and not r[0]['active'] and r[0]['reason'] == 'backend', r)
        # The default.
        if linux:
            root = target.path('xdg')
            rc, r, _, _ = consumer('none', {'XDG_CACHE_HOME': root})
            check('default: $XDG_CACHE_HOME/cycles/webgpu-pipelines', rc == 0 and r[0]['directory'] == root+'/cycles/webgpu-pipelines' and r[0]['directorySource'] == 'user-cache', r)
            home = target.path('home')
            for value in (None, '', 'relative/path'):
                rc, r, _, _ = consumer('none', {'XDG_CACHE_HOME': value, 'HOME': home})
                check('default with XDG_CACHE_HOME='+repr(value)+': ~/.cache/cycles/webgpu-pipelines', rc == 0 and r[0]['directory'] == home+'/.cache/cycles/webgpu-pipelines', r)
            # The tests' known state: nothing touches the user's cache unless a run asks.
            quiet = target.path('quiet')
            rc, _, _, _ = target.run([target.state_gate, target.path('quiet-gate')], {'XDG_CACHE_HOME': quiet, 'HOME': quiet}, 600)
            check('a gate program does not cache by default', rc == 0 and not target.names(quiet), target.names(quiet))
            rc, _, _, _ = target.run([target.runner, '--scene', str(SCENE_ROOT/'starter/diffuse-triangle.json'), '--backend', 'webgpu', '--label', 'x', '--output', target.path('quiet-runner'), '--threads', a.threads],
                                     {'XDG_CACHE_HOME': quiet, 'HOME': quiet}, 600)
            check('the oracle runner does not cache by default', rc == 0 and not target.names(quiet), target.names(quiet))
        else:
            rc, r, _, _ = consumer('none'); check('Android without a directory: no cache, no error', rc == 0 and not r[0]['active'] and r[0]['reason'] == 'no-directory', r)
        return True

    def differences(first, second):
        """Film bytes of two runs: judged by the practical criterion, strict and the largest difference reported."""
        x, y = array('f'), array('f'); x.frombytes(first); y.frombytes(second)
        if len(x) != len(y): return dict(values=-1, practicalFailures=1, strict=0, maxAbs=float('inf'), bitsDiffer=-1)
        bad = strict = differ = 0; largest = 0.0
        for u, v in zip(x, y):
            d = abs(u-v)
            if u != u or v != v or d == float('inf'): bad += 1; continue
            if d > 5e-5+2e-4*abs(v): bad += 1
            if d > 1e-7+1e-7*abs(v): strict += 1
            if d != 0: differ += 1
            largest = max(largest, d)
        return dict(values=len(x), practicalFailures=bad, strict=strict, maxAbs=largest, valuesDiffering=differ)

    def identity(baseline):
        # Control (films are not bit-repeatable, even within one process): warm against cold beside cold against
        # cold of the same binary. Warm differences must be 0 practical and the largest no larger than the control's, whose pool
        # is the same film of two cold runs and the first film of each cold run against the second of each. The second belongs:
        # a first render that compiled nothing lands on other work chunks than one that did, with no pipeline cache
        # involved (shown with Mesa's cache alone, native/DEVICE.md "Pipeline cache"), and a warm first render is such
        # a render. The same-film numbers are recorded apart. A warm number above the pool gets one more cold run.
        # A film with no strict difference is within the control whatever its largest difference: that comparison
        # applies from the strict criterion up. The number of values past strict is not compared: it is rounding noise
        # (5 to 8 on shadow-catcher/catcher-point-light across runs of one binary). The practical check is separate and unchanged.
        # The compiled text must be identical byte for byte.
        for scene in a.scene or SCENES:
            stem = scene.replace('/', '__'); rows, entry = {}, dict()
            state['scenes'][scene] = entry
            def render(name, directory):
                film = target.path('film', stem, name); pipelines = film+'.pipelines.jsonl'; dump = film+'.dump'
                target.write(pipelines+'.keep', b''); target.write(dump+'/.keep', b'')
                env = {'CODEBRIX_CYCLES_PIPELINE_LOG': pipelines, 'CODEBRIX_CYCLES_WGSL_DUMP': dump, 'CODEBRIX_CYCLES_PIPELINE_CACHE_DIR': directory}
                rc, stdout, stderr, wall = target.run([target.runner, '--scene', target.scenes+'/'+scene+'.json', '--backend', 'webgpu', '--label', 'wgsl', '--output', film, '--threads', a.threads], env, 1800)
                lines = (target.read(pipelines) or b'').decode().splitlines()
                ms = [json.loads(l)['pipelineMs'] for l in lines]
                hits = sum(1 for l in lines if json.loads(l).get('cacheHit'))
                row = dict(rc=rc, wallSeconds=round(wall, 1), pipelines=len(ms), cacheHits=hits, compileSeconds=round(sum(ms)/1000, 2), maxPipelineSeconds=round(max(ms or [0])/1000, 2),
                           films=target.film_hashes(film), modules=target.text_hashes(dump), stderr=stderr[-300:] if rc else '')
                marker = stderr.find('"pipelineCache":')
                if marker >= 0:
                    try: row['cache'] = json.loads(stderr[marker+16:stderr.index('\n', marker)])
                    except ValueError: pass
                if lines:
                    worst = max((json.loads(l) for l in lines), key=lambda r: r['pipelineMs']); row['maxEntry'] = worst['entry']
                rows[name] = row; entry[name] = row; save(); log(scene+' '+name+': wall '+str(row['wallSeconds'])+' s, compile '+str(row['compileSeconds'])+' s, hits '+str(hits)+'/'+str(len(ms))+', rc '+str(rc))
                return film
            cache = target.path('cache', stem)
            cold = render('cold', cache)
            entry['cacheFiles'], entry['cacheBytes'] = target.cache_size(cache)
            warm = render('warm', cache)
            control = render('cold-control', target.path('cache', stem+'-control'))
            ok = all(rows[k]['rc'] == 0 and rows[k]['films'] for k in rows)
            check(scene+': all runs rendered', ok, {k: rows[k]['stderr'] for k in rows})
            if not ok: continue
            check(scene+': the warm run hit every pipeline, the cold runs none', rows['cold']['cacheHits'] == 0 and rows['cold-control']['cacheHits'] == 0 and rows['warm']['cacheHits'] == rows['warm']['pipelines'] > 0, {k: (rows[k]['cacheHits'], rows[k]['pipelines'], rows[k]['compileSeconds']) for k in rows})
            check(scene+': the compiled text is identical', rows['cold']['modules'] and rows['cold']['modules'] == rows['warm']['modules'] == rows['cold-control']['modules'], {k: len(rows[k]['modules']) for k in rows})
            def films(path): return {n: target.read(path+'/'+n) for n in rows['cold']['films']}
            cold_films, warm_films, control_films = films(cold), films(warm), films(control)
            warm_pair = {n: differences(warm_films[n], cold_films[n]) for n in cold_films}
            pairs = [{n: differences(control_films[n], cold_films[n]) for n in cold_films}]
            names = sorted(cold_films)
            render_pairs = [{n: differences(x[names[0]], y[names[-1]]) for n in cold_films} for x, y in
                            ((cold_films, cold_films), (cold_films, control_films), (control_films, cold_films), (control_films, control_films))]
            def within(pool):
                # A film whose differences all meet the strict criterion is within the control: below it the largest difference is rounding noise, one float step apart between runs.
                return all(warm_pair[n]['strict'] == 0 or warm_pair[n]['maxAbs'] <= max(p[n]['maxAbs'] for p in pool) for n in cold_films)
            entry['withinSameFilmControl'] = within(pairs)
            if not within(pairs+render_pairs):
                third = films(render('cold-control-2', target.path('cache', stem+'-control-2')))
                pairs += [{n: differences(third[n], cold_films[n]) for n in cold_films}, {n: differences(third[n], control_films[n]) for n in cold_films}]
                entry['withinSameFilmControl'] = within(pairs)
            entry['firstAgainstSecondCold'] = render_pairs
            entry['warmAgainstCold'] = warm_pair; entry['coldAgainstCold'] = pairs; entry['identicalBits'] = rows['cold']['films'] == rows['warm']['films']
            if baseline:
                base = {os.path.basename(k): v for k, v in baseline['results']['x64'][scene]['films'].items()}
                entry['coldEqualsBaseline'] = rows['cold']['films'] == base; entry['warmEqualsBaseline'] = rows['warm']['films'] == base
            practical_ok = all(v['practicalFailures'] == 0 for v in warm_pair.values()) and all(v['practicalFailures'] == 0 for p in pairs+render_pairs for v in p.values())
            check(scene+': warm against cold, 0 practical', practical_ok, dict(warm=warm_pair, control=pairs, firstAgainstSecond=render_pairs))
            check(scene+': warm differences no larger than cold against cold', within(pairs+render_pairs), dict(warm=warm_pair, control=pairs, firstAgainstSecond=render_pairs))
        total = lambda key, rows: sum(v[key] for row in rows for v in row.values())
        state['totals'] = dict(warmStrict=total('strict', [e['warmAgainstCold'] for e in state['scenes'].values() if 'warmAgainstCold' in e]),
                               controlStrict=total('strict', [e['coldAgainstCold'][0] for e in state['scenes'].values() if 'coldAgainstCold' in e]))

    def state_gate():
        cache = target.path('cache', 'state-gate'); rows = {}
        for name in ('cold', 'warm'):
            rc, stdout, stderr, wall = target.run([target.state_gate, target.path('state-'+name)], {'CODEBRIX_CYCLES_PIPELINE_CACHE_DIR': cache}, 900)
            rows[name] = dict(rc=rc, wallSeconds=round(wall, 1), stdout=stdout.strip(), stderr=stderr[-300:] if rc else '')
            log('shader_data gate '+name+': wall '+str(rows[name]['wallSeconds'])+' s, rc '+str(rc))
        state['stateGate'] = rows; state['stateGateCacheFiles'] = target.names(cache)
        check('state gate: cold and warm pass with the same results', rows['cold']['rc'] == 0 and rows['warm']['rc'] == 0 and rows['cold']['stdout'] == rows['warm']['stdout'] and json.loads(rows['cold']['stdout'])['passed'], rows)
        check('state gate: a run that asks gets a cache', target.cache_size(cache)[0] > 0, target.names(cache))

    def film_gate():
        # glass_render's film does not vary from run to run (native/DEVICE.md), so here the bits themselves are compared.
        cache = target.path('cache', 'film-gate'); rows = {}
        for name, directory in (('cold', cache), ('warm', cache), ('cold-control', cache+'-control')):
            prefix = target.path('glass-'+name, 'run'); target.write(prefix+'.keep', b'')
            rc, stdout, stderr, wall = target.run([target.film_gate, prefix], {'CODEBRIX_CYCLES_PIPELINE_CACHE_DIR': directory}, 1200)
            film = target.read(prefix+'.gpu.film.bin')
            rows[name] = dict(rc=rc, wallSeconds=round(wall, 1), film=sha(film) if film else None, stderr=stderr[-300:] if rc else '')
            log('glass_render gate '+name+': wall '+str(rows[name]['wallSeconds'])+' s, rc '+str(rc)+', film '+str(rows[name]['film'])[:16])
        state['filmGate'] = rows
        check('film gate: all runs pass', all(r['rc'] == 0 and r['film'] for r in rows.values()), rows)
        check('film gate: the film repeats (control), and a hit gives the same bits', rows['cold']['film'] == rows['cold-control']['film'] == rows['warm']['film'], rows)

    try:
        offered = True
        if not a.no_safety: offered = safety()
        if offered and not a.no_identity:
            baseline = json.loads(a.baseline.read_text()) if a.baseline else None
            state_gate(); film_gate(); identity(baseline)
        state.update(complete=True, passed=not failures, failures=failures)
    finally:
        save()
        target.remove_all()
    print('PASSED' if not failures and state['complete'] else 'FAILED: '+'; '.join(failures))
    return 0 if state['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
