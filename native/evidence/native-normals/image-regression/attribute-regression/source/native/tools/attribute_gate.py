#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Native static-triangle attributes, independent CPU comparisons and GPU replay."""
import argparse
import concurrent.futures
import datetime
import gzip
import json
import shutil
import subprocess
from pathlib import Path
from build import ROOT, REPO, WORK, PACKAGE, REVISION, audit
from device_gate import sha, save
import arena_replay
import render_gate
import render_replay

OUT = WORK/'attributes'
DEST = ROOT/'evidence/native-attributes'
DEVICES = render_gate.DEVICES


def configure():
    arena_replay.configure(OUT/'linux-x64', OUT/'replay', '/data/local/tmp/codebrix-cycles-attributes')


def run():
    render_gate.run()
    for name, binary, args in [('linux-x64', 'attribute', []), ('render', 'render', ['--attributes'])]:
        folder = OUT/name
        folder.mkdir(parents=True, exist_ok=True)
        executable = WORK/'cycles-build/bin'/('codebrix_cycles_'+binary+'_gate')
        command = [str(executable), str(folder/'run'), *args]
        with (folder/'run.stdout.txt').open('w') as stdout, (folder/'run.stderr.txt').open('w') as stderr:
            subprocess.run(command, stdout=stdout, stderr=stderr, check=True, timeout=300)
        save(folder/'invocation.json', {'command':command, 'executableSha256':sha(executable)})
    replay()
    collect()


def replay():
    configure()
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        for future in [pool.submit(arena_replay.build, rid) for rid in sorted({d[0] for d in DEVICES})]:
            future.result()
    # Reuse the render replay binaries built and checked by render_gate.run().
    # Each device executes its two workloads sequentially.
    def device(rid, serial, label):
        arena_replay.run(rid, serial)
        render_replay.run(rid, OUT/'render', OUT/'render-replay'/label, serial)
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for future in [pool.submit(device, *d) for d in DEVICES]:
            future.result()


def collect():
    configure()
    current_sources = render_gate.inputs()
    regression = json.loads((render_gate.OUT/'validation.json').read_text())
    assert regression['passed'] and regression['sessionProofComplete']
    # Documentation/collection tooling may change after execution. Renderer and
    # driver implementation hashes must still match the validated regression.
    for name, digest in regression['sourceSha256'].items():
        if name.startswith(('native/cycles/', 'native/shaders/', 'native/src/', 'native/include/', 'native/tests/')):
            assert current_sources[name] == digest, 'Regression source changed: '+name
    native = json.loads((OUT/'linux-x64/run.stdout.txt').read_text())
    assert native['passed'] and native['originalCpuSurfaceSvm']
    expected = {'nativeGraphs':48, 'typedPrograms':328, 'cases':329, 'paths':5088,
                'invalidCasesRejected':7, 'cyclesAllocationBytesAfterCleanup':0}
    assert all(native[k] == v for k, v in expected.items())
    assert native['negativeAttributeOffsets'] > 0 and native['attributeMapChains'] > 0
    render = json.loads((OUT/'render/run.stdout.txt').read_text())
    assert all(render[k] for k in ('passed','nativeSceneAndScheduler','independentCpuImage','repeatBitwiseIdentical'))
    assert (render['width'], render['height'], render['samples']) == (64,48,8)
    assert render['maxBounce'] >= 3 and render['maxTransparentBounce'] >= 1
    assert render['surfaceShaders'] >= 20 and render['equivalentSharedEdgeComparisons'] == 0
    assert render['cyclesAllocationBytesAfterCleanup'] == 0
    assert json.loads((OUT/'render/run.shared-edges.json').read_text()) == []
    package = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    for name in ('linux-x64', 'render'):
        assert Path((OUT/name/'run.library.txt').read_text().strip()).resolve() == package.resolve()
        invocation = json.loads((OUT/name/'invocation.json').read_text())
        assert sha(Path(invocation['command'][0])) == invocation['executableSha256']
    assert json.loads((OUT/'render/invocation.json').read_text())['command'][-1] == '--attributes'
    replays = {}
    for rid, serial, label in DEVICES:
        folder = OUT/'replay'/('android-'+serial.replace(':','_') if serial else 'linux-result')
        checks = [(folder, arena_replay.validate(folder), arena_replay.BASE/rid/'arena_replay'),
                  (OUT/'render-replay'/label, render_replay.validate(OUT/'render', OUT/'render-replay'/label),
                   render_replay.BASE/rid/'render_replay')]
        replays[label] = {}
        for kind, (directory, actual, binary) in zip(('attributes', 'render'), checks, strict=True):
            saved = json.loads((directory/'validation.json').read_text())
            assert all(saved[k] == v for k, v in actual.items()), 'Changed replay evidence'
            assert saved['rid'] == rid and saved['serial'] == serial and saved['matchesPinnedPackage']
            assert saved['driverSha256'] == sha(binary)
            assert saved['webGpuSha256'] == sha(PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so')
            replays[label][kind] = str((directory/'validation.json').relative_to(OUT))
    save(OUT/'webgpu-compatibility.json', audit(allow_rust_documentation_change=True))
    report = dict(passed=True, fullCyclesParity=False, cyclesRevision=REVISION,
        scope='Static-triangle native attribute transport and surface SVM, plus a repeated native UV-checker render',
        nativeAttributes=native, nativeRender=render, replays=replays,
        replayScope='48 native graphs / 1152 evaluations and complete UV-render commands on four GPUs; typed derivative and invalid-input programs execute on Linux only. Android native hosts remain pending.',
        oracle='Unmodified original CPU shader_setup_from_ray/surface_shader_eval and independent complete CPU film',
        remaining='Motion/curve/point/voxel/matrix attributes, general bump state, production image/cache coverage, general materials and all nine native hosts',
        absoluteTolerance=5e-5, relativeTolerance=2e-4, strictAbsoluteTolerance=1e-7, strictRelativeTolerance=1e-7,
        strictNumericalParityPassed=False, matchesPinnedPackage=True, webGpuSha256=sha(package),
        regressionReportSha256=sha(render_gate.OUT/'validation.json'), sourceSha256=current_sources,
        nativeExecutableSha256={n:sha(WORK/'cycles-build/bin'/n) for n in
            ('codebrix_cycles_attribute_gate', 'codebrix_cycles_render_gate')})
    save(OUT/'validation.json', report)
    print(json.dumps({k:v for k,v in report.items() if k != 'sourceSha256'}, indent=2))


def archive():
    report = json.loads((OUT/'validation.json').read_text())
    assert report['passed'] and report['sourceSha256'] == render_gate.inputs(), 'Missing/stale collected evidence'
    assert not DEST.exists(), 'Never overwrite a frozen checkpoint'
    assert report['regressionReportSha256'] == sha(render_gate.OUT/'validation.json')
    DEST.mkdir(parents=True)
    def copy(source, target):
        target.parent.mkdir(parents=True, exist_ok=True)
        if source.suffix == '.bin':
            target.with_suffix('.bin.gz').write_bytes(gzip.compress(source.read_bytes(), mtime=0))
        else:
            shutil.copy2(source, target)
    for name in ('validation.json','webgpu-compatibility.json'):
        copy(OUT/name, DEST/name)
    for folder in ('linux-x64','render','replay','render-replay','failures'):
        for path in (OUT/folder).rglob('*'):
            if path.is_file() and (path.suffix in ('.json','.txt','.wgsl','.bin','.cpp','.ppm')):
                copy(path, DEST/path.relative_to(OUT))
    copy(render_gate.OUT/'validation.json', DEST/'regressions/validation.json')
    for name in render_gate.REGRESSIONS:
        for file in ('validation.json','run.stdout.txt','run.stderr.txt'):
            copy(WORK/name/'linux-x64'/file, DEST/'regressions'/name/file)
    for folder in (*render_gate.SCENES, 'surface', 'readback-rejection', 'replay'):
        for path in (render_gate.OUT/folder).rglob('*'):
            if path.is_file() and path.name != 'triangles.bin':
                copy(path, DEST/'regressions'/path.relative_to(render_gate.OUT))
    for path in render_replay.BASE.glob('*/*.json'):
        copy(path, DEST/'render-replay-build'/path.relative_to(render_replay.BASE))
    # Snapshot active native implementation, generated layouts, and its notice.
    # Vendor trees remain in-repository and content-addressed by sourceSha256.
    for name, digest in report['sourceSha256'].items():
        path = REPO/name
        assert sha(path) == digest
        if name.startswith('native/') and path.suffix in ('.cpp','.h','.wgsl','.py','.cmake'):
            copy(path, DEST/'source'/name)
    for name in ('LICENSE','native/THIRD-PARTY-NOTICES.txt'):
        copy(REPO/name, DEST/'source'/name)
    for name in ('cycles-build.json','cycles-webgpu-compatibility.json','cycles-registration.patch'):
        copy(WORK/name, DEST/name)
    for path in (WORK/'cycles-overlay').rglob('*'):
        if path.is_file():
            copy(path, DEST/'generated'/path.relative_to(WORK/'cycles-overlay'))
    save(DEST/'manifest.json', dict(capturedUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        scope=report['scope'], cyclesRevision=REVISION,
        reconstruction='Decompress .bin.gz files. Active native code and generated layouts are saved; use repository vendor trees verified against validation.json sourceSha256. Root notices remain in the repository with recorded hashes. No compiled host/driver/runtime binaries are included.',
        regressionReconstruction='Fresh bounded regression reports, complete original render captures, replay arenas and scheduling-readback rejection are retained. Decompress .bin.gz files to replay. Individual earlier gate captures remain in their historical checkpoints; rerun render_gate.py for this implementation.',
        savedSha256={str(p.relative_to(DEST)):sha(p) for p in sorted(DEST.rglob('*')) if p.is_file()}))
    print(DEST)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['run','replay','collect','archive'])
    args = parser.parse_args()
    globals()[args.action]()
