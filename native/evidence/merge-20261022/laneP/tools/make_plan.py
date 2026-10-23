#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Lane P: the cutoff table (3 x Samsung wall per job, sources recorded) and the Pixel step list -> plan.json."""
import json
from pathlib import Path
P = Path(__file__).resolve().parents[1]
W = 'main:native/work/'
M3, M2, M1 = W+'merge3/runs/samsung/', W+'merge2/runs/samsung/', W+'merge1/runs/samsung/'
FLOOR = 5  # seconds; adb launch latency floor for jobs whose Samsung wall is under 5/3 s (documented in the report)
# (corpus, job, samsung wall, source, samsung passed)
S = [
 ('triangle','triangle_sample',25.8,M3+'triangle-mw3',False),('triangle','surface',97.4,M3+'triangle-mw3',True),
 ('triangle','triangle_lighting',159.0,M3+'triangle-mw3',True),
 *[('triangle','replay-%d'%i,w,M3+'triangle-mw3',True) for i,w in enumerate([21.4,39.1,39.2,39.4,39.0,87.0])],
 ('emitter','emitter_sample',11.3,M3+'emitter-mw3',True),
 *[('hostcap-linux-triangle','tri%d'%i,w,M3+'hostcap-linux-triangle',True) for i,w in enumerate([21.7,40.0,40.0,40.0,39.1,87.9])],
 *[('hostcap-linux-terminator','term%d'%i,w,M3+'hostcap-linux-terminator',True) for i,w in enumerate([17.2,19.6,19.5,19.4,33.7,33.7,33.5,33.7,33.7,72.8])],
 ('materials','nodes_svm',879.2,M3+'materials-mw3',False),('materials','nodes_world',509.8,M3+'materials-mw3',True),
 ('materials','nodes_render',153.2,M3+'materials-mw3',False),('materials','nodes-replay',None,'none (Samsung source job fails, replay never ran)',False),
 ('materials','principled_svm',71.2,M2+'materials-mw2a',False),('materials','principled_bounce',26.3,M2+'materials-mw2a',False),
 ('materials','principled_render',28.7,M2+'materials-mw2r',False),('materials','principled-replay',None,'none (Samsung source job fails, replay never ran)',False),
 ('lighting','broad_lighting',68.3,M1+'lighting-mw1',True),('lighting','broad_lighting-replay-0',20.3,M1+'lighting-mw1',True),('lighting','broad_lighting-replay-1',20.4,M1+'lighting-mw1',True),
 ('lighting','feature_combination',42.8,M1+'lighting-mw1',True),('lighting','feature_combination-replay-0',27.6,M1+'lighting-mw1',True),('lighting','feature_combination-replay-1',27.9,M1+'lighting-mw1',True),
 ('lighting','lighting_mis',33.4,M1+'regression-mw1',True),('lighting','lighting_mis-replay-0',None,'pending lane P Samsung timing',None),('lighting','lighting_mis-replay-1',None,'pending lane P Samsung timing',None),
 ('regression','glass_render',79.9,M1+'regression-mw1',True),('regression','lighting_mis',33.4,M1+'regression-mw1',True),('regression','transparent_lighting',107.5,M1+'regression-mw1',True),
 ('transparent','transparent_shadow',16.1,M1+'transparent-mw1',True),('transparent','transparent_lighting',106.9,M1+'transparent-mw1',True),
 *[('transparent','transparent_lighting-replay-%d'%i,w,M1+'transparent-mw1',True) for i,w in enumerate([23.7,23.7,30.5,30.9,31.4,31.9,30.2,30.8])],
 ('transparent','transparent-shadow-replay-direct',3.9,M1+'transparent-mw1',True),('transparent','transparent-shadow-replay-indexed',4.0,M1+'transparent-mw1',True),
 ('terminator','shadow_terminator',147.4,M1+'terminator-mw1',True),
 *[('terminator','terminator-replay-%d'%i,w,M1+'terminator-mw1',True) for i,w in enumerate([15.3,17.5,17.6,17.6,29.8,29.8,29.8,29.6,29.9,67.7])],
 ('spec-refusal','glass_render-spec',76.8,M2+'spec-refusal-mw2',True),('spec-refusal','glass_render-nospec-refused',0.3,M2+'spec-refusal-mw2',True),
 ('motion','object-moving-images',84.9,M1+'motion-mw1',True),('motion','object-replay-motion-0',66.8,M1+'motion-mw1',True),
 ('motion','object-stationary-images',None,'pending lane P Samsung timing',None),('motion','deforming-moving-images',110.8,M1+'motion-mw1',True),
 ('motion','deforming-replay-motion-0',29.6,M1+'motion-mw1',True),('motion','deforming-stationary-images',None,'pending lane P Samsung timing',None),
 ('motion','camera-static',7.9,M1+'motion-mw1',True),('motion','camera-motion',14.2,M1+'motion-mw1',True),
 ('precision','closest',21.4,M1+'precision-mw1',True),('precision','shadow',16.2,M1+'precision-mw1',True),
 ('precision','shadow-replay-direct',1.5,M1+'precision-mw1',True),('precision','shadow-replay-indexed',1.5,M1+'precision-mw1',True),
 ('volume','volume-state',13.2,M1+'volume-mw1',True),('volume','volume-absorption',120.8,M1+'volume-mw1',True),('volume','volume-isotropic',172.0,M1+'volume-mw1',True),
 ('volume','volume-forward',170.8,M1+'volume-mw1',True),('volume','volume-backward',171.7,M1+'volume-mw1',True),('volume','boundary-state',32.4,M1+'volume-mw1',True),
 ('volume','boundary-compaction-guard',6.4,M1+'volume-mw1',True),('volume','closed-single',214.4,M1+'volume-mw1',True),('volume','closed-overlap',319.9,M1+'volume-mw1',True),
 ('volume','closed-inside',302.9,M1+'volume-mw1',True),
 *[('volume','volume-replay-'+n,w,M1+'volume-mw1',True) for n,w in [('absorption',19.5),('isotropic',20.6),('forward',20.5),('backward',20.7)]],
 *[('volume','closed-replay-'+n,w,M1+'volume-mw1',True) for n,w in [('single',21.5),('overlap',33.9),('inside',34.1)]],
 ('app','managed-app-3-scenes',134.4,W+'merge3/device/app-samsung.log (appSeconds)',True),
]
timing = P/'runs/samsung'
def timed(job):
    for d in sorted(timing.glob('*-timing')) if timing.exists() else []:
        try: r = json.loads((d/'results.json').read_text())
        except Exception: continue
        e = r.get(job)
        if e and (e.get('wallSeconds') or (e.get('report') or {}).get('wallSeconds')):
            return e.get('wallSeconds') or e['report']['wallSeconds'], 'lane P: native/work/p/runs/samsung/'+d.name, bool(e.get('passed'))
    return None
rows = []
for corpus, job, wall, src, ok in S:
    if wall is None and (t := timed(job)): wall, src, ok = t
    cutoff = None if wall is None else max(FLOOR, round(3*wall))
    rows.append(dict(corpus=corpus, job=job, samsungWall=wall, samsungSource=src, samsungPassed=ok, cutoff=cutoff))
(P/'plan.json').write_text(json.dumps(dict(rule='cutoff = max(5 s, round(3 x Samsung wall)); per job; Samsung source precedence merge3 > merge2 > merge1 > lane P timing', rows=rows), indent=1)+'\n')
print(len(rows), 'rows;', sum(1 for r in rows if r['cutoff'] is None), 'without baseline')
