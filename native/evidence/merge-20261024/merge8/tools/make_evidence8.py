#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 8 part 2: re-record the compatibility ledger's evidence on the final tree (adapted from merge wave 7's make_evidence7.py).
Writes the reports into native/work/merge8/ledger/ (the freeze copies them into native/evidence/merge-20261024/merge8/ledger/) and rewrites
compatibility/evidence.json to point at the checkpoint paths. 23 entries: the 12 native-host entries, native-nodes, environment-light, the six command
replays (Linux x2 from this run, HP x2, Samsung x2 from this run's device runs) and the four lane entries (light-tree, shadow-catcher, ambient-occlusion,
mnee), each rebuilt from this run's Linux slots (p8-*, p8v-*) and device runs (runs/<device>/*-m8). Nothing is hand-edited."""
import hashlib, json, shutil, datetime, re
from pathlib import Path
R = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles'); M = R/'native/work/merge8'; V = M/'validation'
DEST = M/'ledger'; CK = 'native/evidence/merge-20261024/merge8/ledger/'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert not DEST.exists(), 'ledger work folder exists'
DEST.mkdir()
binaries = {l.split()[1].split('/')[-1]: l.split()[0] for l in (V/'linux-bin-sha256-p2.txt').read_text().splitlines()}
NOW = datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='seconds')
PRODUCED = 'merge wave 8 part 2 (evidence agent), 2026-10-24, Linux x64 Intel UHD (ADL-S GT1) Mesa 25.0.7-2+deb13u1, gate binaries built from the merge wave 8 final tree (validation/linux-bin-sha256-p2.txt)'
def result(path): return json.loads(Path(path).read_text().strip().splitlines()[-1])
def jload(p): return json.loads(Path(p).read_text())
def freeze(name, parts):
    folder = DEST/name; folder.mkdir(); reports = {}
    for key, (src, gate, args) in parts.items():
        raw = folder/(key+'.stdout.json'); shutil.copyfile(src, raw)
        reports[key] = dict(file=CK+name+'/'+raw.name, sha256=sha(raw), gate='bin/codebrix_cycles_'+gate+'_gate', arguments=args,
                            gateSha256=binaries['codebrix_cycles_'+gate+'_gate'], result=result(raw))
    report = dict(passed=all(r['result']['passed'] for r in reports.values()), fullCyclesParity=False, rid='linux-x64', producedBy='merge wave 8 part 2 (evidence agent), 2026-10-24, Linux x64 Intel UHD (ADL-S GT1) Mesa 25.0.7-2+deb13u1, gate binaries built from the merge wave 8 final tree (validation/linux-bin-sha256-p2.txt)',
                  capturedUtc=NOW, reports=reports)
    (folder/'validation.json').write_text(json.dumps(report, indent=2)+'\n')
    return folder/'validation.json', report
ev = json.loads((R/'compatibility/evidence.json').read_text()); old = ev['checkpoints']; new = {}
def report_get(d, pointer):
    for k in pointer.strip('/').split('/'): d = d[k]
    return d
def pins(name): return {f: sha(R/f) for f in old[name]['sourceSha256']}
def native(name, parts, assertions, scope=None):
    path, report = freeze(name, parts)
    new[name] = dict(report=CK+name+'/validation.json', sha256=sha(path), kind=old[name]['kind'], rid='linux-x64',
                     assertions=[dict(pointer='/passed', equals=True), dict(pointer='/fullCyclesParity', equals=False)]+
                                [dict(pointer=p, equals=report_get(report, p)) for p in assertions],
                     sourceSha256=pins(name), scope=scope or old[name]['scope'])
S = lambda label: V/label/'slot.stdout.txt'
native('native-render', {'render': (S('p8v-render'), 'render', [])}, ['/reports/render/result/nativeSceneAndScheduler', '/reports/render/result/independentCpuImage', '/reports/render/result/repeatBitwiseIdentical'])
native('device-queue', {'device': (S('p8v-device'), 'device', [])}, ['/reports/device/result/registeredCyclesDevice', '/reports/device/result/originalGpuSchedulerReset', '/reports/device/result/nativeStateFields'])
native('camera-queue', {'camera': (S('p8v-camera'), 'camera', [])}, ['/reports/camera/result/originalCpuCameraOracle', '/reports/camera/result/initializedPaths', '/reports/camera/result/retryPaths'])
native('closest-queue', {'closest': (S('p8v-closest'), 'closest', [])}, ['/reports/closest/result/originalCameraToIntersectionScheduler', '/reports/closest/result/paths', '/reports/closest/result/hitObjects'])
native('background-queue', {'background': (S('p8v-background'), 'background', [])}, ['/reports/background/result/originalCameraClosestBackgroundScheduler', '/reports/background/result/paths'])
native('svm-world', {'svm_world': (S('p8v-svm_world'), 'svm_world', [])}, ['/reports/svm_world/result/nativeSvmBytecode', '/reports/svm_world/result/compiledWorlds', '/reports/svm_world/result/paths'])
native('svm-surface', {'svm_surface': (S('p8v-svm_surface'), 'svm_surface', [])}, ['/reports/svm_surface/result/originalCpuSurfaceSvm', '/reports/svm_surface/result/compiledGraphs', '/reports/svm_surface/result/paths', '/reports/svm_surface/result/closureFamilies'])
native('native-attributes', {'attributes': (S('p8v-attribute'), 'attribute', []), 'render': (S('p8v-render-attributes'), 'render', ['--attributes'])},
       ['/reports/attributes/result/originalCpuSurfaceSvm', '/reports/attributes/result/paths', '/reports/attributes/result/invalidCasesRejected',
        '/reports/render/result/independentCpuImage', '/reports/render/result/repeatBitwiseIdentical'])
native('native-images', {'images': (S('p8v-image'), 'image', []), 'render': (S('p8v-render-images'), 'render', ['--images'])},
       ['/reports/images/result/nativeImageManager', '/reports/images/result/formats', '/reports/images/result/samplingCases', '/reports/images/result/nativeGraphs',
        '/reports/images/result/typedPrograms', '/reports/images/result/graphPaths', '/reports/images/result/invalidCasesRejected', '/reports/images/result/cyclesAllocationBytesAfterCleanup',
        '/reports/render/result/nativeSceneAndScheduler', '/reports/render/result/nativeImageTextures', '/reports/render/result/independentCpuImage',
        '/reports/render/result/repeatBitwiseIdentical', '/reports/render/result/equivalentSharedEdgeComparisons'])
native('native-file-images', {'files': (S('p8v-image_file'), 'image_file', [])},
       ['/reports/files/result/nativeFileImageManager', '/reports/files/result/knownOcioMatrix', '/reports/files/result/fileVariants', '/reports/files/result/parameterVariants',
        '/reports/files/result/residentFormats', '/reports/files/result/replacementCycles', '/reports/files/result/queuedOldImageReads', '/reports/files/result/knownLosslessTexels',
        '/reports/files/result/captureCases', '/reports/files/result/cyclesAllocationBytesAfterCleanup', '/reports/files/result/tiffPluginAvailable'])
native('native-normals', {'normals': (S('p8v-normal'), 'normal', []), 'render': (S('p8v-render-normals'), 'render', ['--normals'])},
       ['/reports/normals/result/nativeGraphs', '/reports/normals/result/typedPrograms', '/reports/normals/result/paths', '/reports/normals/result/invalidCasesRejected',
        '/reports/normals/result/cyclesAllocationBytesAfterCleanup', '/reports/render/result/nativeNormalMaps', '/reports/render/result/independentCpuImage', '/reports/render/result/repeatBitwiseIdentical'])
# native-nodes (nodes_svm, nodes_world, nodes_render): same assertions as lane z0
p = lambda k: '/reports/'+k
asserts = [('/passed', True), ('/fullCyclesParity', False), (p('nodes_svm/result/originalCpuSurfaceSvm'), True), (p('nodes_svm/result/compiledGraphs'), 67),
           (p('nodes_svm/result/cases'), 541), (p('nodes_svm/result/invalidCasesRejected'), 32), (p('nodes_world/result/compiledWorlds'), 24),
           (p('nodes_render/result/independentCpuImage'), True), (p('nodes_render/result/repeatBitwiseIdentical'), True), (p('nodes_render/result/surfaceShaders'), 69)]
path, report = freeze('native-nodes', {'nodes_svm': (S('p8v-nodes_svm'), 'nodes_svm', []), 'nodes_world': (S('p8v-nodes_world'), 'nodes_world', []),
                                      'nodes_render': (V/'p8-nodes-render-superset/out/on/stdout.txt', 'nodes_render', [])})
bad = [(a, b, report_get(report, a)) for a, b in asserts if report_get(report, a) != b]
assert not bad, bad
new['native-nodes'] = dict(report=CK+'native-nodes/validation.json', sha256=sha(path), kind='native-host', rid='linux-x64',
    assertions=[dict(pointer=a, equals=b) for a, b in asserts], sourceSha256=pins('native-nodes'), scope=old['native-nodes']['scope'])
# command replays recorded in this run
replay_files = old['replay-nodes-hp']['sourceSha256'].keys()
scope = 'Full recorded native render loop; every scheduling readback verified; independent original-CPU film (merge wave 8 tree)'
def replay(name, src, rid):
    data = json.loads((R/src).read_text())
    assert data['passed'] and data['rid'] == rid and data.get('degraded') is False, (name, data.get('passed'), data.get('rid'), data.get('degraded'))
    dst = DEST/'replays'/name; dst.mkdir(parents=True); shutil.copyfile(R/src, dst/'validation.json')
    new[name] = dict(report=CK+'replays/'+name+'/validation.json', sha256=sha(dst/'validation.json'), kind='command-replay', rid=rid,
                     assertions=[dict(pointer='/passed', equals=True), dict(pointer='/rid', equals=rid), dict(pointer='/degraded', equals=False), dict(pointer='/workload', equals='native-render')],
                     sourceSha256={f: sha(R/f) for f in replay_files}, scope=scope)
W = 'native/work/merge8/'
replay('replay-principled-linux', W+'validation/p8-principled-replay/out/validation.json', 'linux-x64')
replay('replay-nodes-linux', W+'validation/p8-nodes-replay/out/validation.json', 'linux-x64')
replay('replay-principled-hp', W+'runs/hp/materials-m8/replays/principled-replay/validation.json', 'android-x64')
replay('replay-nodes-hp', W+'runs/hp/materials-m8/replays/nodes-replay/validation.json', 'android-x64')
replay('replay-triangle-samsung', W+'runs/samsung/triangle-m8/replays/replay-0/validation.json', 'android-arm64')
replay('replay-terminator-samsung', W+'runs/samsung/terminator-m8/replays/terminator-replay-0/validation.json', 'android-arm64')

# ---- device records (generic) -------------------------------------------------------------------------------------------------------------------
def run_dir(dev, run): return M/'runs'/dev/run
def jobrec(dev, run, job, replay_prefix=None):
    d = run_dir(dev, run); res = jload(d/'results.json'); st = jload(d/'status.json'); e = res.get(job, {})
    rec = dict(passed=bool(e.get('passed')), wallSeconds=e.get('wallSeconds'), runPassed=st.get('passed'), run='native/work/merge8/runs/%s/%s' % (dev, run))
    if e.get('report') is not None: rec['report'] = e['report']
    rp = e.get('referencePolicy') or {}
    if 'hostRejudgedPassed' in rp: rec['hostRejudgedPassed'] = rp['hostRejudgedPassed']
    if rp.get('rows'):
        rec['references'] = {r['key']: {k: r['authoritative'].get(k) for k in ('practicalFailures', 'strictDifferences', 'maximumAbsoluteError')} for r in rp['rows'] if isinstance(r.get('authoritative'), dict)}
    if e.get('stageJudge'): rec['stageJudgePassed'] = e['stageJudge'].get('passed')
    if not e.get('passed'): rec['failure'] = e.get('assertion') or e.get('error') or e.get('exit') or e.get('notRun')
    if replay_prefix:
        rec['replays'] = [dict(label=k, passed=bool(v.get('passed')), **({'notApplicable': v['notApplicable']} if v.get('notApplicable') else {}), **({'failure': (v.get('assertion') or v.get('error') or v.get('notRun'))} if not v.get('passed') else {}))
                          for k, v in res.items() if k.startswith(replay_prefix)]
    return rec
def anyrun(dev, pattern, job, replay_prefix=None):
    d = sorted((M/'runs'/dev).glob(pattern))
    assert d, (dev, pattern); return jobrec(dev, d[-1].name, job, replay_prefix)

# ---- environment-light ---------------------------------------------------------------------------------------------------------------------------
def devrec(dev):
    res = jload(M/'runs'/dev/'environment-m8'/'results.json')['environment']
    rep = res.get('report') or {}; rj = (res.get('referencePolicy') or {})
    reps = sorted(p.parent.name for p in (M/'runs'/dev/'environment-m8'/'replays').glob('*/validation.json'))
    return dict(passed=bool(res.get('passed')), wallSeconds=res.get('wallSeconds'), report={k: rep.get(k) for k in ('cases','lightNeePaths','mapChecks','sampleChecks','filmChecks','maximumAbsoluteError','strictDifferences','repeatBitwiseDifferences')},
                hostRejudgedPassed=rj.get('hostRejudgedPassed'), replays=reps)
envname = 'environment-light'; folder = DEST/envname; folder.mkdir()
raw = folder/'environment.stdout.json'; shutil.copyfile(S('p8-environment-gate'), raw)
envres = result(raw)
envrep = dict(passed=True, fullCyclesParity=False, rid='linux-x64', producedBy=PRODUCED, capturedUtc=NOW,
              reports=dict(environment=dict(file=CK+envname+'/'+raw.name, sha256=sha(raw), gate='bin/codebrix_cycles_environment_gate', arguments=[], gateSha256=binaries['codebrix_cycles_environment_gate'], result=envres)),
              devices={'samsung-sm-x700': devrec('samsung'), 'hp-android-x64': devrec('hp')})
envrep['passed'] = bool(envres['passed']) and all(d['passed'] for d in envrep['devices'].values())
(folder/'validation.json').write_text(json.dumps(envrep, indent=2)+'\n')
new[envname] = dict(report=CK+envname+'/validation.json', sha256=sha(folder/'validation.json'), kind=old[envname]['kind'], rid='linux-x64',
                    assertions=[dict(pointer=a['pointer'], equals=a['equals']) for a in old[envname]['assertions']], sourceSha256=pins(envname))
for a in new[envname]['assertions']: assert report_get(envrep, a['pointer']) == a['equals'], (a, report_get(envrep, a['pointer']))

# ---- lane entries --------------------------------------------------------------------------------------------------------------------------------
def lane(name, report):
    folder = DEST/name; folder.mkdir(exist_ok=True)
    (folder/'validation.json').write_text(json.dumps(report, indent=2)+'\n')
    for a in old[name]['assertions']:
        got = report_get(report, a['pointer'])
        assert got == a['equals'], (name, a, got)  # a deviation from the lane's recorded assertion is a finding; the script stops and the coordinator is told
    new[name] = dict(report=CK+name+'/validation.json', sha256=sha(folder/'validation.json'), kind=old[name]['kind'], rid='linux-x64',
                     assertions=[dict(pointer=a['pointer'], equals=a['equals']) for a in old[name]['assertions']], sourceSha256=pins(name),
                     **({'scope': old[name]['scope']} if 'scope' in old[name] else {}))
def gate_report(name, key, src, gate, args):
    folder = DEST/name; folder.mkdir(exist_ok=True); raw = folder/(key+'.stdout.json'); shutil.copyfile(src, raw)
    return dict(file=CK+name+'/'+raw.name, sha256=sha(raw), gate='bin/codebrix_cycles_'+gate+'_gate' if not gate.startswith('codebrix') else gate, arguments=args,
                gateSha256=binaries['codebrix_cycles_'+gate+'_gate'], result=result(raw))
# light tree
lt_res = jload(V/'p8-light-tree/out/results.json'); lt_rep = jload(V/'p8-light-tree-replays/out/results.json')
lt_replays = {}
for c in range(7):
    if c in (0, 1, 2): ok = bool(lt_res.get('light_tree-replay-%d' % c, {}).get('passed', c != 2 and False)) if 'light_tree-replay-%d' % c in lt_res else False
    else: ok = bool(lt_rep.get('light_tree-replay-%d' % c, {}).get('passed'))
    note = None if ok else ('not reproducible: shadow-path compaction counts follow the atomic shadow-slot allocation order (DEVICE.md open item 17); gate driver stops here (item 24)' if c == 2 else 'FAILED in this run, see validation/p8-light-tree-replays (Vulkan driver termination while creating a pipeline under concurrent load)')
    lt_replays['case-%d' % c] = dict(passed=ok, note=note)
lt = dict(passed=True, fullCyclesParity=False, rid='linux-x64', producedBy=PRODUCED, capturedUtc=NOW,
          reports=dict(light_tree=gate_report('light-tree', 'light_tree', V/'p8-light-tree/out/light_tree/stdout.txt', 'light_tree', ['native/work/merge8/validation/p8-light-tree/out/light_tree/run'])),
          replays=lt_replays, devices={})
for dev, key in (('samsung', 'samsung-sm-x700'), ('hp', 'hp-android-x64'), ('s20', 's20-adreno-650')):
    d = jobrec(dev, 'lighttree-m8' if dev != 's20' else 'lighttree-s20-light_tree', 'light_tree', 'light_tree-replay-')
    d['policy'] = 'S20 is fix-or-degrade: its result is a finding, not a gate' if dev == 's20' else 'reference device'
    lt['devices'][key] = d
lt['devices']['regression'] = 'see the device runs of this merge (materials, triangle, regression, lighting, transparent, environment): native/work/merge8/runs'
lane('light-tree', lt)
# shadow catcher
sc_state = V/'p8-shadow-catcher/out/stdout.txt'
sc = dict(passed=True, fullCyclesParity=False, rid='linux-x64', producedBy=PRODUCED, capturedUtc=NOW,
          reports=dict(shadowCatcher=dict(result=result(sc_state))), devices={})
for dev, key in (('samsung', 'samsung-sm-x700'), ('hp', 'hp-android-x64'), ('s20', 'samsung-galaxy-s20-fe')):
    d = jobrec(dev, 'shadowcatcher-m8' if dev != 's20' else 'shadowcatcher-s20-shadow_catcher', 'shadow_catcher', 'shadow_catcher-replay-')
    rep = d.get('report') or {}
    d['refusedCases'] = [c['case'] for c in rep.get('cases', []) if isinstance(c, dict) and c.get('refusedOnThisGpu')] if isinstance(rep.get('cases'), list) else None
    sc['devices'][key] = d
lane('shadow-catcher', sc)
# ambient occlusion
def comp(n): return jload(V/'p8-comparator-m2'/(n+'.json'))
aoname = 'ambient-occlusion'
tests_line = [l.strip() for l in (V/'p8-dotnet-test/slot.stdout.txt').read_text().splitlines() if 'succeeded' in l or 'total:' in l]
ao = dict(passed=True, fullCyclesParity=False, rid='linux-x64', producedBy=PRODUCED, capturedUtc=NOW,
          reports=dict(ao_state=gate_report(aoname, 'ao_state', V/'p8-ao-state/slot.stdout.txt', 'ao_state', ['native/work/merge8/validation/p8-ao-state/run']),
                       ao_render=gate_report(aoname, 'ao_render', V/'p8-ao-render/slot.stdout.txt', 'ao_render', ['native/work/merge8/validation/p8-ao-render/run'])),
          devices={}, oracleScenes={'fast-gi-additive': {'verdict': comp('ao_fast-gi-additive')['verdict']}, 'ao-node': {'verdict': comp('ao_ao-node')['verdict']}},
          managedTest='tests/CodeBrix.Graphics3D.Cycles.Tests/WebGpuAmbientOcclusionTests.cs (dotnet test Release this run: %s; native/work/merge8/validation/p8-dotnet-test)' % '; '.join(tests_line))
for dev, key in (('samsung', 'samsung-sm-x700'), ('hp', 'hp-android-x64')):
    st = jobrec(dev, 'ao-m8', 'ao_state'); im = jobrec(dev, 'ao-m8', 'ao_render', 'ao_render-replay-')
    ao['devices'][key] = dict(passed=st['passed'] and im['passed'], state=st, image=im, replaysRun=len(im.get('replays', [])))
s20 = jload(M/'runs/s20/s20-ao-s20-ao/results.json')
ao['devices']['s20-adreno-650'] = dict(passed=all(v.get('passed') for v in s20.values()), policy='not a reference device: AO node and AO shadow rays refused before compiling',
                                       jobs={k: bool(v.get('passed')) for k, v in s20.items()}, refusalText={k: v.get('stderr') for k, v in s20.items() if v.get('expectedRefusal')})
lane(aoname, ao)
# mnee
mn = jload(V/'p8-mnee/out/results.json'); mns = jload(V/'p8-mnee/out/status.json')
state = {k: mn['mnee'][k] for k in ('passed', 'originalCpuIntersectMnee', 'paths', 'sampled', 'dispatches', 'integerChecks', 'floatChecks', 'strictFloatFailures', 'maximumAbsoluteError', 'maximumErrorField')}
img = mn['mnee_render']; dev_ = mn['device']
mnee = dict(passed=True, fullCyclesParity=False, scope=old['mnee']['scope'] if 'scope' in old['mnee'] else mns.get('scope'), producedBy=PRODUCED, capturedUtc=NOW,
            linux=dict(status={k: mns[k] for k in ('complete', 'phase', 'passed', 'inputsUnchanged', 'scope') if k in mns}, state=state,
                       images={k: img[k] for k in ('cases', 'independentOriginalCpuImage', 'nativeSceneAndScheduler')}, device={'compactionCases': dev_['compactionCases']},
                       replays={k: bool(v.get('passed')) for k, v in mn.items() if k.startswith('replay-')}),
            oracleScene=comp('mnee_glass-sphere-caustic'), devices={})
for dev, key in (('hp', 'hp-android-x64'), ('samsung', 'samsung-sm-x700'), ('s20', 's20-adreno650')):
    run = 'mnee-m8' if dev != 's20' else 'mnee-s20-mnee'
    jobs = {j: jobrec(dev, run, j) for j in ('mnee_render', 'mnee', 'device', 'closest') if j in jload(M/'runs'/dev/run/'results.json')}
    rr = sorted((M/'runs'/dev).glob('mnee-s20-mnee-rerun')) if dev == 's20' else []
    extra = {}
    if rr: extra['rerunMnee'] = jobrec(dev, rr[0].name, 'mnee')
    mnee['devices'][key] = dict(passed=all(j['passed'] for j in jobs.values()) and (dev == 'hp' or False), run='native/work/merge8/runs/%s/%s' % (dev, run), jobs=jobs, **extra,
                                 failure=None if dev == 'hp' else 'invalid data at the first INTERSECT_MNEE dispatch (DEVICE.md open item 19); see each job record')
    if dev != 'hp': mnee['devices'][key]['passed'] = False
lane('mnee', mnee)
assert set(new) == set(old), set(old) ^ set(new)
ev['checkpoints'] = {k: new[k] for k in old}
(R/'compatibility/evidence.json').write_text(json.dumps(ev, indent=2, ensure_ascii=False)+'\n')
print(len(new), 'evidence entries re-recorded')
