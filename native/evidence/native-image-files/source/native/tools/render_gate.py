#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Run, collect and freeze the bounded native Cycles scheduler rendering proof."""
import argparse
import concurrent.futures
import datetime
import gzip
import json
import shutil
import struct
import subprocess
from pathlib import Path
from build import ROOT, REPO, WORK, REVISION, PACKAGE, audit
from build_cycles import UPSTREAM, verify_cycles
from dependency_sources import verify as verify_dependencies
from device_gate import sha, save
import render_replay
import svm_surface_gate

OUT = WORK/'render'
DEST = ROOT/'evidence/native-render'
SCENES = ['linux-x64','bench']
DEVICES = [('linux-x64',None,'linux'),('android-arm64','98271FFBA0048D','pixel'),
           ('android-arm64','R52W207XQ1V','samsung'),('android-x64','192.168.86.225:5555','hp')]
REGRESSIONS = ['device-queue','camera-queue','closest-queue','background-queue',
               'svm-world','shader-data','svm-surface']


def inputs():
    verify_cycles()
    verify_dependencies()
    paths = [REPO/n for n in ['AGENTS.md','README.md','PARITY.md','LICENSE',
                             'NOTICE','RELEASE-LICENSE-PLAN.md','THIRD-PARTY-NOTICES.txt']]
    for top in ['native','research','prototype','parity','third_party','tools']:
        paths += [p for p in (REPO/top).rglob('*') if p.is_file() and not any(
            n in ('work','evidence','__pycache__','bin','obj') for n in p.relative_to(REPO).parts)]
    return {str(p.relative_to(REPO)):sha(p) for p in sorted(set(paths))}


def execute(name, executable, args=()):
    folder = OUT/name
    folder.mkdir(parents=True,exist_ok=True)
    command = [str(executable),str(folder/'run'),*map(str,args)]
    with (folder/'run.stdout.txt').open('w') as stdout, (folder/'run.stderr.txt').open('w') as stderr:
        subprocess.run(command,stdout=stdout,stderr=stderr,check=True,timeout=300)
    save(folder/'invocation.json', {'command':command,'executableSha256':sha(executable)})


def run():
    svm_surface_gate.run(android=True)
    for scene in SCENES:
        execute(scene,WORK/'cycles-build/bin/codebrix_cycles_render_gate',
                [OUT/'bench/triangles.bin'] if scene == 'bench' else [])
    execute('surface',WORK/'cycles-build/bin/codebrix_cycles_surface_gate')
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        for future in [pool.submit(render_replay.build,rid) for rid in sorted({d[0] for d in DEVICES})]:
            future.result()
    def replay_device(rid,serial,label):
        for scene in SCENES:
            render_replay.run(rid,OUT/scene,OUT/'replay'/scene/label,serial)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for future in [pool.submit(replay_device,*device) for device in DEVICES]:
            future.result()
    collect()


def readback_rejection():
    source = OUT/'linux-x64'
    folder = OUT/'readback-rejection'
    folder.mkdir(exist_ok=True)
    for name in render_replay.INPUTS:
        if name != 'run.commands.bin':
            shutil.copy2(source/name,folder/name)
    commands = bytearray((source/'run.commands.bin').read_bytes())
    cursor = 0
    while cursor < len(commands):
        kind,address,length = struct.unpack_from('<III',commands,cursor)
        cursor += 12
        if kind == 4:
            assert length > 0
            commands[cursor] ^= 1
            break
        cursor += length if kind == 1 else 128 if kind == 3 else 0
    else:
        raise RuntimeError('Capture has no scheduling readback')
    (folder/'run.commands.bin').write_bytes(commands)
    command = [str(render_replay.BASE/'linux-x64/render_replay'),str(folder),str(folder/'rejected')]
    result = subprocess.run(command,capture_output=True,text=True,timeout=60)
    (folder/'run.stdout.txt').write_text(result.stdout)
    (folder/'run.stderr.txt').write_text(result.stderr)
    assert result.returncode != 0 and 'Native scheduler decision readback differs' in result.stderr
    report = {'passed':True,'wrongExpectedReadbackRejected':True,'changedByteOffset':cursor,
              'originalCommandSha256':sha(source/'run.commands.bin'),
              'mutatedCommandSha256':sha(folder/'run.commands.bin'),'returnCode':result.returncode,
              'driverSha256':sha(render_replay.BASE/'linux-x64/render_replay')}
    save(folder/'validation.json',report)
    return report


def collect():
    """Validate existing working results; do not rerun already-passing GPU tests."""
    verify_cycles()
    save(OUT/'webgpu-compatibility.json',audit(allow_rust_documentation_change=True))
    package = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    reports = {}
    for scene in SCENES:
        directory = OUT/scene
        report = json.loads((directory/'run.stdout.txt').read_text())
        assert all(report[k] for k in ['passed','nativeSceneAndScheduler','independentCpuImage','repeatBitwiseIdentical'])
        assert report['cyclesAllocationBytesAfterCleanup'] == 0 and report['maxBounce'] >= 3
        assert (report['width'],report['height'],report['samples']) == (64,48,8)
        if scene == 'linux-x64':
            assert report['surfaceShaders'] >= 20 and report['maxTransparentBounce'] >= 1
            assert report['equivalentSharedEdgeComparisons'] == 0
        edges = json.loads((directory/'run.shared-edges.json').read_text())
        assert len(edges) == report['equivalentSharedEdgeComparisons']
        assert Path((directory/'run.library.txt').read_text().strip()).resolve() == package.resolve()
        for rid,serial,label in DEVICES:
            replay = OUT/'replay'/scene/label
            current = render_replay.validate(directory,replay)
            saved = json.loads((replay/'validation.json').read_text())
            assert all(saved[k] == v for k,v in current.items()), 'Changed replay evidence'
            assert saved['rid'] == rid and saved['serial'] == serial
            assert saved['webGpuSha256'] == sha(PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so')
            assert saved['driverSha256'] == sha(render_replay.BASE/rid/'render_replay')
        reports[scene] = report
    surface = json.loads((OUT/'surface/run.stdout.txt').read_text())
    assert surface['passed'] and surface['originalCpuSurfaceIntegrator']
    assert surface['invalidCasesRejected'] == 20 and surface['cases'] == 19 and surface['paths'] == 18360
    assert surface['cyclesAllocationBytesAfterCleanup'] == 0
    regression_reports = {}
    for name in REGRESSIONS:
        regression = json.loads((WORK/name/'linux-x64/validation.json').read_text())
        assert regression['passed'] and regression['matchesPinnedPackage']
        assert regression['driverSha256'] == sha(WORK/'cycles-build/bin'/('codebrix_cycles_'+{
            'device-queue':'device','camera-queue':'camera','closest-queue':'closest',
            'background-queue':'background','svm-world':'svm_world','shader-data':'shader_data',
            'svm-surface':'svm_surface'}[name]+'_gate')), 'Stale regression binary: '+name
        regression_reports[name] = sha(WORK/name/'linux-x64/validation.json')
    negative = readback_rejection()
    report = {'passed':True,'sessionProofComplete':True,'fullCyclesParity':False,
        'scope':'Native Cycles scene preparation and original GPU scheduler drive a repeated complete multi-bounce selected-material render through unchanged shipped WebGPU; full command replay on four GPUs',
        'cyclesRevision':REVISION,'nativeResults':reports,'surfaceKernel':surface,'replayDevices':DEVICES,
        'replayScope':'Android runs recorded native commands and checks every scheduling readback; native Android Cycles hosts remain pending',
        'readbackRejection':negative,'regressionReportSha256':regression_reports,
        'absoluteTolerance':5e-5,'relativeTolerance':2e-4,'strictAbsoluteTolerance':1e-7,'strictRelativeTolerance':1e-7,
        'strictNumericalParityPassed':False,'sharedEdgePolicy':'Only constant-clay, flat-normal shared edges with equal object, shader, position and geometric normal; exceptions recorded separately',
        'webGpuPackage':'CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433',
        'sourceSha256':inputs(),'nativeExecutableSha256':{n:sha(WORK/'cycles-build/bin'/n) for n in
            ['codebrix_cycles_render_gate','codebrix_cycles_surface_gate']},
        'licenseStatus':'Filtered Cycles source and independent Apache-2.0 host atomics; see current licensing audit for dependency and package scope',
        'assetGeometrySha256':sha(OUT/'bench/triangles.bin'),'assetMetadataSha256':sha(OUT/'bench/triangles.bin.json')}
    save(OUT/'validation.json',report)
    print(json.dumps({k:v for k,v in report.items() if k != 'sourceSha256'},indent=2))


def archive():
    report = json.loads((OUT/'validation.json').read_text())
    assert report['passed'] and report['sourceSha256'] == inputs(), 'Missing/stale collected proof'
    assert not DEST.exists(), 'Never overwrite a frozen checkpoint'
    for name,digest in report['nativeExecutableSha256'].items():
        assert sha(WORK/'cycles-build/bin'/name) == digest
    DEST.mkdir(parents=True)
    def copy(source,target):
        target.parent.mkdir(parents=True,exist_ok=True)
        if source.suffix == '.bin':
            target.with_suffix(target.suffix+'.gz').write_bytes(gzip.compress(source.read_bytes(),mtime=0))
        else:
            shutil.copy2(source,target)
    for name in ['validation.json','webgpu-compatibility.json']:
        copy(OUT/name,DEST/name)
    for folder in [*SCENES,'surface','readback-rejection']:
        for path in (OUT/folder).iterdir():
            # Keep original asset files local. The capture includes prepared
            # bench geometry authored by Jeremy; notices record that provenance.
            if path.is_file() and path.name != 'triangles.bin':
                copy(path,DEST/folder/path.name)
    for path in (OUT/'replay').glob('*/*/*'):
        if path.is_file():
            copy(path,DEST/'replay'/path.relative_to(OUT/'replay'))
    for path in render_replay.BASE.glob('*/*.json'):
        copy(path,DEST/'replay-build'/path.relative_to(render_replay.BASE))
    for name in REGRESSIONS:
        for file in ['validation.json','run.stdout.txt','run.stderr.txt']:
            copy(WORK/name/'linux-x64'/file,DEST/'regressions'/name/file)
    for name,digest in report['sourceSha256'].items():
        assert sha(REPO/name) == digest
        # The exact source snapshot is already housed in the repository and
        # verified above; retain its manifest without duplicating 200 MB in
        # each evidence checkpoint. Never apply this to an existing checkpoint.
        if name.startswith('third_party/deps/'):
            continue
        copy(REPO/name,DEST/'source'/name)
    for name in ['cycles-build.json','cycles-webgpu-compatibility.json','cycles-registration.patch']:
        copy(WORK/name,DEST/name)
    for path in (WORK/'cycles-overlay').rglob('*'):
        if path.is_file():
            copy(path,DEST/'generated'/path.relative_to(WORK/'cycles-overlay'))
    provenance = json.loads((REPO/'third_party/provenance.json').read_text())
    for name,digest in provenance['nativeSurfaceDerivations']['sourceSha256'].items():
        assert sha(UPSTREAM/name) == digest
        copy(UPSTREAM/name,DEST/'original-reference'/name)
    commands = json.loads((WORK/'cycles-build/compile_commands.json').read_text())
    save(DEST/'compile-commands.json',[c for c in commands if str(ROOT) in c['file'] or c['file'].endswith('/path_trace_work_gpu.cpp')])
    save(DEST/'manifest.json',{'capturedUtc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'scope':report['scope'],'cyclesRevision':REVISION,
        'dependencyReconstruction':'Use third_party/deps from the repository, verified against source/third_party/dependency-imports.json. Dependency implementation is referenced by hash, not duplicated in this checkpoint.',
        'replayReconstruction':'Decompress every .bin.gz to the corresponding .bin. Each scene directory contains the complete input arena, actual command stream, compiled WGSL and independent CPU film. Replay outputs are complete arenas, hashable against validation.json.',
        'assetProvenance':'Prepared bench geometry: Copyright (c) 2026 Jeremy Ellis and contributors, as recorded in the ForestVillage README and root notices. Original textures/fonts are not included.',
        'savedSha256':{str(p.relative_to(DEST)):sha(p) for p in sorted(DEST.rglob('*')) if p.is_file()}})
    print(DEST)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action',choices=['run','collect','archive'])
    parser.add_argument('--checkpoint',default='native-render',choices=['native-render','license-clean-render'])
    args = parser.parse_args()
    DEST = ROOT/'evidence'/args.checkpoint
    {'run':run,'collect':collect,'archive':archive}[args.action]()
