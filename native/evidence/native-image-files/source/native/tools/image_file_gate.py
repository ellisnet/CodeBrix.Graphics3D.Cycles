#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""File decoding, resident-image replacement and immutable GPU sampler replay."""
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

OUT = WORK/'image-files'
CAPTURE = OUT/'linux-x64'
DEST = ROOT/'evidence/native-image-files'
PRIOR = ROOT/'evidence/native-images'


def configure():
    arena_replay.configure(CAPTURE, OUT/'replay', '/data/local/tmp/codebrix-cycles-image-files')


def native():
    CAPTURE.mkdir(parents=True, exist_ok=True)
    binary = WORK/'cycles-build/bin/codebrix_cycles_image_file_gate'
    command = [str(binary),str(CAPTURE/'run')]
    digest = sha(binary)
    with (CAPTURE/'run.stdout.txt').open('w') as stdout, (CAPTURE/'run.stderr.txt').open('w') as stderr:
        subprocess.run(command, stdout=stdout, stderr=stderr, check=True, timeout=600)
    assert sha(binary) == digest
    save(CAPTURE/'invocation.json', {'command':command,'executableSha256':digest})


def replay():
    configure()
    with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
        for future in [pool.submit(arena_replay.build,rid) for rid in sorted({d[0] for d in render_gate.DEVICES})]:
            future.result()
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        for future in [pool.submit(arena_replay.run,rid,serial) for rid,serial,_ in render_gate.DEVICES]:
            future.result()


def prior_identity(sources):
    reports = ['validation.json','attribute-regression/validation.json','attribute-regression/regressions/validation.json']
    binaries = {}
    report_hashes = {}
    for name in reports:
        report = json.loads((PRIOR/name).read_text())
        assert report['passed']
        report_hashes[str((PRIOR/name).relative_to(REPO))] = sha(PRIOR/name)
        for binary,expected in report['nativeExecutableSha256'].items():
            assert sha(WORK/'cycles-build/bin'/binary) == expected, 'Previously validated executable changed: '+binary
            binaries[binary] = expected
    previous = json.loads((PRIOR/'validation.json').read_text())
    unchanged = {}
    for name,expected in previous['sourceSha256'].items():
        if name.startswith(('native/cycles/','native/shaders/','native/src/','native/include/','native/tests/')):
            assert sources[name] == expected, 'Previously validated renderer source changed: '+name
            unchanged[name] = expected
    return {'passed':True,'reportSha256':report_hashes,'nativeExecutableSha256':binaries,
            'sourceSha256':unchanged,
            'scope':'Earlier rendering/attribute/image results remain applicable: all previous native implementation files and four validated native executables are byte-identical. Build changes only add this file-image test target; previous GPU workloads were not rerun for this test addition.'}


def collect():
    configure()
    result = json.loads((CAPTURE/'run.stdout.txt').read_text())
    assert result['passed'] and result['nativeFileImageManager'] and result['knownOcioMatrix']
    expected = {'fileVariants':18,'parameterVariants':450,'residentFormats':8,'nativeUdimTiles':3,
                'missingFiles':2,'duplicateHandles':16,'replacementCycles':16,'queuedOldImageReads':16,
                'knownLosslessTexels':34483,'samplingCases':154931,'captureCases':116352,
                'cyclesAllocationBytesAfterCleanup':0,'tiffPluginAvailable':False}
    assert all(result[k] == v and type(result[k]) is type(v) for k,v in expected.items())
    diagnostics = json.loads((CAPTURE/'run.sampling-diagnostics.json').read_text())
    assert diagnostics['maximumToleranceRatio'] <= 1
    library = PACKAGE/'runtimes/linux-x64/native/libcodebrix_wgpu.so'
    assert Path((CAPTURE/'run.library.txt').read_text().strip()).resolve() == library.resolve()
    invocation = json.loads((CAPTURE/'invocation.json').read_text())
    assert sha(Path(invocation['command'][0])) == invocation['executableSha256']
    replays = {}
    for rid,serial,label in render_gate.DEVICES:
        folder = arena_replay.BASE/('android-'+serial.replace(':','_') if serial else 'linux-result')
        actual = arena_replay.validate(folder)
        saved = json.loads((folder/'validation.json').read_text())
        assert all(saved[k] == v for k,v in actual.items())
        assert saved['paths'] == expected['captureCases'] and saved['rid'] == rid and saved['serial'] == serial
        assert saved['matchesPinnedPackage'] and saved['driverSha256'] == sha(arena_replay.BASE/rid/'arena_replay')
        assert saved['webGpuSha256'] == sha(PACKAGE/'runtimes'/rid/'native/libcodebrix_wgpu.so')
        replays[label] = {'path':str((folder/'validation.json').relative_to(OUT)), 'sha256':sha(folder/'validation.json')}
    sources = render_gate.inputs()
    identity = prior_identity(sources)
    save(OUT/'webgpu-compatibility.json',audit(allow_rust_documentation_change=True))
    report = dict(passed=True, fullCyclesParity=False, cyclesRevision=REVISION,
        scope='Native PNG/JPEG/OpenEXR decoding, channel/alpha/color conformance, native UDIM and queued image replacement; bounded original-CPU and independent known-pixel checks',
        nativeFiles=result,samplingDiagnostics=diagnostics,replays=replays,unchangedPriorRenderer=identity,
        replayScope='Final 116352-sample corpus on four GPUs. Actual file decoding, OCIO configuration, duplicate/replacement lifecycle and independent known-texel assertions currently execute on Linux only. Android native hosts remain pending.',
        remaining='Compatible TIFF/LZW and other missing format paths, production OCIO/animation/cache stress, native Android/other RIDs and original material renders',
        absoluteTolerance=5e-5,relativeTolerance=2e-4,strictAbsoluteTolerance=1e-7,strictRelativeTolerance=1e-7,
        strictNumericalParityPassed=False,matchesPinnedPackage=True,webGpuSha256=sha(library),
        sourceSha256=sources,nativeExecutableSha256={'codebrix_cycles_image_file_gate':invocation['executableSha256']},
        fixtureSha256={str(p.relative_to(OUT)):sha(p) for p in sorted((CAPTURE/'files').rglob('*')) if p.is_file()},
        ocioConfigSha256=sha(CAPTURE/'run.ocio'))
    save(OUT/'validation.json',report)
    print(json.dumps({k:v for k,v in report.items() if k not in ('sourceSha256','unchangedPriorRenderer')},indent=2))


def archive():
    report = json.loads((OUT/'validation.json').read_text())
    assert report['passed'] and report['sourceSha256'] == render_gate.inputs()
    assert not DEST.exists(), 'Never overwrite a frozen checkpoint'
    DEST.mkdir(parents=True)
    def copy(source,target):
        target.parent.mkdir(parents=True,exist_ok=True)
        if source.suffix == '.bin':
            target.with_suffix('.bin.gz').write_bytes(gzip.compress(source.read_bytes(),mtime=0))
        else:
            shutil.copy2(source,target)
    for name in ('validation.json','webgpu-compatibility.json'):
        copy(OUT/name,DEST/name)
    for name in ('linux-x64','replay','failures'):
        for path in (OUT/name).rglob('*'):
            if path.is_file() and path.suffix in ('.json','.txt','.wgsl','.bin','.cpp','.h','.png','.jpg','.exr','.ocio'):
                copy(path,DEST/path.relative_to(OUT))
    for name,digest in report['sourceSha256'].items():
        path = REPO/name
        assert sha(path) == digest
        if name.startswith('native/') and path.suffix in ('.cpp','.h','.wgsl','.py','.cmake'):
            copy(path,DEST/'source'/name)
    for name in ('AGENTS.md','LICENSE','native/THIRD-PARTY-NOTICES.txt'):
        copy(REPO/name,DEST/'source'/name)
    copy(WORK/'cycles-build.json',DEST/'cycles-build.json')
    compile_commands = json.loads((WORK/'cycles-build/compile_commands.json').read_text())
    save(DEST/'compile-commands.json',[item for item in compile_commands if 'codebrix_cycles_image_file_gate.dir' in item['command']])
    copy(WORK/'cycles-build/CMakeFiles/codebrix_cycles_image_file_gate.dir/link.txt',DEST/'link.txt')
    save(DEST/'manifest.json',dict(capturedUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(),
        cyclesRevision=REVISION,scope=report['scope'],
        reconstruction='Decompress .bin.gz. File fixtures are original generated inputs, including one deliberately corrupt PNG. The missing PNG is intentionally absent. Native source is saved; vendor snapshots and generated rendering layouts are pinned by source hashes and the prior native-images checkpoint. No compiled binaries are included.',
        savedSha256={str(p.relative_to(DEST)):sha(p) for p in sorted(DEST.rglob('*')) if p.is_file()}))
    print(DEST)


def run():
    subprocess.run(['python3',str(ROOT/'tools/build_cycles.py')],check=True)
    native()
    replay()
    collect()


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action',choices=['run','native','replay','collect','archive'])
    globals()[parser.parse_args().action]()
