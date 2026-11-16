#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Survey helper (scratch): the cross-run comparisons of the Apple Silicon feature survey, phase 1.

Every comparison is native/oracle/compare.py (CPU directory first), its JSON kept under compare/ and its stdout appended to
compare/compare.output.txt; summary.json lists per pair the reference, candidate, practical failures, strict differences,
maximum absolute difference and NaN/Inf counts, and per backend directory the film SHA-256 list from meta.json.
Comparisons are reported, never judged. Cross-document pairs (row 9) are film-hash and numeric differences only (compare.py
refuses two scene documents); they are listed under crossDocument.
Usage: python3 cross_compare.py (from anywhere)."""
import array,hashlib,json,math,subprocess,sys
from pathlib import Path
HERE=Path(__file__).resolve().parent;REPO=HERE.parents[2]
RUNS=REPO/'native/work/oracles';PREFIX='macos-arm64-survey-20261113-'
BASE_A=REPO/'native/evidence/cycles-oracles-macos-20261113/oracle'
OUT=HERE/'compare';OUT.mkdir(exist_ok=True)
CPU,MB,MR='cpu-arm64-macos','metal-m2pro','metal-m2pro-metalrt'
def d(run,scene,backend):return RUNS/(PREFIX+run)/'oracle'/scene/backend
def meta(directory):return json.loads((Path(directory)/'meta.json').read_text())
summary=dict(compare=[],crossDocument=[],hashEquality=[])
log=(OUT/'compare.output.txt').open('w')


def compare(name,*directories):
    target=OUT/(name+'.json')
    result=subprocess.run([sys.executable,str(REPO/'native/oracle/compare.py'),*map(str,directories),'--json',str(target)],capture_output=True,text=True)
    log.write(f'### {name}\n$ compare.py {" ".join(str(Path(x).relative_to(REPO)) for x in directories)}\n{result.stdout}{result.stderr}exit {result.returncode}\n\n')
    if not target.exists():summary['compare'].append(dict(name=name,error=(result.stdout+result.stderr).strip()[-400:]));return
    report=json.loads(target.read_text())
    pairs=[dict(reference=p['reference'],candidate=p['candidate'],role=p['role'],bitwiseEqual=p['bitwiseEqual'],practicalFailures=p['practicalFailures'],
                strictDifferences=p['strictDifferences'],maxAbsoluteDifference=p['maxAbsoluteDifference'],
                passes={q['name']:(q.get('practicalFailures'),q.get('maxAbsoluteDifference')) for q in p.get('passes',[])} or None) for p in report['pairs']]
    backends=[dict(directory=str(Path(b['directory']).relative_to(REPO)),backend=b['backend'],repeatBitwiseIdentical=b['measuredRepeatBitwiseIdentical'],
                   repeatMaxAbsDifference=b['measuredRepeatMaxAbsDifference'],nonfinite=b['nonfiniteComponents'],
                   filmSha256=meta(b['directory'])['filmSha256']) for b in report['backends']]
    summary['compare'].append(dict(name=name,passed=report['passed'],problems=report['problems'],backends=backends,pairs=pairs))


def film(directory,repeat=0):
    values=array.array('f');values.frombytes((Path(directory)/f'film-{repeat}.f32').read_bytes());return values


def cross(name,a,b):
    """Two different scene documents: film hashes and numeric differences of film-0, reported only."""
    ma,mb=meta(a),meta(b);row=dict(name=name,a=str(Path(a).relative_to(REPO)),b=str(Path(b).relative_to(REPO)),
        aShape=[ma['width'],ma['height']],bShape=[mb['width'],mb['height']],aFilmSha256=ma['filmSha256'],bFilmSha256=mb['filmSha256'],
        film0BitwiseEqual=ma['filmSha256'][0]==mb['filmSha256'][0])
    if row['aShape']==row['bShape']:
        fa,fb=film(a),film(b);diffs=[abs(x-y) for x,y in zip(fa,fb) if math.isfinite(x) and math.isfinite(y)]
        row.update(differingComponents=sum(1 for x in diffs if x>0),maxAbsoluteDifference=max(diffs,default=0.0),
                   practicalFailuresBAsReference=sum(1 for x,y in zip(fa,fb) if abs(x-y)>5e-5+2e-4*abs(y)),components=len(fa))
    summary['crossDocument'].append(row)


def same_hash(name,a,b):
    ma,mb=meta(a),meta(b)
    summary['hashEquality'].append(dict(name=name,a=str(Path(a).relative_to(REPO)),b=str(Path(b).relative_to(REPO)),
        sceneSha256Equal=ma['sceneSha256']==mb['sceneSha256'],filmSha256Equal=ma['filmSha256']==mb['filmSha256'],
        passSha256Equal=[p.get('filmSha256') for p in ma.get('passes',[])]==[p.get('filmSha256') for p in mb.get('passes',[])],
        aFilmSha256=ma['filmSha256'],bFilmSha256=mb['filmSha256']))


# Row 6: two processes (R1, R1b); CPU repeat identity and CPU run against run; Metal run against run per backend.
for scene in ['survey/curves-thick-diffuse-both','survey/points-diffuse-both']:
    tag=scene.split('/')[1]
    compare(f'row06-{tag}--cpu--R1-vs-R1b',d('R1-base',scene,CPU),d('R1b-base-row6',scene,CPU))
    for backend in (MB,MR):
        compare(f'row06-{tag}--{backend}--R1-vs-R1b',d('R1-base',scene,CPU),d('R1-base',scene,backend),d('R1b-base-row6',scene,backend))
# Row 9: CPU (survey build, R2) against Metal (base build, R1) per document; factor documents against the factor-1 control.
docs=['1-gpu','2-gpu','4-gpu','1.5-gpu','2-cpu-oidn','1-gpu-catcher','2-gpu-catcher']
for doc in docs:
    scene='survey/denoise-upscale-'+doc
    compare(f'row09-{doc}--cpu-R2-vs-metal-R1',d('R2-survey',scene,CPU),d('R1-base',scene,MB),d('R1-base',scene,MR))
    for backend in (MB,MR):same_hash(f'row09-{doc}--{backend}--R1-base-vs-R2-survey',d('R1-base',scene,backend),d('R2-survey',scene,backend))
for backend in (CPU,MB,MR):
    run_name='R2-survey' if backend==CPU else 'R1-base'
    for doc in ['2-gpu','4-gpu','1.5-gpu','2-cpu-oidn']:
        cross(f'row09-{doc}-vs-1-gpu--{backend}',d(run_name,'survey/denoise-upscale-'+doc,backend),d(run_name,'survey/denoise-upscale-1-gpu',backend))
    cross(f'row09-2-gpu-catcher-vs-1-gpu-catcher--{backend}',d(run_name,'survey/denoise-upscale-2-gpu-catcher',backend),d(run_name,'survey/denoise-upscale-1-gpu-catcher',backend))
    # denoise_use_gpu false with factor 2 against the committed denoise/beckmann-on (factor 1, CPU OIDN) of Part A.
    cross(f'row09-2-cpu-oidn-vs-partA-beckmann-on--{backend}',d(run_name,'survey/denoise-upscale-2-cpu-oidn',backend),BASE_A/'denoise/beckmann-on'/backend)
    cross(f'row09-1-gpu-vs-partA-beckmann-on--{backend}',d(run_name,'survey/denoise-upscale-1-gpu',backend),BASE_A/'denoise/beckmann-on'/backend)
# Row 11: per scene, steps 0 (R2) against steps 1 (R3) and 3 (R4).
ROW11=['curves-b/motion-deform-thick','curves-b/motion-deform-ribbon','curves-b/motion-deform-linear','curves-b/motion-radius-thick',
       'curves-b/shadow-motion','points/deform-3','points/deform-5','points/deform-transparent','linux-gates/deforming-motion-0',
       'light-motion/camera-deforming-motion','points/object-motion']
for scene in ROW11:
    tag=scene.replace('/','__')
    for steps,run_name in (('1','R3-steps1'),('3','R4-steps3')):
        compare(f'row11-{tag}--cpu--steps0-vs-steps{steps}',d('R2-survey',scene,CPU),d(run_name,scene,CPU))
        for backend in (MB,MR):
            compare(f'row11-{tag}--{backend}--steps0-vs-steps{steps}',d('R2-survey',scene,CPU),d('R2-survey',scene,backend),d(run_name,scene,backend))
# Survey-build equivalence: R2 (variable unset) CPU films against Part A's base-run CPU films (rule-9 style hash check).
for scene in ['starter/diffuse-triangle','denoise/beckmann-on','bake/combined']+ROW11:
    same_hash(f'equivalence-{scene.replace("/","__")}--cpu--partA-vs-R2',BASE_A/scene/CPU,d('R2-survey',scene,CPU))
    for backend in (MB,MR):compare(f'equivalence-{scene.replace("/","__")}--{backend}--partA-vs-R2',BASE_A/scene/CPU,BASE_A/scene/backend,d('R2-survey',scene,backend))
# Row 10: bsdf/velvet and the starter against Part A (same documents), CPU hash and Metal numbers.
for scene in ['bsdf/velvet','starter/diffuse-triangle']:
    same_hash(f'row10-{scene.replace("/","__")}--cpu--partA-vs-R10',BASE_A/scene/CPU,d('R10-metal-comparison',scene,CPU))
    for backend in (MB,MR):compare(f'row10-{scene.replace("/","__")}--{backend}--partA-vs-R10',BASE_A/scene/CPU,BASE_A/scene/backend,d('R10-metal-comparison',scene,backend))
# Row 10, phase 2: the Linux ARM64 CPU film of bsdf/velvet (Part B, the same document at commit 0b19d875) against the Mac CPU
# and both Metal films of R10; information only (all four are gold standards, every pair is reported).
PART_B=REPO/'native/evidence/linux-arm64-cpu-oracle-20261113/oracle'
compare('row10-bsdf__velvet--partB-cpu-arm64-linux-vs-R10',PART_B/'bsdf/velvet/cpu-arm64-linux',d('R10-metal-comparison','bsdf/velvet',CPU),
        d('R10-metal-comparison','bsdf/velvet',MB),d('R10-metal-comparison','bsdf/velvet',MR))
# Phase 2: the 34 committed scenes rendered again in a second process (run phase2) against the draft-check renders of the
# same documents (P2, P2c): CPU film hashes (cross-process repeat identity, which kit rule 9 relies on).
NEW=[l.strip()[:-5] for l in (HERE/'phase2-draft/new-scenes.txt').read_text().splitlines() if l.strip() and not l.startswith('#')]
for scene in NEW:
    for draft in ('P2c-base-draft','P2b-base-draft','P2-base-draft'):
        a=d(draft,scene,CPU)
        if (a/'meta.json').exists() and meta(a)['sceneSha256']==meta(d('phase2',scene,CPU))['sceneSha256']:
            same_hash(f'phase2-{scene.replace("/","__")}--cpu--{draft}-vs-phase2',a,d('phase2',scene,CPU));break
# The three zero-area bake types added after the draft check were rendered then as probe documents (another id, the same
# render content): film hashes across the two documents.
for kind in ('glossy-indirect','transmission-indirect','transmission-color'):
    cross(f'phase2-bake__zero-area-{kind}--cpu--P2b-probe-vs-phase2',d('P2b-base-draft','survey/bake-zero-area-'+kind,CPU),d('phase2','bake/zero-area-'+kind,CPU))
log.close()
(OUT/'summary.json').write_text(json.dumps(summary,indent=1)+'\n')
print('compare entries',len(summary['compare']),'cross-document',len(summary['crossDocument']),'hash checks',len(summary['hashEquality']))
print('compare errors',[c['name'] for c in summary['compare'] if 'error' in c])
