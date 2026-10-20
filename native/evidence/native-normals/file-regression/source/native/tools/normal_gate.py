#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Native normal maps/tangents, original CPU comparisons and four-device replay."""
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
import render_replay
import render_gate
import image_gate
import image_file_gate

OUT = WORK/'normals'
DEST = ROOT/'evidence/native-normals'
DEVICES = render_gate.DEVICES
CAPTURES = {'graphs':OUT/'linux-x64', **{'dual-'+str(i):OUT/'linux-x64'/('dual-'+str(i)) for i in range(1,4)}}
image_file_gate.OUT = OUT/'file-regression'
image_file_gate.CAPTURE = image_file_gate.OUT/'linux-x64'


def native():
    for name, binary, args in [('linux-x64','normal',[]), ('render','render',['--normals'])]:
        folder = OUT/name
        folder.mkdir(parents=True, exist_ok=True)
        exe = WORK/'cycles-build/bin'/('codebrix_cycles_'+binary+'_gate')
        command = [str(exe), str(folder/'run'), *args]
        before = sha(exe)
        with (folder/'run.stdout.txt').open('w') as stdout, (folder/'run.stderr.txt').open('w') as stderr:
            subprocess.run(command, stdout=stdout, stderr=stderr, check=True, timeout=600)
        assert before == sha(exe)
        save(folder/'invocation.json', {'command':command,'executableSha256':before})


def configure(kind):
    arena_replay.configure(CAPTURES[kind], OUT/'replay'/kind,
                           '/data/local/tmp/codebrix-cycles-normals-'+kind)


def replay_kernels():
    for kind in CAPTURES:
        configure(kind)
        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
            for result in [pool.submit(arena_replay.build,rid) for rid in sorted({d[0] for d in DEVICES})]:
                result.result()
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            for result in [pool.submit(arena_replay.run,rid,serial) for rid,serial,_ in DEVICES]:
                result.result()


def replay_render():
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for result in [pool.submit(render_replay.run,rid,OUT/'render',OUT/'render-replay'/label,serial)
                       for rid,serial,label in DEVICES]:
            result.result()


def regressions():
    image_gate.run()
    image_file_gate.native()
    image_file_gate.replay()
    image_file_gate.collect(image_gate.OUT/'validation.json')


def collect():
    image_gate.collect()
    image_file_gate.collect(image_gate.OUT/'validation.json')
    result = json.loads((OUT/'linux-x64/run.stdout.txt').read_text())
    assert result['passed'] and result['originalCpuSurfaceSvm']
    expected = {'nativeGraphs':48,'typedPrograms':398,'cases':399,'paths':10704,
                'invalidCasesRejected':10,'cyclesAllocationBytesAfterCleanup':0}
    assert all(result[k] == v for k,v in expected.items()), 'Normal coverage changed'
    render = json.loads((OUT/'render/run.stdout.txt').read_text())
    assert all(render[k] for k in ('passed','nativeSceneAndScheduler','independentCpuImage','repeatBitwiseIdentical','nativeImageTextures','nativeNormalMaps'))
    assert (render['width'],render['height'],render['samples']) == (64,48,8)
    assert render['maxBounce'] >= 3 and render['maxTransparentBounce'] >= 1
    assert render['equivalentSharedEdgeComparisons'] == 0 and render['cyclesAllocationBytesAfterCleanup'] == 0
    assert json.loads((OUT/'render/run.shared-edges.json').read_text()) == []
    library = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    for name in ('linux-x64','render'):
        assert Path((OUT/name/'run.library.txt').read_text().strip()).resolve() == library.resolve()
        invocation = json.loads((OUT/name/'invocation.json').read_text())
        assert sha(Path(invocation['command'][0])) == invocation['executableSha256']
    replays = {}
    for kind in CAPTURES:
        configure(kind)
        for rid,serial,label in DEVICES:
            folder = arena_replay.BASE/('android-'+serial.replace(':','_') if serial else 'linux-result')
            current = arena_replay.validate(folder)
            saved = json.loads((folder/'validation.json').read_text())
            assert all(saved[k] == v for k,v in current.items())
            assert saved['rid'] == rid and saved['serial'] == serial and saved['matchesPinnedPackage']
            assert saved['driverSha256'] == sha(arena_replay.BASE/rid/'arena_replay')
            assert saved['webGpuSha256'] == sha(PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so')
            replays[kind+'-'+label] = {'path':str((folder/'validation.json').relative_to(OUT)), 'sha256':sha(folder/'validation.json')}
    for rid,serial,label in DEVICES:
        folder = OUT/'render-replay'/label
        current = render_replay.validate(OUT/'render',folder)
        saved = json.loads((folder/'validation.json').read_text())
        assert all(saved[k] == v for k,v in current.items())
        assert saved['rid'] == rid and saved['serial'] == serial and saved['matchesPinnedPackage']
        assert saved['driverSha256'] == sha(render_replay.BASE/rid/'render_replay')
        assert saved['webGpuSha256'] == sha(PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so')
        replays['render-'+label] = {'path':str((folder/'validation.json').relative_to(OUT)), 'sha256':sha(folder/'validation.json')}
    save(OUT/'webgpu-compatibility.json',audit(allow_rust_documentation_change=True))
    report = dict(passed=True,fullCyclesParity=False,cyclesRevision=REVISION,
        scope='Native static-triangle normal-map and tangent SVM, analytic tangent derivatives and independent complete normal-mapped image render',
        nativeNormals=result,nativeRender=render,replays=replays,
        replayScope='48 native graphs, selected tangent value/dx/dy captures and complete normal-mapped render on four GPUs. Remaining typed and malformed-input cases run on Linux only. Android native hosts remain pending.',
        remaining='Full bump entry/exit, motion and other geometry, native degenerate plain-tangent nonfinite behavior, original asset materials/closures and all nine native hosts',
        absoluteTolerance=5e-5,relativeTolerance=2e-4,strictAbsoluteTolerance=1e-7,strictRelativeTolerance=1e-7,
        strictNumericalParityPassed=False,matchesPinnedPackage=True,webGpuSha256=sha(library),
        imageRegressionSha256=sha(image_gate.OUT/'validation.json'),fileRegressionSha256=sha(image_file_gate.OUT/'validation.json'),
        sourceSha256=render_gate.inputs(),nativeExecutableSha256={name:sha(WORK/'cycles-build/bin'/name) for name in ('codebrix_cycles_normal_gate','codebrix_cycles_render_gate')})
    save(OUT/'validation.json',report)
    print(json.dumps({k:v for k,v in report.items() if k != 'sourceSha256'},indent=2))


def archive():
    report = json.loads((OUT/'validation.json').read_text())
    assert report['passed'] and report['sourceSha256'] == render_gate.inputs()
    assert report['imageRegressionSha256'] == sha(image_gate.OUT/'validation.json')
    assert report['fileRegressionSha256'] == sha(image_file_gate.OUT/'validation.json')
    assert not DEST.exists(), 'Never overwrite a frozen checkpoint'
    DEST.mkdir(parents=True)
    image_gate.DEST = DEST/'image-regression'
    image_gate.archive()
    image_file_gate.DEST = DEST/'file-regression'
    image_file_gate.archive()
    def copy(source,target):
        target.parent.mkdir(parents=True,exist_ok=True)
        if source.suffix == '.bin':
            target.with_suffix('.bin.gz').write_bytes(gzip.compress(source.read_bytes(),mtime=0))
        else:
            shutil.copy2(source,target)
    for name in ('validation.json','webgpu-compatibility.json','build.stdout.txt','build.stderr.txt'):
        copy(OUT/name,DEST/name)
    for name in ('linux-x64','render','replay','render-replay','failures'):
        for path in (OUT/name).rglob('*'):
            if path.is_file() and path.suffix in ('.json','.txt','.wgsl','.bin','.cpp','.h','.ppm','.inc'):
                copy(path,DEST/path.relative_to(OUT))
    save(DEST/'manifest.json',dict(capturedUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        scope=report['scope'],cyclesRevision=REVISION,
        reconstruction='Decompress .bin.gz. Native source and generated layouts are in image-regression/attribute-regression/source and generated. Fresh earlier regressions and file-image regression are retained. Vendor sources remain pinned in-repository. No compiled binaries are included.',
        savedSha256={str(p.relative_to(DEST)):sha(p) for p in sorted(DEST.rglob('*')) if p.is_file()}))
    print(DEST)


def run():
    subprocess.run(['python3',str(ROOT/'tools/build_cycles.py')],check=True)
    native()
    regressions()
    replay_kernels()
    replay_render()
    collect()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action',choices=['run','native','regressions','replay-kernels','replay-render','collect','archive'])
    globals()[parser.parse_args().action.replace('-','_')]()
