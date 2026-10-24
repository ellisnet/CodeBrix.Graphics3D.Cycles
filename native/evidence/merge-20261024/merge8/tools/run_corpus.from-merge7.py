#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 3 copy of the Phase X1 runner (via Phase E1) of the Phase C2 (ARM64) runner, generalised to the Samsung SM-X700 (android-arm64) and the HP (android-x64,
cold Mesa compiles via MESA_SHADER_CACHE_DISABLE=true as in Phase C2 x64). Added: expected-refusal jobs.

Adapted from B2's run_samsung_timeout.py and B3's android_validate.py (same staging,
ELF/NEEDED review, runtime-hash checks, report assertions and exact-PID timeout
cleanup). Differences: one shared staged binary set per device (re-verified by hash
before every corpus), a declared corpus wall budget plus per-job timeout (the run is
terminated and reported on overrun), per-job CODEBRIX_CYCLES_PIPELINE_LOG, and native
command replays executed on the device directly from the capture the device produced.
The Linux gate scripts' assertions are mirrored per job; no criterion is relaxed.

Usage: run_corpus.py DEVICE CORPUS LABEL BUDGET_S JOB_TIMEOUT_S [--pipeline-budget-ms N] [--jobs a,b] [--replays all|N]
"""
import argparse, json, math, os, re, shlex, shutil, struct, subprocess, sys, time, datetime, array
from pathlib import Path
HERE = Path(__file__).resolve().parent; C2 = HERE.parent; REPO = HERE.parents[3]  # merge wave 7: C2 = native/work/merge8 (copied from merge wave 6) (lane S3's stage-exchange runner, from merge wave 5's)
BUILD_ROOT = REPO/'native/work/merge5'  # merge wave 6: the Android gate build trees and replay build stay in merge 5's folder (built in place)
sys.path.insert(0, str(REPO/'native/tools'))
from build import PACKAGE
from device_gate import sha, save
from render_gate import inputs
from android_toolchain import AndroidToolchain, TARGETS
from build_cycles_android import dependencies
from android_host_gate import PLATFORM
import android_reference
import render_replay
render_replay.BASE = C2/'replay-build'  # merge wave 7: fresh replay builds (render_replay.cpp v9)

ADB = '/home/jeremy/Android/Sdk/platform-tools/adb'
DEVICES = {'samsung': ('R52W207XQ1V', 'SM-X700'), 'hp': ('192.168.86.225:5555', '87FE'), 'pixel': ('98271FFBA0048D', 'Pixel 4 XL'), 's20': ('RFCRB0MPHYH', 'SM-G781U1')}
RIDS = {'samsung': 'android-arm64', 'hp': 'android-x64', 'pixel': 'android-arm64', 's20': 'android-arm64'}; ABIS = {'android-arm64': 'arm64-v8a', 'android-x64': 'x86_64'}
RUNTIME_SHAS = {'android-arm64': '8113cbfa287b5ffc3d9b3c092ddc5ce543b8390abf05eefdde86cf69792fed52', 'android-x64': 'c2b0105f28ba01068cd9b6af6d560315af90b4b2cab6c40fcb78813bd9bb4380'}
PRACTICAL = '5e-5 + 2e-4*abs(reference)'; STRICT = '1e-7 + 1e-7*abs(reference)'

# ---- corpus definitions: (label, gate, args, env, check) and replays (label, capture-relative path)
def J(label, gate, args=(), env=None, check=None, refuse=None): return dict(label=label, gate=gate, args=list(args), env=env or {}, check=check, refuse=refuse)

def img(cases, **extra):
    def check(r):
        assert r['cases'] == cases and r['nativeSceneAndScheduler'] and r['independentOriginalCpuImage']
        for k, v in extra.items():
            if k == 'shadowPathsMin': assert r['shadowPaths'] > v
            elif k == 'pointSun': assert r['pointSunMisEnabled']
            elif k == 'transmissionMin': assert r['transmissionPaths'] > v
            elif k == 'forwardMin': assert r['forwardLightPaths'] > v
            elif k == 'deformingMin': assert r['deformingSurfacePaths'] > v
            elif k == 'deformingZero': assert r['deformingSurfacePaths'] == 0
    return check
def c_glass_svm(r): assert r['originalCpuSurfaceSvm'] and r['compiledGraphs'] == 20 and r['closureFamilies'] == 7 and r['invalidCasesRejected'] == 17
def c_glass_bounce(r): assert r['originalCpuSurfaceIntegrator'] and r['cases'] == 29 and r['invalidCasesRejected'] == 21
def c_render(r): assert r['nativeSceneAndScheduler'] and r['independentCpuImage'] and r['repeatBitwiseIdentical']
def c_mf_bounce(r): assert r['originalCpuSurfaceIntegrator'] and r['cases'] == 22 and r['paths'] == 21420 and r['invalidCasesRejected'] == 21 and r['zeroCapacityPathsRejected'] == 1020
def c_mf_svm(r): assert r['originalCpuSurfaceSvm']
def c_tri_sample(r): assert r['samples'] == 4096 and r['guardCases'] == 5
def c_tri_image(r):
    # Merge wave 3 (X2): six image modes incl. the static instanced emitter (mode 4); no rejection case remains.
    assert r['cases'] == 6 and r['meshMisPaths'] > 10 and r['unsupportedEmitterConfigurationsRejected'] == 0
    assert r['nativeSceneAndScheduler'] and r['independentOriginalCpuImage'] and not r['fullLightingMisProven']
def c_surface(r): assert r['invalidCasesRejected'] == 21
def c_shadow(r):
    assert r['originalCpuShadowKernels'] and r['originalGpuScheduler'] and r['cases'] == 60 and r['paths'] == 3846
    assert r['directDispatchCases'] == 30 and r['invalidCasesRejected'] == 18 and r['blocked'] > 100 and r['unoccluded'] > 100 and r['selfOrLightExclusions'] > 50
def c_closest(r): assert r['cases'] == 22 and r['invalidCasesRejected'] == 21 and r['originalCameraToIntersectionScheduler']
def c_vol_state(r): assert r['originalCpuVolumeIntegrator'] and r['cases'] == 24 and r['invalidCasesRejected'] == 18 and min(r[k] for k in ('scatterEvents', 'transmitEvents', 'terminatedEvents')) > 0
def c_vol_render(absorption):
    def check(r):
        assert r['independentCpuImage'] and r['repeatBitwiseIdentical'] and r['volumeTransmitEvents'] > 0
        assert (r['volumeScatterEvents'] == 0) == absorption
    return check
def c_closed(inside):
    def check(r):
        assert r['independentCpuImage'] and r['repeatBitwiseIdentical'] and r['volumeTransmitEvents'] > 0 and r['volumeScatterEvents'] > 0
        assert r['maxVolumeBoundaryBounce'] >= 2 and bool(r['volumeStackInitializations']) == inside
    return check
def c_boundary(r):
    assert r['originalCpuVolumeBoundaries'] and r['cases'] == 24 and r['densityInputs'] == 800 and r['malformedCasesRejected'] == 6
    assert all(r[k] > 0 for k in ('enteredVolumes','exitedVolumes','initializedInside','initializedOutside','initializedOverlap','volumeOnlyBoundaries','glossyReflections','glossyTransmissions'))
def c_compaction(r): assert r['malformedCasesRejected'] == 1 and r['cases'] == 0
def c_cam(static):
    def check(r):
        if static: assert r['cases'] == 24 and r['invalidCasesRejected'] == 12
        else: assert r['cameraMotionCases'] == 24 and r['invalidCasesRejected'] == 19
    return check
def c_render_motion(r): assert r['independentCpuImage']
def c_prin_svm(r):
    assert r['originalCpuSurfaceSvm'] and r['compiledGraphs'] == 20 and r['closureFamilies'] == 10
    assert r['invalidCasesRejected'] == 19 and r['nativeUnsupportedFeatureRejections'] == 3
def c_prin_bounce(r): assert r['originalCpuSurfaceIntegrator'] and r['cases'] == 29 and r['invalidCasesRejected'] == 21
def c_nodes_svm(r): assert r['originalCpuSurfaceSvm'] and r['compiledGraphs'] == 67 and r['closureFamilies'] == 5 and r['invalidCasesRejected'] == 32
def c_nodes_world(r): assert r['compiledWorlds'] == 24
def c_environment(r): assert r['cases'] == 12 and r['nativeSceneAndScheduler'] and r['independentOriginalCpuImage'] and r['lightNeePaths'] > 1000
def c_emitter(r): pass
def c_light_state(r): pass
def c_shader_data(r): pass

CORPORA = {
    # Merge wave 7 part 2 (from lane m1-lighting's m1l corpus): the environment gate (12 cases) and its on-device replays
    'environment': [J('environment', 'environment', check=c_environment)],
    # E1: specialization-off refusal on Android (item 5) and the specialized glass film
    'spec-refusal': [J('glass_render-spec', 'glass_render', check=c_render),
                     J('glass_render-nospec-refused', 'glass_render', env={'CODEBRIX_CYCLES_SHADER_SPECIALIZATION': '0'}, refuse='refused on Android'),
                     # Merge wave 4 (lane L): the superset diagnostic is refused on Android exactly like =0.
                     J('glass_render-superset-refused', 'glass_render', env={'CODEBRIX_CYCLES_SHADER_SPECIALIZATION': 'superset'}, refuse='refused on Android')],
    # E1: regression set compared bit-for-bit with Phase C2
    'regression': [J('glass_render', 'glass_render', check=c_render), J('lighting_mis', 'lighting_mis', check=img(8)),
                   J('transparent_lighting', 'transparent_lighting', check=img(8))],
    # 1: specialization on/off bit identity (glass_render); compile timings come from every corpus' pipeline logs
    'spec': [J('glass_render-spec', 'glass_render', check=c_render),
             J('glass_render-nospec', 'glass_render', env={'CODEBRIX_CYCLES_SHADER_SPECIALIZATION': '0'}, check=c_render)],
    # 2: emissive triangle lights
    'triangle': [J('triangle_sample', 'triangle_sample', check=c_tri_sample), J('surface', 'surface', check=c_surface),
                 J('triangle_lighting', 'triangle_lighting', check=c_tri_image)],
    # 3: transparent-shadow split (v7)
    'transparent': [J('transparent_shadow', 'transparent_shadow'), J('transparent_lighting', 'transparent_lighting', check=img(8))],
    # 4: ARM normalization mode + rounded adapters
    'arm': [J('glass_bounce', 'glass_bounce', check=c_glass_bounce), J('glass_render', 'glass_render', check=c_render),
            J('microfacet_render', 'microfacet_render', check=c_render)],
    'arm-pixel': [J('glass_bounce', 'glass_bounce', check=c_glass_bounce), J('glass_render', 'glass_render', check=c_render)],
    # 5a: lighting family
    'lighting': [J('lighting', 'lighting', check=img(8, shadowPathsMin=1000)), J('light_state', 'light_state', check=c_light_state),
                 J('lighting_mis', 'lighting_mis', check=img(8)), J('broad_light_state', 'broad_light_state', check=c_light_state),
                 J('broad_lighting', 'broad_lighting', check=img(12)),
                 J('feature_combination', 'feature_combination', check=img(8, pointSun=1, transmissionMin=100, forwardMin=100))],
    # 5b: motion
    'motion': [J('object-moving-images', 'object_motion_render', check=img(8, pointSun=1, shadowPathsMin=100, forwardMin=100)),
               J('object-stationary-images', 'object_motion_render', ['--stationary'], check=img(8, pointSun=1, shadowPathsMin=100, forwardMin=100)),
               J('deforming-moving-images', 'deforming_motion_render', check=img(8, pointSun=1, shadowPathsMin=100, forwardMin=100, deformingMin=800)),
               J('deforming-stationary-images', 'deforming_motion_render', ['--stationary'], check=img(8, pointSun=1, shadowPathsMin=100, forwardMin=100, deformingZero=1)),
               J('camera-static', 'camera', check=c_cam(True)), J('camera-motion', 'camera', ['--motion'], check=c_cam(False)),
               J('render-static', 'render', check=c_render_motion),
               *[J('render-motion-'+str(m), 'render', ['--motion='+str(m)], check=c_render_motion) for m in range(8)]],
    # 5c: glass/GGX remainder
    'ggx': [J('glass_svm', 'glass_svm', check=c_glass_svm), J('microfacet_svm', 'microfacet_svm', check=c_mf_svm),
            J('microfacet_bounce', 'microfacet_bounce', check=c_mf_bounce), J('shader_data', 'shader_data', check=c_shader_data),
            J('closest', 'closest', check=c_closest), J('shadow', 'shadow', check=c_shadow)],
    # 5d: finite homogeneous volumes
    'volume': [J('volume-state', 'volume', check=c_vol_state),
               J('volume-absorption', 'volume_render', env={'CODEBRIX_VOLUME_ABSORPTION_ONLY': '1'}, check=c_vol_render(True)),
               J('volume-isotropic', 'volume_render', env={'CODEBRIX_VOLUME_G': '0'}, check=c_vol_render(False)),
               J('volume-forward', 'volume_render', env={'CODEBRIX_VOLUME_G': '.45'}, check=c_vol_render(False)),
               J('volume-backward', 'volume_render', env={'CODEBRIX_VOLUME_G': '-.65'}, check=c_vol_render(False)),
               J('boundary-state', 'volume_boundary', check=c_boundary),
               J('boundary-compaction-guard', 'volume_boundary', env={'CODEBRIX_VOLUME_COMPACTION_CASE': '1'}, check=c_compaction),
               J('closed-single', 'closed_volume_render', env={'CODEBRIX_VOLUME_CLOSED_COUNT': '1'}, check=c_closed(False)),
               J('closed-overlap', 'closed_volume_render', env={'CODEBRIX_VOLUME_CLOSED_COUNT': '4'}, check=c_closed(False)),
               J('closed-inside', 'closed_volume_render', env={'CODEBRIX_VOLUME_CLOSED_COUNT': '4', 'CODEBRIX_VOLUME_CAMERA_INSIDE': '1'}, check=c_closed(True))],
    # Lane A650 (copied from the lane's runner): texture, colour and utility node scenes are refused on the Adreno 650 before any pipeline compiles.
    'a650-refusals': [J('nodes_world-refused', 'nodes_world', refuse='are not supported on Adreno (TM) 650 GPUs'),
                      J('nodes_svm-refused', 'nodes_svm', refuse='are not supported on Adreno (TM) 650 GPUs'),
                      J('nodes_render-refused', 'nodes_render', refuse='are not supported on Adreno (TM) 650 GPUs')],
    # 6: emitter-sample probe (specialized module)
    'emitter': [J('emitter_sample', 'emitter_sample', check=c_emitter)],
    # Merge wave 3 additions
    # E3: shadow terminator offsets (lighting_gate.cpp variant, 10 cases)
    'terminator': [J('shadow_terminator', 'shadow_terminator', check=img(10))],
    # X2: closest/shadow precision fixtures (exact traversal); the gates assert 0 bit / 0 decision mismatches
    'precision': [J('closest', 'closest', check=c_closest), J('shadow', 'shadow', check=c_shadow)],
    # E2 (Pixel): the formerly failing corpora plus their regression neighbours
    'pixel-lighting': [J('lighting_mis', 'lighting_mis', check=img(8)),
                       J('feature_combination', 'feature_combination', check=img(8, pointSun=1, transmissionMin=100, forwardMin=100))],
    'pixel-motion': [J('object-moving-images', 'object_motion_render', check=img(8, pointSun=1, shadowPathsMin=100, forwardMin=100)),
                     J('object-stationary-images', 'object_motion_render', ['--stationary'], check=img(8, pointSun=1, shadowPathsMin=100, forwardMin=100)),
                     J('deforming-moving-images', 'deforming_motion_render', check=img(8, pointSun=1, shadowPathsMin=100, forwardMin=100, deformingMin=800)),
                     J('deforming-stationary-images', 'deforming_motion_render', ['--stationary'], check=img(8, pointSun=1, shadowPathsMin=100, forwardMin=100, deformingZero=1)),
                     J('camera-static', 'camera', check=c_cam(True)), J('camera-motion', 'camera', ['--motion'], check=c_cam(False))],
    # Merge wave 3 (materials M1+M2+M3): Principled and texture/colour/utility node gates, assertions mirrored
    # from native/tools/principled_gate.py and nodes_gate.py (specialized modules; SVM probe modules are scene-specialized since merge wave 2).
    'materials': [J('principled_svm', 'principled_svm', check=c_prin_svm), J('principled_bounce', 'principled_bounce', check=c_prin_bounce),
                  J('principled_render', 'principled_render', check=c_render),
                  J('nodes_svm', 'nodes_svm', check=c_nodes_svm), J('nodes_world', 'nodes_world', check=c_nodes_world),
                  J('nodes_render', 'nodes_render', check=c_render)],
    'pixel-volume': [J('boundary-state', 'volume_boundary', check=c_boundary),
                     J('closed-inside', 'closed_volume_render', env={'CODEBRIX_VOLUME_CLOSED_COUNT': '4', 'CODEBRIX_VOLUME_CAMERA_INSIDE': '1'}, check=c_closed(True))],
}
def scene_env(job):  # merge wave 6: scene parameters of a job for the Linux reference and stage-judge passes
    return {k: v for k, v in job['env'].items() if not k.startswith('CODEBRIX_CYCLES_')}
IMAGE_GATES = {'environment', 'triangle_lighting', 'shadow_terminator', 'transparent_lighting', 'lighting', 'lighting_mis', 'broad_lighting', 'feature_combination',
               'object_motion_render', 'deforming_motion_render'}
# replays: (label, job label, capture path relative to the job's remote 'run' prefix; '' = the prefix itself)
REPLAYS = {
    'environment': [('environment-replay-'+str(c), 'environment', 'case-'+str(c)) for c in range(12)],
    'triangle': [('replay-'+str(c), 'triangle_lighting', 'case-'+str(c)) for c in range(6)],
    'terminator': [('terminator-replay-'+str(c), 'shadow_terminator', 'case-'+str(c)) for c in range(10)],
    'precision': [('shadow-replay-'+m, 'shadow', '@run.capture-'+m) for m in ('direct', 'indexed')],
    'pixel-lighting': [('lighting_mis-replay-'+str(c), 'lighting_mis', 'case-'+str(c)) for c in range(8)] +
                      [('feature_combination-replay-'+str(c), 'feature_combination', 'case-'+str(c)) for c in range(8)],
    'pixel-volume': [('closed-replay-inside', 'closed-inside', '@')],
    'materials': [('principled-replay', 'principled_render', '@'), ('nodes-replay', 'nodes_render', '@')],
    'pixel-motion': [('object-replay-motion-'+str(c), 'object-moving-images', 'case-'+str(c)) for c in range(8)] +
                    [('deforming-replay-motion-'+str(c), 'deforming-moving-images', 'case-'+str(c)) for c in range(8)],
    'transparent': [('transparent_lighting-replay-'+str(c), 'transparent_lighting', 'case-'+str(c)) for c in range(8)] +
                   [('transparent-shadow-replay-'+m, 'transparent_shadow', '@run.capture-'+m) for m in ('direct', 'indexed')],
    'lighting': [('lighting-replay-'+str(c), 'lighting', 'case-'+str(c)) for c in range(8)] +
                [('lighting_mis-replay-'+str(c), 'lighting_mis', 'case-'+str(c)) for c in range(8)] +
                [('broad_lighting-replay-'+str(c), 'broad_lighting', 'case-'+str(c)) for c in range(12)] +
                [('feature_combination-replay-'+str(c), 'feature_combination', 'case-'+str(c)) for c in range(8)],
    'motion': [('object-replay-motion-'+str(c), 'object-moving-images', 'case-'+str(c)) for c in range(8)] +
              [('deforming-replay-motion-'+str(c), 'deforming-moving-images', 'case-'+str(c)) for c in range(8)],
    'volume': [('volume-replay-'+n, 'volume-'+n, '@') for n in ('absorption', 'isotropic', 'forward', 'backward')] +
              [('closed-replay-'+n, 'closed-'+n, '@') for n in ('single', 'overlap', 'inside')],
    'ggx': [('shadow-replay-'+m, 'shadow', '@run.capture-'+m) for m in ('direct', 'indexed')],
}

ap = argparse.ArgumentParser()
ap.add_argument('device', choices=DEVICES); ap.add_argument('corpus', choices=CORPORA); ap.add_argument('label')
ap.add_argument('budget', type=int); ap.add_argument('job_timeout', type=int)
ap.add_argument('--pipeline-budget-ms', type=int); ap.add_argument('--jobs'); ap.add_argument('--replays', default='all')
# X1: correctly rounded reference (native/tools/android_reference.py export root); jobs with an exported reference
# import it on the device (CODEBRIX_CYCLES_REFERENCE_IMPORT) and are re-judged on the host from the pulled files.
ap.add_argument('--reference', type=Path)
# X1 authoritative flow: a device dump pass writes the scene the device prepared, the Linux x64 (glibc) gate computes
# the reference from exactly that scene (android_reference.py scene-reference), the reference is pushed and the
# device run imports it with CODEBRIX_CYCLES_SCENE_VERIFY (its prepared scene must equal the dumped one).
ap.add_argument('--device-scene', action='store_true')
ap.add_argument('--mesa-scratch-cache', action='store_true')
ap.add_argument('--linux-bin', type=Path, default=REPO/'native/work/cycles-build/bin')
args = ap.parse_args()
if args.reference: args.reference = args.reference.resolve()
serial, model = DEVICES[args.device]; RID = RIDS[args.device]; RUNTIME_SHA = RUNTIME_SHAS[RID]; BUILD = BUILD_ROOT/('build-'+RID)/'android'/RID/'host'
jobs = CORPORA[args.corpus]
if args.jobs: jobs = [j for j in jobs if j['label'] in args.jobs.split(',')]
replays = REPLAYS.get(args.corpus, [])
if args.replays.startswith('perjob'):  # perjobK: the first K replays of each source job (declared subset)
    k = int(args.replays[6:]); seen = {}
    replays = [r for r in replays if seen.setdefault(r[1], []).append(r) or len(seen[r[1]]) <= k]
elif args.replays != 'all': replays = replays[:int(args.replays)]
replays = [r for r in replays if r[1] in {j['label'] for j in jobs}]
OUT = C2/'runs'/args.device/(args.corpus+'-'+args.label); OUT.mkdir(parents=True, exist_ok=False)
STAGE_REMOTE = '/data/local/tmp/codebrix-m8-stage'
REMOTE = '/data/local/tmp/codebrix-m8-'+args.corpus+'-'+args.label
T0 = time.time(); deadline = T0+args.budget
def ts(): return datetime.datetime.now().strftime('%H:%M:%S')
def log(msg):
    line = '['+ts()+'] '+args.device+' '+args.corpus+'-'+args.label+': '+msg
    print(line, flush=True)
    with (C2/'timeline.log').open('a') as f: f.write(line+'\n')
with (C2/'budgets.txt').open('a') as f:
    f.write('[%s] DECLARED device=%s serial=%s corpus=%s label=%s wallBudget=%ds perJobTimeout=%ds pipelineBudgetMs=%s jobs=%s replays=%d\n' % (
        ts(), args.device, serial, args.corpus, args.label, args.budget, args.job_timeout, args.pipeline_budget_ms or 'default(60000)',
        ','.join(j['label'] for j in jobs), len(replays)))
state = dict(complete=False, phase='stage', device=args.device, serial=serial, corpus=args.corpus, label=args.label,
             declaredBudgetSeconds=args.budget, perJobTimeoutSeconds=args.job_timeout, pipelineBudgetMs=args.pipeline_budget_ms,
             practicalCriterion=PRACTICAL, strictCriterion=STRICT, startedLocal=ts())
save(OUT/'status.json', state)
commands = []
def call(argv, name, timeout=120, check=True):
    command = [ADB, '-s', serial, *argv]; commands.append(dict(command=command, timeout=timeout)); save(OUT/'commands.json', commands)
    with (OUT/(name+'.stdout.txt')).open('w') as so, (OUT/(name+'.stderr.txt')).open('w') as se:
        r = subprocess.run(command, stdout=so, stderr=se, timeout=timeout)
    if check: r.check_returncode()
    return r.returncode, (OUT/(name+'.stdout.txt')).read_text()
def shell(argv, name, **kw): return call(['shell', shlex.join(argv)], name, **kw)
def kill_exact(pidfile, program, name):
    pid = shell(['cat', pidfile], name+'-timeout-pid', check=False)[1].strip()
    killed = False
    if pid.isdecimal():
        rc, path = shell(['readlink', '/proc/'+pid+'/exe'], name+'-timeout-executable', check=False)
        if rc == 0 and path.strip() == program: shell(['kill', '-KILL', pid], name+'-timeout-cleanup', check=False); killed = True
    left = shell(['ps', '-A', '-o', 'PID,ARGS'], name+'-post-timeout-processes', check=False)[1]
    return dict(pid=pid, killed=killed, remaining=[r for r in left.splitlines() if 'codebrix-m8-' in r])

sources = inputs(); pins = {}
EXES = sorted({'codebrix_cycles_'+j['gate']+'_gate' for j in jobs})
results = {}; failures = []
def film_floats(path):
    a = array.array('f'); a.frombytes(path.read_bytes()); return a
try:
    devices = subprocess.run([ADB, 'devices', '-l'], capture_output=True, text=True, check=True).stdout
    assert any(l.startswith(serial) and ' device ' in l for l in devices.splitlines()), 'device not attached'
    status = json.loads((BUILD/'status.json').read_text()); inv = json.loads((BUILD/'invocation.json').read_text())
    assert status['complete'] and status['passed']
    assert inv['sourceSha256'] == sources, 'Production sources changed since build'
    runtime = PACKAGE/'runtimes'/RID/'native/libcodebrix_wgpu.so'
    assert sha(runtime) == inv['runtimeSha256'] == RUNTIME_SHA
    staged = json.loads((C2/('staged-binaries-'+RID+'.json')).read_text())
    expected = {STAGE_REMOTE+'/'+f['remote']: f['sha256'] for f in staged['files']}
    for f in staged['files']: pins[f['path']] = f['sha256']
    for e in EXES: assert any(f['remote'] == 'bin/'+e for f in staged['files']), 'not staged: '+e
    meta = {k: shell(c, k)[1].strip() for k, c in {'api': ['getprop', 'ro.build.version.sdk'], 'abi': ['getprop', 'ro.product.cpu.abi'],
            'model': ['getprop', 'ro.product.model'], 'fingerprint': ['getprop', 'ro.build.fingerprint']}.items()}
    assert meta['abi'] == ABIS[RID] and meta['model'] == model; state['deviceProperties'] = meta
    processes = shell(['ps', '-A', '-o', 'PID,ARGS'], 'prior-processes')[1]
    assert not any('codebrix_' in l or 'codebrix-m8-' in l or 'render_replay' in l or 'compile_probe' in l for l in processes.splitlines()), 'Another codebrix process is running'
    hashes = shell(['sha256sum', *expected], 'device-hashes')[1]
    assert {l.split()[1]: l.split()[0] for l in hashes.splitlines()} == expected, 'Staged device binaries differ'
    assert expected[STAGE_REMOTE+'/lib/libcodebrix_wgpu.so'] == RUNTIME_SHA
    shell(['test', '!', '-e', REMOTE], 'unused-remote'); shell(['mkdir', '-p', REMOTE], 'remote-dir')
    state['stagedVerified'] = True
    REFERENCE_KINDS = {'triangle_sample': 'samples', 'emitter_sample': 'samples', 'glass_render': 'render', 'principled_render': 'render', 'nodes_render': 'render',
                       'nodes_svm': 'svm', 'principled_svm': 'svm',  # lane S: render and SVM probe gates take the device-scene flow
                       # merge wave 6 (S3 Q2, decided): the other render_gate.cpp binaries join the device-scene flow and the stage judge (new rows)
                       'microfacet_render': 'render', 'render': 'render', 'volume_render': 'render', 'closed_volume_render': 'render'}
    imported = {}
    if args.reference:
        provenance = json.loads((args.reference/'provenance.json').read_text())
        assert provenance['passed'] and provenance['role'] == 'authoritative'
        state['reference'] = dict(root=str(args.reference), oracleSha256=provenance['oracleSha256'], libc=provenance['platform']['libc'],
                                  provenanceSha256=sha(args.reference/'provenance.json'))
        for job in jobs:
            folder = args.reference/'references'/job['label']
            if not folder.exists(): continue
            if args.device_scene and (REFERENCE_KINDS.get(job['gate']) in ('samples', 'render', 'svm') or job['gate'] in IMAGE_GATES): continue
            files = provenance['jobs'][job['label']]['referenceSha256']
            assert files and {p.name: sha(p) for p in folder.glob('*.f32')} == files, 'reference set changed: '+job['label']
            remote_ref = REMOTE+'/reference/'+job['label']
            shell(['mkdir', '-p', remote_ref], 'reference-dir-'+job['label'])
            for name in files: call(['push', str(folder/name), remote_ref+'/'+name], 'reference-push-'+job['label']+'-'+name, 300)
            pushed = shell(['sha256sum', *[remote_ref+'/'+n for n in files]], 'reference-hashes-'+job['label'])[1]
            assert {l.split()[1].rsplit('/', 1)[1]: l.split()[0] for l in pushed.splitlines()} == files, 'pushed reference differs'
            imported[job['label']] = dict(remote=remote_ref, local=folder, files=files,
                                          kind=REFERENCE_KINDS.get(job['gate'], 'lighting' if job['gate'] in IMAGE_GATES else None))
        state['referenceImported'] = sorted(imported)
    if args.device_scene:
        state['deviceScene'] = dict(flow='device-scene', linuxBin=str(args.linux_bin.resolve()))
    for job in jobs:
        label = job['label']; remaining = deadline-time.time()
        if remaining < 30:
            failures.append(label+': not run (corpus budget exhausted)'); results[label] = dict(passed=False, notRun='budget'); continue
        timeout = min(args.job_timeout, remaining)
        state['phase'] = label; save(OUT/'status.json', state); log('START '+label+' timeout=%ds' % timeout)
        program = STAGE_REMOTE+'/bin/codebrix_cycles_'+job['gate']+'_gate'
        jd = REMOTE+'/'+label
        env = ['env', '-u', 'OCIO', 'LD_LIBRARY_PATH='+STAGE_REMOTE+'/lib', 'CODEBRIX_CYCLES_PIPELINE_LOG='+jd+'/pipelines.jsonl']
        # Merge wave 3: --mesa-scratch-cache gives each job a fresh (cold) Mesa cache directory instead of disabling
        # the cache, as on Linux: cold at job start, reused within the job (gates that rebuild a scene per case).
        if RID == 'android-x64': env.append('MESA_SHADER_CACHE_DIR='+jd+'/mesa-cache' if args.mesa_scratch_cache else 'MESA_SHADER_CACHE_DISABLE=true')
        if args.pipeline_budget_ms: env.append('CODEBRIX_CYCLES_PIPELINE_BUDGET_MS='+str(args.pipeline_budget_ms))
        env += [k+'='+v for k, v in job['env'].items()]
        kind = REFERENCE_KINDS.get(job['gate'], 'lighting' if job['gate'] in IMAGE_GATES else None)
        if args.device_scene and kind in ('samples', 'lighting', 'render', 'svm') and not job.get('refuse'):  # merge wave 4: a refusal job has no scene to dump
            # Pass A (device): dump the device-prepared scene; no GPU work.
            dump_remote = jd+'/scene-dump'
            dump_env = [e for e in env if not e.startswith('CODEBRIX_CYCLES_PIPELINE_LOG=')]+['CODEBRIX_CYCLES_PIPELINE_LOG='+jd+'/dump-pipelines.jsonl', 'CODEBRIX_CYCLES_SCENE_DUMP='+dump_remote]
            dump_cmd = 'mkdir -p '+shlex.quote(jd)+'; echo $$ > '+shlex.quote(jd+'/active.pid')+'; exec '+shlex.join([*dump_env, program, jd+'/dump-run', *job['args']])
            log('SCENE-DUMP '+label)
            dcode, dout = call(['shell', dump_cmd], label+'-scene-dump', min(args.job_timeout, max(30, deadline-time.time())), False)
            assert dcode == 0 and json.loads(dout).get('sceneExchangeOnly') == 'dump', 'scene dump pass failed: '+label
            scenes_local = OUT/'scenes'/label; scenes_local.parent.mkdir(parents=True, exist_ok=True)
            call(['pull', dump_remote, str(scenes_local)], label+'-scene-pull', 600)
            # Pass B (Linux x64, glibc): reference from the device's own scene.
            ref_out = OUT/'device-scene-reference'
            command = [sys.executable, str(REPO/'native/tools/android_reference.py'), 'scene-reference', '--bin', str(args.linux_bin.resolve()),
                       '--scenes', str(scenes_local), '--output', str(ref_out), '--label', label, '--gate', job['gate'], *['--arg='+a for a in job['args']]]  # merge wave 1: '=' form, so '--stationary' is not read as an option
            if ref_out.exists(): command[command.index('--output')+1] = str(ref_out)
            with (OUT/(label+'.scene-reference.txt')).open('w') as f:
                r = subprocess.run(command, stdout=f, stderr=subprocess.STDOUT, timeout=900, env={**os.environ, **scene_env(job)})
            assert r.returncode == 0, 'Linux scene reference failed: '+label
            files = json.loads((ref_out/(label+'.provenance.json')).read_text())['referenceSha256']
            remote_ref = REMOTE+'/reference/'+label
            shell(['mkdir', '-p', remote_ref], 'reference-dir-'+label)
            for name in files: call(['push', str(ref_out/'references'/label/name), remote_ref+'/'+name], 'reference-push-'+label+'-'+name, 300)
            pushed = shell(['sha256sum', *[remote_ref+'/'+n for n in files]], 'reference-hashes-'+label)[1]
            assert {l.split()[1].rsplit('/', 1)[1]: l.split()[0] for l in pushed.splitlines()} == files, 'pushed reference differs'
            imported[label] = dict(remote=remote_ref, local=ref_out/'references'/label, files=files, kind=kind, verify=dump_remote)
            log('SCENE-REFERENCE '+label+' references=%d' % len(files))
            timeout = min(args.job_timeout, max(30, deadline-time.time()))
        if label in imported: env.append('CODEBRIX_CYCLES_REFERENCE_IMPORT='+imported[label]['remote'])
        if label in imported and kind == 'svm': env.append('CODEBRIX_CYCLES_REFERENCE_DIAGNOSTIC='+jd+'/run.reference-diagnostic.jsonl')
        if label in imported and imported[label].get('verify'): env.append('CODEBRIX_CYCLES_SCENE_VERIFY='+imported[label]['verify'])
        command = 'mkdir -p '+shlex.quote(jd)+'; echo $$ > '+shlex.quote(jd+'/active.pid')+'; exec '+shlex.join([*env, program, jd+'/run', *job['args']])
        started = time.time()
        try:
            code, stdout = call(['shell', command], label, timeout, False)
        except subprocess.TimeoutExpired:
            info = kill_exact(jd+'/active.pid', program, label)
            seconds = round(time.time()-started, 1)
            results[label] = dict(passed=False, timeout=True, wallSeconds=seconds, cleanup=info); save(OUT/'results.json', results)
            failures.append(label+': TIMEOUT after %.0fs (budget)' % seconds); log('TIMEOUT '+label+' %.0fs; killed=%s' % (seconds, info['killed']))
            call(['pull', jd, str(OUT/'output')], label+'-pull', 600, False)
            break
        seconds = round(time.time()-started, 1)
        (OUT/'output').mkdir(exist_ok=True)
        shell(['rm', '-rf', jd+'/mesa-cache'], label+'-rm-mesa-cache', check=False)
        call(['pull', jd, str(OUT/'output')], label+'-pull', 900, False)
        local = OUT/'output'/label
        plog = local/'pipelines.jsonl'
        pipelines = [json.loads(l) for l in plog.read_text().splitlines() if l.strip()] if plog.exists() else []
        entry = dict(gate=job['gate'], args=job['args'], env=job['env'], exit=code, wallSeconds=seconds, pipelines=pipelines,
                     maxPipelineMs=max([p.get('pipelineMs', 0) for p in pipelines], default=None))
        if job.get('refuse'):
            err = (OUT/(label+'.stderr.txt')).read_text()
            ok = code == 1 and job['refuse'] in err and not (local/'pipelines.jsonl').exists()
            entry.update(passed=ok, expectedRefusal=job['refuse'], stderr=err.strip().splitlines()[-2:])
            results[label] = entry; save(OUT/'results.json', results)
            if not ok: failures.append(label+': expected refusal not observed (exit %d)' % code)
            log(('PASS ' if ok else 'FAIL ')+label+' refusal exit=%d %.0fs' % (code, seconds)); continue
        if label in imported and kind == 'svm':
            entry['referencePolicy'] = dict(reference='linux-x64-glibc', flow='device-scene', hostRejudged=False,
                note='SVM probe records are compared on the device against the imported glibc records; the gate writes no GPU record file for a host recompute')
        elif label in imported:
            # Host-side re-judgement from the pulled files (authoritative vs glibc + diagnostics).
            try:
                refdir = imported[label]['local']
                if job['gate'] == 'environment':  # map/sample keys are judged on the device; re-judge the films here (lane m1-lighting)
                    films = OUT/'film-references'/label; films.mkdir(parents=True, exist_ok=True)
                    for f in refdir.glob('*.film.f32'):
                        if not (films/f.name).exists(): (films/f.name).symlink_to(f.resolve())
                    refdir = films
                ns = argparse.Namespace(kind=imported[label]['kind'], run=local/'run', reference=refdir,
                                        output=OUT/(label+'.reference-policy.json'))
                android_reference.compare(ns)
                policy = json.loads((OUT/(label+'.reference-policy.json')).read_text())
                scene_flow = bool(imported[label].get('verify'))
                oracle = json.loads((OUT/'device-scene-reference'/(label+'.provenance.json')).read_text())['oracleSha256'] if scene_flow else state['reference']['oracleSha256']
                entry['referencePolicy'] = dict(reference='linux-x64-glibc', flow='device-scene' if scene_flow else 'linux-prepared', oracleSha256=oracle,
                    hostRejudgedPassed=policy['passed'], rows=[dict(key=r['key'], authoritative=r['authoritative'],
                    diagnosticLocalVersusGlibc=r.get('diagnosticLocalVersusGlibc'), diagnosticGpuVersusLocal=r.get('diagnosticGpuVersusLocal')) for r in policy['rows']])
            except Exception as error:
                entry['referencePolicy'] = dict(error=repr(error))
            log('REFERENCE '+label+' host re-judged vs glibc: '+str(entry['referencePolicy'].get('hostRejudgedPassed', entry['referencePolicy'].get('error'))))
        # Lane S3: render_gate's lockstep stages in the device-scene flow are recorded on the device (run.stages.bin)
        # and judged here by the Linux x64 glibc gate on the device's dumped scene (android_reference.py stage-judge).
        # The job passes only when the device run AND the stage judge pass. The judge also runs after a failed device
        # run when the record is complete (diagnosis).
        stage_judge = None
        if label in imported and kind == 'render' and imported[label].get('verify') and (local/'run.stages.bin').exists():
            try:
                ns = argparse.Namespace(bin=args.linux_bin.resolve(), scenes=OUT/'scenes'/label, stages=local/'run.stages.bin', gate=job['gate'],
                                        arg=job['args'], output=OUT/(label+'.stage-judge.json'), budget=900)
                saved = dict(os.environ); os.environ.update(scene_env(job))
                try: android_reference.stage_judge(ns)
                finally: os.environ.clear(); os.environ.update(saved)
                stage_judge = json.loads((OUT/(label+'.stage-judge.json')).read_text())
            except Exception as error:
                stage_judge = dict(passed=False, error=repr(error))
            entry['stageJudge'] = stage_judge
            shell(['rm', '-f', jd+'/run.stages.bin'], label+'-rm-stages', check=False)
            log('STAGE-JUDGE '+label+' passed=%s %s' % (stage_judge.get('passed'), stage_judge.get('failure') or (stage_judge.get('report') or {}).get('maximumStagePracticalToleranceRatio')))
        if code != 0:
            call(['logcat', '-d', '-t', '300'], label+'-logcat', 120, False)
            err = (OUT/(label+'.stderr.txt')).read_text().strip().splitlines()
            entry.update(passed=False, error=err[-3:] if err else 'exit '+str(code)); results[label] = entry; save(OUT/'results.json', results)
            failures.append(label+': '+(err[-1] if err else 'exit '+str(code))); log('FAIL '+label+' exit=%d %.0fs: %s' % (code, seconds, err[-1] if err else '')); continue
        report = None
        try:
            report = json.loads(stdout)
            assert report['passed'] and report.get('cyclesAllocationBytesAfterCleanup', 0) == 0
            library = local/'run/library.txt' if job['gate'] in IMAGE_GATES else local/'run.library.txt'
            assert library.read_text().strip() == STAGE_REMOTE+'/lib/libcodebrix_wgpu.so', 'unexpected runtime'
            if job['check']: job['check'](report)
            if report.get('lockstepStageChecks') == 'exchanged':
                assert stage_judge and stage_judge.get('passed'), 'Linux stage judge failed: '+str((stage_judge or {}).get('failure') or (stage_judge or {}).get('error'))
                assert stage_judge['report']['stages'] == report['stages'] == report['stageExchangeRecords'], 'stage judge coverage differs'
            entry.update(passed=True, report=report, pipelineReportPresent='pipelineReport' in report)
            log('PASS '+label+' %.0fs maxPipelineMs=%s' % (seconds, entry['maxPipelineMs']))
        except Exception as error:
            entry.update(passed=False, report=report, assertion=repr(error))
            failures.append(label+': assertion '+repr(error)); log('FAIL '+label+' assertion '+repr(error))
        results[label] = entry; save(OUT/'results.json', results)
    # replays on device, from the device's own captures
    replay_bin = STAGE_REMOTE+'/bin/render_replay'
    for name, job_label, rel in replays:
        if not results.get(job_label, {}).get('passed'):
            results[name] = dict(passed=False, notRun='source job failed'); failures.append(name+': not run (source job failed)'); continue
        remaining = deadline-time.time()
        if remaining < 30:
            results[name] = dict(passed=False, notRun='budget'); failures.append(name+': not run (corpus budget exhausted)'); continue
        timeout = min(args.job_timeout, remaining)
        if rel == '@': cap_remote, cap_local = REMOTE+'/'+job_label, OUT/'output'/job_label
        elif rel.startswith('@'): cap_remote, cap_local = REMOTE+'/'+job_label+'/'+rel[1:], OUT/'output'/job_label/rel[1:]
        else: cap_remote, cap_local = REMOTE+'/'+job_label+'/run/'+rel, OUT/'output'/job_label/'run'/rel
        rd = REMOTE+'/replays/'+name
        state['phase'] = name; save(OUT/'status.json', state); log('START '+name+' timeout=%ds' % timeout)
        env = ['env', 'LD_LIBRARY_PATH='+STAGE_REMOTE+'/lib', 'CODEBRIX_CYCLES_PIPELINE_LOG='+rd+'/pipelines.jsonl']
        if RID == 'android-x64': env.append('MESA_SHADER_CACHE_DISABLE=true')
        if args.pipeline_budget_ms: env.append('CODEBRIX_CYCLES_PIPELINE_BUDGET_MS='+str(args.pipeline_budget_ms))
        command = 'mkdir -p '+shlex.quote(rd)+'; echo $$ > '+shlex.quote(rd+'/active.pid')+'; exec '+shlex.join([*env, replay_bin, cap_remote, rd+'/result'])
        out = OUT/'replays'/name; out.mkdir(parents=True)
        started = time.time()
        try:
            code, stdout = call(['shell', command], 'replay-'+name, timeout, False)
        except subprocess.TimeoutExpired:
            info = kill_exact(rd+'/active.pid', replay_bin, name)
            results[name] = dict(passed=False, timeout=True, cleanup=info); failures.append(name+': TIMEOUT'); log('TIMEOUT '+name); break
        seconds = round(time.time()-started, 1)
        (out/'run.stdout.txt').write_text(stdout); shutil.copy2(OUT/('replay-'+name+'.stderr.txt'), out/'run.stderr.txt')
        if code != 0:
            err = (out/'run.stderr.txt').read_text().strip().splitlines()
            results[name] = dict(passed=False, exit=code, error=err[-3:]); failures.append(name+': exit %d' % code); log('FAIL '+name+' exit %d' % code); continue
        for suffix in ['arena.bin', 'adapter.txt', 'library.txt']:
            call(['pull', rd+'/result.'+suffix, str(out/('run.'+suffix))], 'replay-'+name+'-pull-'+suffix.split('.')[0], 600)
        call(['shell', 'rm -f '+shlex.quote(rd+'/result.arena.bin')], 'replay-'+name+'-rm-arena', 60, False)
        try:
            assert (out/'run.library.txt').read_text().strip() == STAGE_REMOTE+'/lib/libcodebrix_wgpu.so'
            # Lane E4 (adopted in merge wave 3): under the X1 reference policy the replayed film is judged against the same
            # correctly rounded glibc reference as its source job (device-scene flow); the capture's own CPU film (the
            # device's C library) is validated as a diagnostic only. Captures without an imported reference are unchanged.
            judged = cap_local; glibc = OUT/'device-scene-reference'/'references'/job_label/(rel+'.film.f32')
            if rel == '@': glibc = OUT/'device-scene-reference'/'references'/job_label/'image.f32'  # lane S3: render_gate captures
            if args.device_scene and (rel.startswith('case-') or rel == '@') and glibc.exists():
                judged = OUT/'replay-judged'/name; judged.mkdir(parents=True)
                for f in cap_local.iterdir():
                    if f.name != 'run.cpu.film.bin': (judged/f.name).symlink_to(f.resolve())
                shutil.copy2(glibc, judged/'run.cpu.film.bin')
                try: diag = render_replay.validate(cap_local, out); diagnostic = dict(passed=True, maximumAbsoluteImageError=diag['maximumAbsoluteImageError'], strictImageFailures=diag['strictImageFailures'])
                except Exception as e: diagnostic = dict(passed=False, assertion=repr(e))
            report = render_replay.validate(judged, out)
            if judged != cap_local: report.update(judgedAgainst='glibc device-scene reference '+str(glibc), localCpuFilmDiagnostic=diagnostic)
            report.update(rid=RID, serial=serial, wallSeconds=seconds, captureFormat=json.loads((cap_local/'run.capture.json').read_text())['format'],
                          webGpuSha256=RUNTIME_SHA, driverSha256=pins.get(str(render_replay.BASE/RID/'render_replay')))
            save(out/'validation.json', report); (out/'run.arena.bin').unlink()
            results[name] = dict(passed=True, report=report); log('PASS %s %.0fs readbacks=%d max=%.3g strict=%d fmt=%s' % (name, seconds,
                report['results']['exactSchedulingReadbacks'], report['maximumAbsoluteImageError'], report['strictImageFailures'], report['captureFormat']))
        except Exception as error:
            results[name] = dict(passed=False, assertion=repr(error)); failures.append(name+': '+repr(error)); log('FAIL '+name+' '+repr(error))
        save(OUT/'results.json', results)
    state.update(phase='complete', passed=not failures, failures=failures)
except BaseException as error:
    state.update(passed=False, error=repr(error)); log('ERROR '+repr(error)); raise
finally:
    state['complete'] = True; state['wallSeconds'] = round(time.time()-T0, 1); state['withinBudget'] = state['wallSeconds'] <= args.budget+60
    after = subprocess.run([ADB, '-s', serial, 'shell', 'ps -A -o PID,ARGS'], capture_output=True, text=True, timeout=60).stdout
    state['remoteProcessesAfter'] = [l for l in after.splitlines() if 'codebrix-m8-' in l or 'codebrix_cycles' in l or 'render_replay' in l]
    state['productionSourcesUnchanged'] = sources == inputs()
    state['binaryInputsUnchanged'] = all(sha(Path(p)) == d for p, d in pins.items())
    state['inputsUnchanged'] = state['productionSourcesUnchanged'] and state['binaryInputsUnchanged']
    if not state['inputsUnchanged']: state['passed'] = False
    save(OUT/'status.json', state); save(OUT/'results.json', results)
    log('END passed=%s wall=%.0fs/%ds failures=%d' % (state.get('passed'), state['wallSeconds'], args.budget, len(failures)))
print(json.dumps({k: state.get(k) for k in ['passed', 'failures', 'error', 'wallSeconds']}), flush=True)
