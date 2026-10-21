#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 2 copy (via Phase E1) of the Phase C2 x64 copy of B3 probe.py (paths; Linux runs get a fresh scratch Mesa cache).
Measurement-only pipeline-creation probe for Linux, HP (x64) and Pixel (arm64).

Each (module, entry) pair runs in its own process so VmHWM is that pipeline's peak.
On Android a sampler also records VmHWM/VmRSS from /proc/<pid>/status every second.
Never relaxes any criterion: it only times compilation of unchanged modules.
"""
import argparse, hashlib, json, shlex, subprocess, sys, time
from pathlib import Path
HERE = Path(__file__).resolve().parent
WT = HERE.parents[3]  # merge wave 2 copy: repository root
sys.path.insert(0, str(WT/'native/tools'))
PACKAGE = Path.home()/'.nuget/packages/codebrix.webgpu.native.mitlicenseforever/1.0.290.1433'
NDK = Path.home()/'Android/Sdk/ndk/29.0.14206865/toolchains/llvm/prebuilt/linux-x86_64/bin'
ADB = Path.home()/'Android/Sdk/platform-tools/adb'
BUILD = WT/'native/work/merge2/probe-build'
DEVICES = {'hp': ('android-x64', '192.168.86.225:5555'), 'samsung': ('android-arm64', 'R52W207XQ1V'), 'pixel': ('android-arm64', '98271FFBA0048D'), 'linux': ('linux-x64', None)}

def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def build(rid):
    out = BUILD/rid; out.mkdir(parents=True, exist_ok=True)
    cxx = {'linux-x64': 'clang++', 'android-arm64': str(NDK/'aarch64-linux-android33-clang++'), 'android-x64': str(NDK/'x86_64-linux-android33-clang++')}[rid]
    cmd = [cxx, '-std=c++20', '-O2', '-I', str(WT/'native/src'), '-I', str(WT/'native/include'), '-I', str(PACKAGE/'include'),
           str(HERE/'compile_probe.cpp'), str(WT/'native/src/webgpu_device.cpp'), '-L', str(PACKAGE/'runtimes'/rid/'native'),
           '-lcodebrix_wgpu', '-ldl', '-pthread', '-Wl,-rpath,$ORIGIN', '-o', str(out/'compile_probe')]
    if rid.startswith('android'): cmd.append('-static-libstdc++')
    subprocess.run(cmd, check=True)
    lib = PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so'
    (out/lib.name).write_bytes(lib.read_bytes())
    assert sha(out/lib.name) == sha(lib)
    return out

def adb(serial, *args, timeout=120, check=True):
    return subprocess.run([str(ADB), '-s', serial, *args], capture_output=True, text=True, timeout=timeout, check=check)

def run(device, wgsl, entry, timeout, mode='default', env=None, tag=''):
    rid, serial = DEVICES[device]
    binary = BUILD/rid
    started = time.time()
    if serial is None:
        command = ['/usr/bin/time', '-v', str(binary/'compile_probe'), str(wgsl), entry, mode]
        import os
        e = dict(os.environ); e.update(env or {})
        import tempfile
        e['MESA_SHADER_CACHE_DIR'] = tempfile.mkdtemp(prefix='mesa-probe-', dir=str(WT/'native/work/merge2'))
        try:
            r = subprocess.run(command, capture_output=True, text=True, timeout=timeout, env=e)
            out = r.stdout.strip().splitlines()
            rec = json.loads(out[-1]) if out else {'passed': False, 'error': r.stderr[-2000:]}
            for line in r.stderr.splitlines():
                if 'Maximum resident set size' in line: rec['timeMaxRSSkB'] = int(line.split()[-1])
            if r.returncode and rec.get('passed'): rec['passed'] = False
            rec['returncode'] = r.returncode
            if r.returncode and r.returncode != 1: rec['stderrTail'] = r.stderr[-1500:]
        except subprocess.TimeoutExpired:
            rec = {'entry': entry, 'passed': False, 'timeout': True, 'timeoutSeconds': timeout}
    else:
        remote = '/data/local/tmp/codebrix-mw2-probe'
        adb(serial, 'shell', 'mkdir -p '+remote)
        for p in [binary/'compile_probe', binary/'libcodebrix_wgpu.so']:
            adb(serial, 'push', str(p), remote+'/'+p.name, timeout=300)
        remote_wgsl = remote+'/module.wgsl'
        adb(serial, 'push', str(wgsl), remote_wgsl, timeout=300)
        adb(serial, 'shell', 'chmod 700 '+remote+'/compile_probe')
        assert adb(serial, 'shell', 'sha256sum '+remote+'/libcodebrix_wgpu.so').stdout.split()[0] == sha(PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so')
        assert adb(serial, 'shell', 'sha256sum '+remote_wgsl).stdout.split()[0] == sha(wgsl)
        # Launch in background on the device, sampling VmHWM until exit or budget.
        # Detach the subshell's stdio so adb returns immediately (else adb waits for it).
        inner = 'cd %s; LD_LIBRARY_PATH=%s MESA_SHADER_CACHE_DISABLE=true ./compile_probe module.wgsl %s %s > out.txt 2> err.txt; echo $? > rc.txt' % (remote, remote, shlex.quote(entry), shlex.quote(mode))
        script = ('cd %s; rm -f out.txt err.txt pid.txt; setsid sh -c %s > /dev/null 2>&1 < /dev/null & '
                  'sleep 0.5; pidof compile_probe > pid.txt; cat pid.txt') % (remote, shlex.quote(inner))
        assert not adb(serial, 'shell', 'pidof compile_probe', check=False).stdout.strip(), 'A probe is already running on this device'
        adb(serial, 'shell', 'rm -f %s/rc.txt' % remote)
        pid = adb(serial, 'shell', script).stdout.strip().split()
        samples = []; peak = 0; rss_peak = 0
        deadline = started+timeout
        while time.time() < deadline:
            st = adb(serial, 'shell', 'cat /proc/%s/status 2>/dev/null | grep -E "VmHWM|VmRSS"; cat %s/rc.txt 2>/dev/null' % (pid[0] if pid else '0', remote), check=False).stdout
            hwm = [int(l.split()[1]) for l in st.splitlines() if l.startswith('VmHWM')]
            rss = [int(l.split()[1]) for l in st.splitlines() if l.startswith('VmRSS')]
            if hwm: peak = max(peak, hwm[0])
            if rss: rss_peak = max(rss_peak, rss[0]); samples.append([round(time.time()-started, 1), rss[0]])
            if st.strip().splitlines() and st.strip().splitlines()[-1].strip().lstrip('-').isdigit() and not hwm:
                break
            time.sleep(1.0)
        rc = adb(serial, 'shell', 'cat %s/rc.txt 2>/dev/null' % remote, check=False).stdout.strip()
        if rc == '':
            # Budget exceeded: kill the remote process and verify it is gone.
            if pid: adb(serial, 'shell', 'kill -KILL %s' % pid[0], check=False)
            time.sleep(1)
            left = adb(serial, 'shell', 'ps -A -o PID,ARGS | grep compile_probe | grep -v grep', check=False).stdout.strip()
            rec = {'entry': entry, 'passed': False, 'timeout': True, 'timeoutSeconds': timeout, 'remoteProcessRemaining': left}
        else:
            # Adapter names (Adreno) contain newlines: escape them before parsing.
            out = adb(serial, 'shell', 'cat %s/out.txt' % remote).stdout.strip()
            err = adb(serial, 'shell', 'cat %s/err.txt' % remote).stdout
            rec = json.loads(out.replace('\r', '').replace('\n', '\\n')) if out else {'entry': entry, 'passed': False, 'error': 'no output (crash?)', 'stderrTail': err[-1500:]}
            rec['returncode'] = int(rc)
            if int(rc) not in (0, 1): rec['passed'] = False; rec['stderrTail'] = err[-1500:]
        rec['sampledPeakVmHWMkB'] = peak; rec['sampledPeakVmRSSkB'] = rss_peak; rec['rssSamples'] = samples[-120:]
    rec.update({'device': device, 'wgsl': str(wgsl), 'wgslSha256': sha(wgsl), 'wallSeconds': round(time.time()-started, 2), 'tag': tag, 'mode': mode})
    return rec

if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('action', choices=['build', 'run'])
    ap.add_argument('--device', choices=list(DEVICES))
    ap.add_argument('--rid')
    ap.add_argument('--wgsl', type=Path); ap.add_argument('--entry', action='append')
    ap.add_argument('--timeout', type=int, default=600); ap.add_argument('--mode', default='default')
    ap.add_argument('--tag', default=''); ap.add_argument('--log', type=Path)
    ap.add_argument('--env', action='append', default=[])
    a = ap.parse_args()
    if a.action == 'build': print(build(a.rid)); sys.exit(0)
    env = dict(x.split('=', 1) for x in a.env)
    for entry in a.entry:
        rec = run(a.device, a.wgsl.resolve(), entry, a.timeout, a.mode, env, a.tag)
        line = json.dumps(rec)
        print(line, flush=True)
        if a.log:
            with a.log.open('a') as f: f.write(line+'\n')
