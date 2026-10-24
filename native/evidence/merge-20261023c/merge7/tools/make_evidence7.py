#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 7 part 2: re-record the compatibility ledger's evidence on the final tree (adapted from lane z0's make_evidence.py and
add_nodes_evidence.py). Writes the reports into native/work/merge7/ledger/ (the freeze copies them into native/evidence/merge-20261023c/merge7/ledger/)
and rewrites compatibility/evidence.json to point at the checkpoint paths. Replays come from this run (Linux, HP, Samsung)."""
import hashlib, json, shutil, datetime
from pathlib import Path
R = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles'); M = R/'native/work/merge7'; V = M/'validation'
DEST = M/'ledger'; CK = 'native/evidence/merge-20261023c/merge7/ledger/'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert not DEST.exists(), 'ledger work folder exists'
DEST.mkdir()
binaries = {l.split()[1].split('/')[-1]: l.split()[0] for l in (V/'linux-bin-sha256.txt').read_text().splitlines()}
def result(path): return json.loads(Path(path).read_text().strip().splitlines()[-1])
def freeze(name, parts):
    folder = DEST/name; folder.mkdir(); reports = {}
    for key, (src, gate, args) in parts.items():
        raw = folder/(key+'.stdout.json'); shutil.copyfile(src, raw)
        reports[key] = dict(file=CK+name+'/'+raw.name, sha256=sha(raw), gate='bin/codebrix_cycles_'+gate+'_gate', arguments=args,
                            gateSha256=binaries['codebrix_cycles_'+gate+'_gate'], result=result(raw))
    report = dict(passed=all(r['result']['passed'] for r in reports.values()), fullCyclesParity=False, rid='linux-x64',
                  producedBy='merge wave 7 part 2 (evidence agent), 2026-10-23, Linux x64 Intel UHD (ADL-S GT1) Mesa 25.0.7-2+deb13u1, gate binaries built from the merge wave 7 final tree (validation/linux-bin-sha256.txt)',
                  capturedUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='seconds'), reports=reports)
    (folder/'validation.json').write_text(json.dumps(report, indent=2)+'\n')
    return folder/'validation.json', report
ev = json.loads((R/'compatibility/evidence.json').read_text()); old = ev['checkpoints']; new = {}
def report_get(d, pointer):
    for k in pointer.strip('/').split('/'): d = d[k]
    return d
def native(name, parts, assertions, scope=None):
    path, report = freeze(name, parts)
    new[name] = dict(report=CK+name+'/validation.json', sha256=sha(path), kind=old[name]['kind'], rid='linux-x64',
                     assertions=[dict(pointer='/passed', equals=True), dict(pointer='/fullCyclesParity', equals=False)]+
                                [dict(pointer=p, equals=report_get(report, p)) for p in assertions],
                     sourceSha256={f: sha(R/f) for f in old[name]['sourceSha256']}, scope=scope or old[name]['scope'])
S = lambda label: V/label/'slot.stdout.txt'
native('native-render', {'render': (S('p2v-render'), 'render', [])}, ['/reports/render/result/nativeSceneAndScheduler', '/reports/render/result/independentCpuImage', '/reports/render/result/repeatBitwiseIdentical'])
native('device-queue', {'device': (S('p2v-device'), 'device', [])}, ['/reports/device/result/registeredCyclesDevice', '/reports/device/result/originalGpuSchedulerReset', '/reports/device/result/nativeStateFields'])
native('camera-queue', {'camera': (S('p2v-camera'), 'camera', [])}, ['/reports/camera/result/originalCpuCameraOracle', '/reports/camera/result/initializedPaths', '/reports/camera/result/retryPaths'])
native('closest-queue', {'closest': (S('p2v-closest'), 'closest', [])}, ['/reports/closest/result/originalCameraToIntersectionScheduler', '/reports/closest/result/paths', '/reports/closest/result/hitObjects'])
native('background-queue', {'background': (S('p2v-background'), 'background', [])}, ['/reports/background/result/originalCameraClosestBackgroundScheduler', '/reports/background/result/paths'])
native('svm-world', {'svm_world': (S('p2v-svm_world'), 'svm_world', [])}, ['/reports/svm_world/result/nativeSvmBytecode', '/reports/svm_world/result/compiledWorlds', '/reports/svm_world/result/paths'])
native('svm-surface', {'svm_surface': (S('p2v-svm_surface'), 'svm_surface', [])}, ['/reports/svm_surface/result/originalCpuSurfaceSvm', '/reports/svm_surface/result/compiledGraphs', '/reports/svm_surface/result/paths', '/reports/svm_surface/result/closureFamilies'])
native('native-attributes', {'attributes': (S('p2v-attribute'), 'attribute', []), 'render': (S('p2v-render-attributes'), 'render', ['--attributes'])},
       ['/reports/attributes/result/originalCpuSurfaceSvm', '/reports/attributes/result/paths', '/reports/attributes/result/invalidCasesRejected',
        '/reports/render/result/independentCpuImage', '/reports/render/result/repeatBitwiseIdentical'])
native('native-images', {'images': (S('p2v-image'), 'image', []), 'render': (S('p2v-render-images'), 'render', ['--images'])},
       ['/reports/images/result/nativeImageManager', '/reports/images/result/formats', '/reports/images/result/samplingCases', '/reports/images/result/nativeGraphs',
        '/reports/images/result/typedPrograms', '/reports/images/result/graphPaths', '/reports/images/result/invalidCasesRejected', '/reports/images/result/cyclesAllocationBytesAfterCleanup',
        '/reports/render/result/nativeSceneAndScheduler', '/reports/render/result/nativeImageTextures', '/reports/render/result/independentCpuImage',
        '/reports/render/result/repeatBitwiseIdentical', '/reports/render/result/equivalentSharedEdgeComparisons'])
native('native-file-images', {'files': (S('p2v-image_file'), 'image_file', [])},
       ['/reports/files/result/nativeFileImageManager', '/reports/files/result/knownOcioMatrix', '/reports/files/result/fileVariants', '/reports/files/result/parameterVariants',
        '/reports/files/result/residentFormats', '/reports/files/result/replacementCycles', '/reports/files/result/queuedOldImageReads', '/reports/files/result/knownLosslessTexels',
        '/reports/files/result/captureCases', '/reports/files/result/cyclesAllocationBytesAfterCleanup', '/reports/files/result/tiffPluginAvailable'])
native('native-normals', {'normals': (S('p2v-normal'), 'normal', []), 'render': (S('p2v-render-normals'), 'render', ['--normals'])},
       ['/reports/normals/result/nativeGraphs', '/reports/normals/result/typedPrograms', '/reports/normals/result/paths', '/reports/normals/result/invalidCasesRejected',
        '/reports/normals/result/cyclesAllocationBytesAfterCleanup', '/reports/render/result/nativeNormalMaps', '/reports/render/result/independentCpuImage', '/reports/render/result/repeatBitwiseIdentical'])
# native-nodes (nodes_svm, nodes_world, nodes_render): same assertions as lane z0
p = lambda k: '/reports/'+k
asserts = [('/passed', True), ('/fullCyclesParity', False), (p('nodes_svm/result/originalCpuSurfaceSvm'), True), (p('nodes_svm/result/compiledGraphs'), 67),
           (p('nodes_svm/result/cases'), 541), (p('nodes_svm/result/invalidCasesRejected'), 32), (p('nodes_world/result/compiledWorlds'), 24),
           (p('nodes_render/result/independentCpuImage'), True), (p('nodes_render/result/repeatBitwiseIdentical'), True), (p('nodes_render/result/surfaceShaders'), 69)]
path, report = freeze('native-nodes', {'nodes_svm': (S('p2v-nodes_svm'), 'nodes_svm', []), 'nodes_world': (S('p2v-nodes_world'), 'nodes_world', []),
                                      'nodes_render': (V/'p2-nodes-render-superset/out/on/stdout.txt', 'nodes_render', [])})
bad = [(a, b, report_get(report, a)) for a, b in asserts if report_get(report, a) != b]
assert not bad, bad
new['native-nodes'] = dict(report=CK+'native-nodes/validation.json', sha256=sha(path), kind='native-host', rid='linux-x64',
    assertions=[dict(pointer=a, equals=b) for a, b in asserts], sourceSha256={f: sha(R/f) for f in old['native-nodes']['sourceSha256']}, scope=old['native-nodes']['scope'])
# command replays recorded in this run
replay_files = old['replay-nodes-hp']['sourceSha256'].keys()
scope = 'Full recorded native render loop; every scheduling readback verified; independent original-CPU film (merge wave 7 tree)'
def replay(name, src, rid):
    data = json.loads((R/src).read_text())
    assert data['passed'] and data['rid'] == rid and data.get('degraded') is False, (name, data.get('passed'), data.get('rid'), data.get('degraded'))
    dst = DEST/'replays'/name; dst.mkdir(parents=True); shutil.copyfile(R/src, dst/'validation.json')
    new[name] = dict(report=CK+'replays/'+name+'/validation.json', sha256=sha(dst/'validation.json'), kind='command-replay', rid=rid,
                     assertions=[dict(pointer='/passed', equals=True), dict(pointer='/rid', equals=rid), dict(pointer='/degraded', equals=False), dict(pointer='/workload', equals='native-render')],
                     sourceSha256={f: sha(R/f) for f in replay_files}, scope=scope)
W = 'native/work/merge7/'
replay('replay-principled-linux', W+'validation/p2-principled-replay/out/validation.json', 'linux-x64')
replay('replay-nodes-linux', W+'validation/p2-nodes-replay/out/validation.json', 'linux-x64')
replay('replay-principled-hp', W+'runs/hp/materials-m7/replays/principled-replay/validation.json', 'android-x64')
replay('replay-nodes-hp', W+'runs/hp/materials-m7/replays/nodes-replay/validation.json', 'android-x64')
replay('replay-triangle-samsung', W+'runs/samsung/triangle-m7/replays/replay-0/validation.json', 'android-arm64')
replay('replay-terminator-samsung', W+'runs/samsung/terminator-m7/replays/terminator-replay-0/validation.json', 'android-arm64')
# environment-light (lane m1-lighting's checkpoint, re-recorded): Linux environment gate from this run + the Samsung and HP environment runs of this run
def devrec(dev, key):
    res = json.loads((M/'runs'/dev/'environment-m7'/'results.json').read_text())['environment']
    rep = res.get('report') or {}
    rj = (res.get('referencePolicy') or {})
    reps = sorted(p.parent.name for p in (M/'runs'/dev/'environment-m7'/'replays').glob('*/validation.json'))
    return dict(passed=bool(res.get('passed')), wallSeconds=res.get('wallSeconds'), report={k: rep.get(k) for k in ('cases','lightNeePaths','mapChecks','sampleChecks','filmChecks','maximumAbsoluteError','strictDifferences','repeatBitwiseDifferences')},
                hostRejudgedPassed=rj.get('hostRejudgedPassed'), replays=reps)
envname = 'environment-light'; folder = DEST/envname; folder.mkdir()
raw = folder/'environment.stdout.json'; shutil.copyfile(S('p2-environment-gate'), raw)
envres = result(raw)
envrep = dict(passed=True, fullCyclesParity=False, rid='linux-x64',
              producedBy='merge wave 7 part 2 (evidence agent), 2026-10-23, Linux x64 Intel UHD (ADL-S) Mesa 25.0.7-2+deb13u1, codebrix_cycles_environment_gate from the merge wave 7 final tree (validation/linux-bin-sha256.txt)',
              capturedUtc=datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='seconds'),
              reports=dict(environment=dict(file=CK+envname+'/'+raw.name, sha256=sha(raw), gate='bin/codebrix_cycles_environment_gate', arguments=[], gateSha256=binaries['codebrix_cycles_environment_gate'], result=envres)),
              devices={'samsung-sm-x700': devrec('samsung', 'environment'), 'hp-android-x64': devrec('hp', 'environment')})
envrep['passed'] = bool(envres['passed']) and all(d['passed'] for d in envrep['devices'].values())
(folder/'validation.json').write_text(json.dumps(envrep, indent=2)+'\n')
new[envname] = dict(report=CK+envname+'/validation.json', sha256=sha(folder/'validation.json'), kind=old[envname]['kind'], rid='linux-x64',
                    assertions=[dict(pointer=a['pointer'], equals=a['equals']) for a in old[envname]['assertions']],
                    sourceSha256={f: sha(R/f) for f in old[envname]['sourceSha256']})
for a in new[envname]['assertions']:
    assert report_get(envrep, a['pointer']) == a['equals'], (a, report_get(envrep, a['pointer']))
assert set(new) == set(old), set(old) ^ set(new)
ev['checkpoints'] = {k: new[k] for k in old}
(R/'compatibility/evidence.json').write_text(json.dumps(ev, indent=2, ensure_ascii=False)+'\n')
print(len(new), 'evidence entries re-recorded')
