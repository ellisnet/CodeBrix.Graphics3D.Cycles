#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Native resident images: CPU sampling/SVM comparisons, full render and replay."""
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
import attribute_gate

OUT = WORK/'images'
DEST = ROOT/'evidence/native-images'
DEVICES = render_gate.DEVICES
CAPTURES = {'sampling':OUT/'linux-x64/sampling', 'graphs':OUT/'linux-x64'}


def native():
    for name, binary, args in [('linux-x64','image',[]), ('render','render',['--images'])]:
        folder = OUT/name
        folder.mkdir(parents=True, exist_ok=True)
        exe = WORK/'cycles-build/bin'/('codebrix_cycles_'+binary+'_gate')
        command = [str(exe), str(folder/'run'), *args]
        before = sha(exe)
        with (folder/'run.stdout.txt').open('w') as stdout, (folder/'run.stderr.txt').open('w') as stderr:
            subprocess.run(command, stdout=stdout, stderr=stderr, check=True, timeout=600)
        assert before == sha(exe), 'Executable changed during native gate'
        save(folder/'invocation.json', {'command':command,'executableSha256':before})


def configure(kind):
    arena_replay.configure(CAPTURES[kind], OUT/'replay'/kind,
                           '/data/local/tmp/codebrix-cycles-images-'+kind)


def replay_kernels():
    # Each global capture configuration remains stable until every job finishes.
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


def replay():
    replay_kernels()
    replay_render()


def collect():
    attribute_gate.collect()
    native_result = json.loads((OUT/'linux-x64/run.stdout.txt').read_text())
    assert native_result['passed'] and native_result['nativeImageManager']
    expected = {'formats':8, 'nativeGraphs':128, 'typedPrograms':176,
                'invalidCasesRejected':14, 'samplingCases':115650, 'graphPaths':1824,
                'cyclesAllocationBytesAfterCleanup':0}
    assert all(native_result[k] == v for k,v in expected.items()), 'Image test coverage changed'
    render = json.loads((OUT/'render/run.stdout.txt').read_text())
    assert all(render[k] for k in ('passed','nativeSceneAndScheduler','independentCpuImage','repeatBitwiseIdentical','nativeImageTextures'))
    assert (render['width'],render['height'],render['samples']) == (64,48,8)
    assert render['maxBounce'] >= 3 and render['maxTransparentBounce'] >= 1
    assert render['equivalentSharedEdgeComparisons'] == 0 and render['cyclesAllocationBytesAfterCleanup'] == 0
    assert json.loads((OUT/'render/run.shared-edges.json').read_text()) == []
    package = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    for name in ('linux-x64','render'):
        assert Path((OUT/name/'run.library.txt').read_text().strip()).resolve() == package.resolve()
        invocation = json.loads((OUT/name/'invocation.json').read_text())
        assert sha(Path(invocation['command'][0])) == invocation['executableSha256']
    replays = {}
    for kind in CAPTURES:
        configure(kind)
        for rid,serial,label in DEVICES:
            directory = arena_replay.BASE/('android-'+serial.replace(':','_') if serial else 'linux-result')
            current = arena_replay.validate(directory)
            saved = json.loads((directory/'validation.json').read_text())
            assert all(saved[k] == v for k,v in current.items())
            assert saved['rid'] == rid and saved['serial'] == serial and saved['matchesPinnedPackage']
            assert saved['driverSha256'] == sha(arena_replay.BASE/rid/'arena_replay')
            assert saved['webGpuSha256'] == sha(PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so')
            replays[kind+'-'+label] = {'path':str((directory/'validation.json').relative_to(OUT)), 'sha256':sha(directory/'validation.json')}
    for rid,serial,label in DEVICES:
        directory = OUT/'render-replay'/label
        current = render_replay.validate(OUT/'render',directory)
        saved = json.loads((directory/'validation.json').read_text())
        assert all(saved[k] == v for k,v in current.items())
        assert saved['rid'] == rid and saved['serial'] == serial and saved['matchesPinnedPackage']
        assert saved['driverSha256'] == sha(render_replay.BASE/rid/'render_replay')
        assert saved['webGpuSha256'] == sha(PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so')
        replays['render-'+label] = {'path':str((directory/'validation.json').relative_to(OUT)), 'sha256':sha(directory/'validation.json')}
    diagnostics = json.loads((OUT/'linux-x64/run.sampling-diagnostics.json').read_text())
    assert diagnostics['maximumToleranceRatio'] <= 1
    save(OUT/'webgpu-compatibility.json',audit(allow_rust_documentation_change=True))
    report = dict(passed=True,fullCyclesParity=False,cyclesRevision=REVISION,
        scope='Resident native image transport, original-CPU software sampling and surface SVM; independent complete image-textured native render',
        nativeImages=native_result,nativeRender=render,samplingDiagnostics=diagnostics,replays=replays,
        replayScope='All sampling cases, 128 compiled image graphs and full recorded render on four GPUs; typed projections/derivatives and malformed-input cases currently execute on Linux only. Android native hosts remain pending.',
        remaining='Tiled/mipmap cache and retry, sparse 3D images, production color configurations, file-format/UDIM/animation corpus, original asset materials and full native platform hosts',
        absoluteTolerance=5e-5,relativeTolerance=2e-4,strictAbsoluteTolerance=1e-7,strictRelativeTolerance=1e-7,
        strictNumericalParityPassed=False,matchesPinnedPackage=True,webGpuSha256=sha(package),
        attributeRegressionSha256=sha(attribute_gate.OUT/'validation.json'),sourceSha256=render_gate.inputs(),
        nativeExecutableSha256={name:sha(WORK/'cycles-build/bin'/name) for name in ('codebrix_cycles_image_gate','codebrix_cycles_render_gate')})
    save(OUT/'validation.json',report)
    print(json.dumps({k:v for k,v in report.items() if k != 'sourceSha256'},indent=2))


def run():
    attribute_gate.run()
    native()
    replay()
    collect()


def archive():
    report = json.loads((OUT/'validation.json').read_text())
    assert report['passed'] and report['sourceSha256'] == render_gate.inputs()
    assert report['attributeRegressionSha256'] == sha(attribute_gate.OUT/'validation.json')
    assert not DEST.exists(), 'Never overwrite a frozen checkpoint'
    DEST.mkdir(parents=True)
    # Fresh attribute and earlier render regressions include source snapshots,
    # generated layouts and original-render captures for this implementation.
    attribute_gate.DEST = DEST/'attribute-regression'
    attribute_gate.archive()
    def copy(source,target):
        target.parent.mkdir(parents=True,exist_ok=True)
        if source.suffix == '.bin':
            target.with_suffix('.bin.gz').write_bytes(gzip.compress(source.read_bytes(),mtime=0))
        else:
            shutil.copy2(source,target)
    for name in ('validation.json','webgpu-compatibility.json'):
        copy(OUT/name,DEST/name)
    for name in ('linux-x64','render','replay','render-replay','failures'):
        for path in (OUT/name).rglob('*'):
            if path.is_file() and path.suffix in ('.json','.txt','.wgsl','.bin','.cpp','.h','.ppm'):
                copy(path,DEST/path.relative_to(OUT))
    save(DEST/'manifest.json',dict(capturedUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        scope=report['scope'],cyclesRevision=REVISION,
        reconstruction='Decompress .bin.gz. Native code and generated layouts are in attribute-regression/source and generated. Vendor trees stay in-repository with exact source hashes in validation.json. This checkpoint includes no compiled binaries. Replay folders retain full output arenas.',
        savedSha256={str(p.relative_to(DEST)):sha(p) for p in sorted(DEST.rglob('*')) if p.is_file()}))
    print(DEST)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action',choices=['run','native','replay','replay-kernels','replay-render','collect','archive'])
    globals()[parser.parse_args().action.replace('-','_')]()
