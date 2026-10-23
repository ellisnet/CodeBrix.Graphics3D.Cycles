#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase X1 copy (via Phase E1) of the Phase C2 (ARM64) stage.py, generalised to android-arm64 (Samsung) and android-x64 (HP).

ELF/NEEDED review and staging follow B3's android_validate.py stage_files() (same
assertions); render_replay is built for android-arm64 with the maintained
render_replay.build() into native/work/phaseC2/arm64/replay-build.
Usage: stage.py review | stage.py push DEVICE
"""
import json, re, shlex, shutil, struct, subprocess, sys, datetime
from pathlib import Path
HERE = Path(__file__).resolve().parent; C2 = HERE.parent; REPO = HERE.parents[3]  # C2 = native/work/merge5 (merge wave 5 copy of merge wave 4's stager; push is part 2's)
sys.path.insert(0, str(REPO/'native/tools'))
from build import PACKAGE
from device_gate import sha, save
from render_gate import inputs
from android_toolchain import AndroidToolchain
from build_cycles_android import dependencies
from android_host_gate import PLATFORM
import render_replay
render_replay.BASE = C2/'replay-build'
ADB = '/home/jeremy/Android/Sdk/platform-tools/adb'
DEVICES = {'samsung': 'R52W207XQ1V', 'hp': '192.168.86.225:5555', 'pixel': '98271FFBA0048D', 's20': 'RFCRB0MPHYH'}
RIDS = {'samsung': 'android-arm64', 'hp': 'android-x64', 'pixel': 'android-arm64', 's20': 'android-arm64'}
RUNTIME_SHAS = {'android-arm64': '8113cbfa287b5ffc3d9b3c092ddc5ce543b8390abf05eefdde86cf69792fed52', 'android-x64': 'c2b0105f28ba01068cd9b6af6d560315af90b4b2cab6c40fcb78813bd9bb4380'}
MACHINES = {'android-arm64': 183, 'android-x64': 62}
REMOTE = '/data/local/tmp/codebrix-m5-stage'
def build_dir(rid): return C2/('build-'+rid)/'android'/rid/'host'

def review(RID):
    RUNTIME_SHA = RUNTIME_SHAS[RID]; BUILD = build_dir(RID)
    sources = inputs()
    status = json.loads((BUILD/'status.json').read_text()); inv = json.loads((BUILD/'invocation.json').read_text())
    assert status['complete'] and status['passed'] and inv['sourceSha256'] == sources, 'Android build does not match sources'
    toolchain = AndroidToolchain(RID); assert toolchain.identity() == inv['toolchain']
    prefix = dependencies.prefix(RID)
    runtime = PACKAGE/'runtimes'/RID/'native/libcodebrix_wgpu.so'; assert sha(runtime) == inv['runtimeSha256'] == RUNTIME_SHA
    if not (render_replay.BASE/RID/'render_replay').exists(): render_replay.build(RID)
    replay = render_replay.BASE/RID/'render_replay'
    exes = {p.name: p for p in (BUILD/'build/bin').glob('codebrix_cycles_*_gate')}
    for name, p in exes.items(): assert sha(p) == status['artifacts']['bin/'+name]
    libs = {p.name: p for p in (prefix/'lib').glob('*.so*') if p.is_file()}
    libs.update({runtime.name: runtime, toolchain.runtime.name: toolchain.runtime})
    stage = {**{n: ('lib', p) for n, p in libs.items()}, **{n: ('bin', p) for n, p in exes.items()}, 'render_replay': ('bin', replay)}
    files = []
    for name, (kind, p) in sorted(stage.items()):
        data = p.resolve().read_bytes(); assert data[:6] == b'\x7fELF\x02\x01' and struct.unpack_from('<H', data, 18)[0] == MACHINES[RID]
        dynamic = subprocess.check_output([str(toolchain.bin/'llvm-readelf'), '-d', '--version-info', str(p)], text=True)
        needed = re.findall(r'\(NEEDED\).*?\[(.*?)\]', dynamic)
        assert not set(needed)-set(libs)-PLATFORM, (name, set(needed)-set(libs)-PLATFORM); assert 'GLIBC_' not in dynamic
        files.append(dict(path=str(p), remote=kind+'/'+name, sha256=sha(p), needed=needed, bytes=len(data)))
    save(C2/('staged-binaries-'+RID+'.json'), dict(created=datetime.datetime.now().isoformat(timespec='seconds'), rid=RID,
        ndk=toolchain.identity(), minimumAndroidApi=33, runtime=dict(package='CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433', sha256=RUNTIME_SHA),
        sourceSha256=sources, buildInvocationCommands=inv['commands'], platformPatchSha256=inv['platformPatchSha256'],
        dependencyMarkerSha256=inv['dependencyMarkerSha256'], overlaySha256=inv['overlaySha256'],
        replayBuild=json.loads((render_replay.BASE/RID/'build.json').read_text()),
        elfReview='ELF64 little-endian, e_machine '+str(MACHINES[RID])+', NEEDED closure within staged libs + Android platform set, no GLIBC symbol versions',
        files=files))
    print(len(files), 'files reviewed')

def push(device):
    serial = DEVICES[device]; staged = json.loads((C2/('staged-binaries-'+RIDS[device]+'.json')).read_text())
    assert staged['sourceSha256'] == inputs()
    local = C2/('stage-tmp-'+device)
    if local.exists(): shutil.rmtree(local)
    (local/'lib').mkdir(parents=True); (local/'bin').mkdir()
    expected = {}
    for f in staged['files']:
        assert sha(Path(f['path'])) == f['sha256']
        shutil.copy2(f['path'], local/f['remote']); expected[REMOTE+'/'+f['remote']] = f['sha256']
    adb = [ADB, '-s', serial]
    subprocess.run(adb+['shell', 'rm -rf '+REMOTE], check=True)
    subprocess.run(adb+['push', str(local), REMOTE], check=True, capture_output=True, timeout=1200)
    subprocess.run(adb+['shell', 'chmod 755 '+REMOTE+'/bin/*'], check=True)
    out = subprocess.run(adb+['shell', shlex.join(['sha256sum', *expected])], check=True, capture_output=True, text=True).stdout
    assert {l.split()[1]: l.split()[0] for l in out.splitlines()} == expected
    shutil.rmtree(local)
    save(C2/('staged-'+device+'.json'), dict(serial=serial, remote=REMOTE, verified=datetime.datetime.now().isoformat(timespec='seconds'), sha256=expected))
    print(device, len(expected), 'files staged and verified')

if __name__ == '__main__':
    review(sys.argv[2]) if sys.argv[1] == 'review' else push(sys.argv[2])
