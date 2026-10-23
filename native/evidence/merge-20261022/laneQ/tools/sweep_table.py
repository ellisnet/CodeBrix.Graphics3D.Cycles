#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Lane Q (S20 sweep): per-job sweep table from runs/s20/*/{results,status}.json plus device/extra-results.json
(host gate, app). Writes checkpoint/sweep-table.{md,json}. Tablet walls are the SM-X700's latest per-job walls
(merge waves 1-3, native/work/merge{1,2,3}/runs/samsung in main; host gate/app from phaseD5/merge3)."""
import json, re
from pathlib import Path
Q = Path(__file__).resolve().parents[1]
TABLET = {  # job -> (tablet wall s, tablet result, source)
 'emitter_sample': (11.3, 'PASS', 'mw3'), 'triangle_sample': (25.8, 'FAIL 2 practical (sample 3133)', 'mw3'), 'surface': (97.4, 'PASS', 'mw3'),
 'triangle_lighting': (159.0, 'PASS', 'mw3'), **{'replay-%d' % i: (w, 'PASS', 'mw3') for i, w in enumerate([21.4, 39.1, 39.2, 39.4, 39.0, 87.0])},
 'broad_lighting': (68.3, 'PASS', 'mw1'), 'feature_combination': (42.8, 'PASS', 'mw1'), 'lighting_mis': (33.4, 'PASS', 'mw1'),
 'broad_lighting-replay-0': (20.3, 'PASS', 'mw1'), 'broad_lighting-replay-1': (20.4, 'PASS', 'mw1'),
 'feature_combination-replay-0': (27.6, 'PASS', 'mw1'), 'feature_combination-replay-1': (27.9, 'PASS', 'mw1'),
 'lighting_mis-replay-0': (None, 'not run on tablet', '-'), 'lighting_mis-replay-1': (None, 'not run on tablet', '-'),
 'glass_render': (79.9, 'PASS', 'mw1'), 'transparent_lighting': (106.9, 'PASS', 'mw1'), 'transparent_shadow': (16.1, 'PASS', 'mw1'),
 **{'transparent_lighting-replay-%d' % i: (w, 'PASS', 'mw1') for i, w in enumerate([23.7, 23.7, 30.5, 30.9, 31.4, 31.9, 30.2, 30.8])},
 'transparent-shadow-replay-direct': (3.9, 'PASS', 'mw1'), 'transparent-shadow-replay-indexed': (4.0, 'PASS', 'mw1'),
 'shadow_terminator': (147.4, 'PASS', 'mw1'), **{'terminator-replay-%d' % i: (w, 'PASS', 'mw1') for i, w in enumerate([15.3, 17.5, 17.6, 17.6, 29.8, 29.8, 29.8, 29.6, 29.9, 67.7])},
 'glass_render-spec': (76.8, 'PASS', 'mw2'), 'glass_render-nospec-refused': (0.3, 'PASS (refused)', 'mw2'),
 'object-moving-images': (84.9, 'PASS', 'mw1'), 'deforming-moving-images': (110.8, 'PASS', 'mw1'), 'camera-static': (7.9, 'PASS', 'mw1'), 'camera-motion': (14.2, 'PASS', 'mw1'),
 'object-replay-motion-0': (66.8, 'PASS', 'mw1'), 'deforming-replay-motion-0': (29.6, 'PASS', 'mw1'),
 'closest': (21.4, 'PASS', 'mw1'), 'shadow': (16.2, 'PASS', 'mw1'), 'shadow-replay-direct': (1.5, 'PASS', 'mw1'), 'shadow-replay-indexed': (1.5, 'PASS', 'mw1'),
 'volume-state': (13.2, 'PASS', 'mw1'), 'volume-absorption': (120.8, 'PASS', 'mw1'), 'volume-isotropic': (172.0, 'PASS', 'mw1'), 'volume-forward': (170.8, 'PASS', 'mw1'),
 'volume-backward': (171.7, 'PASS', 'mw1'), 'boundary-state': (32.4, 'PASS', 'mw1'), 'boundary-compaction-guard': (6.4, 'PASS', 'mw1'),
 'closed-single': (214.4, 'PASS', 'mw1'), 'closed-overlap': (319.9, 'PASS', 'mw1'), 'closed-inside': (302.9, 'PASS', 'mw1'),
 'volume-replay-absorption': (19.5, 'PASS', 'mw1'), 'volume-replay-isotropic': (20.6, 'PASS', 'mw1'), 'volume-replay-forward': (20.5, 'PASS', 'mw1'), 'volume-replay-backward': (20.7, 'PASS', 'mw1'),
 'closed-replay-single': (21.5, 'PASS', 'mw1'), 'closed-replay-overlap': (33.9, 'PASS', 'mw1'), 'closed-replay-inside': (34.1, 'PASS', 'mw1'),
 **{'tri%d' % i: (w, 'PASS', 'mw3') for i, w in enumerate([21.7, 40.0, 40.0, 40.0, 39.1, 87.9])},
 **{'term%d' % i: (w, 'PASS', 'mw3') for i, w in enumerate([17.2, 19.6, 19.5, 19.4, 33.7, 33.7, 33.5, 33.7, 33.7, 72.8])},
 'principled_svm': (71.2, 'FAIL device lost (item 8)', 'mw2'), 'principled_bounce': (26.3, 'FAIL device lost (item 8)', 'mw2'),
 'principled_render': (28.7, 'FAIL device lost (item 8)', 'mw2'), 'principled-replay': (None, 'not run (source failed)', 'mw2'),
 'nodes_svm': (879.2, 'FAIL typed 2D noise (item 9)', 'mw3'), 'nodes_world': (509.8, 'PASS', 'mw3'), 'nodes_render': (153.2, 'FAIL pixel 6720 (item 9)', 'mw3'),
 'nodes-replay': (None, 'not run (source failed)', 'mw3'),
 'canary-glass_render': (79.9, 'PASS (glass_render, no canary run)', 'mw1'),
 'android-host-gate': (62.0, 'PASS 48 exports (wall from file times)', 'phaseD5'), 'app-webgpu': (173.0, 'PASS 0 practical', 'mw3'),
}
def classify(e):
    if e.get('passed'): return ''
    if e.get('notRun'): return 'not run: '+e['notRun']
    if e.get('timeout'): return 'timeout'
    t = json.dumps(e.get('error') or e.get('assertion') or '') + json.dumps(e.get('stderrTail') or '')
    if re.search(r'device is lost|Device lost|DeviceLost|device lost|Failed to link', t, re.I): return 'device lost'
    if e.get('exit') in (134, 139, 135, 136, -6, -11) or re.search(r'signal|Segmentation|Abort', t): return 'crash'
    if re.search(r'invalid data|invalid state', t): return 'numerical practical (GPU kernel reported invalid data)'
    if re.search(r'mismatch|differs|got=|expected|practical|Mismatch|error 8|SVM error', t): return 'numerical practical'
    return 'other: '+t[:80]
def first_error(run, job, e):
    f = run/(job+'.stderr.txt')
    if e.get('passed') or e.get('notRun') or not f.exists(): return None
    for l in f.read_text(errors='replace').splitlines():
        l = l.strip()
        if not l or l.startswith(('CodeBrix Cycles WebGPU warning', '[OpenColorIO')): continue
        if re.search(r'ERROR|error|failed|panicked|lost|mismatch|differs|refus', l): return l[:200]
    return None
rows = []; adapters = set(); tiers = set()
for run in sorted((Q/'runs'/'s20').glob('*')):
    rf = run/'results.json'
    if not rf.exists(): continue
    res = json.loads(rf.read_text()); st = json.loads((run/'status.json').read_text()) if (run/'status.json').exists() else {}
    for job, e in res.items():
        p = e.get('pipelines') or []
        for x in p:
            if x.get('adapter'): adapters.add(x['adapter'].strip())
            if x.get('tier'): tiers.add((x.get('tier'), x.get('tierReason')))
        warn = sorted({(x.get('entry'), round(x.get('pipelineMs', 0)/1000, 1)) for x in p if x.get('pipelineMs', 0) > (st.get('pipelineBudgetMs') or 20000)}, key=lambda t: -t[1])
        pol = e.get('referencePolicy') or {}; reached = [x for x in pol.get('rows', []) if not x['authoritative'].get('notReached')]
        g = loc = rep = None
        if reached:
            a = [x['authoritative'] for x in reached]; d = [x.get('diagnosticLocalVersusGlibc') or {} for x in reached]
            g = dict(cases='%d/%d' % (len(reached), len(pol['rows'])), practical=sum(x['practicalFailures'] for x in a), strict=sum(x['strictDifferences'] for x in a), max=max(x['maximumAbsoluteError'] for x in a))
            loc = dict(bionicVsGlibcDiffering=sum(x.get('bitwiseDiffering', 0) for x in d), bionicVsGlibcPractical=sum(x.get('practicalFailures', 0) for x in d))
        r = e.get('report') if isinstance(e.get('report'), dict) else {}
        if isinstance(r.get('results'), dict) and 'exactSchedulingReadbacks' in r['results']:
            rep = dict(readbacks=r['results']['exactSchedulingReadbacks'], strict=r.get('strictImageFailures'), max=r.get('maximumAbsoluteImageError'))
        localnum = {k: v for k, v in r.items() if isinstance(v, (int, float)) and not isinstance(v, bool) and any(t in k for t in ('strict', 'maximum', 'Mismatch'))} if r and not rep else {}
        wall = e.get('wallSeconds') or (r.get('wallSeconds') if r else None)
        tw = TABLET.get(job, (None, '?', '?'))
        rows.append(dict(corpus=run.name, job=job, s20Wall=wall, tabletWall=tw[0], tabletResult=tw[1], passed=bool(e.get('passed')),
                         classification=classify(e), gpuVsGlibc=g, localDiag=loc, replay=rep, local=localnum, compileWarnings=warn,
                         maxPipelineMs=e.get('maxPipelineMs'), error=(first_error(run, job, e) or e.get('error') or e.get('assertion') or e.get('notRun')),
                         evidence=str(run.relative_to(Q.parents[2])), corpusWall=st.get('wallSeconds'), corpusPassed=st.get('passed')))
extra = Q/'device'/'extra-results.json'
if extra.exists():
    for x in json.loads(extra.read_text()):
        tw = TABLET.get(x['job'], (None, '?', '?')); x.setdefault('tabletWall', tw[0]); x.setdefault('tabletResult', tw[1]); rows.append(x)
def fmt(r):
    g = r.get('gpuVsGlibc'); rp = r.get('replay'); lc = r.get('local') or {}
    num = ('glibc %d/%d/%.3g' % (g['practical'], g['strict'], g['max'])) if g else ('replay %s rb, %s strict, max %.3g' % (rp['readbacks'], rp['strict'], rp['max'] or 0)) if rp else \
          ', '.join('%s=%s' % (k, ('%.3g' % v) if isinstance(v, float) else v) for k, v in list(lc.items())[:3]) or (r.get('numbers') or '-')
    if r.get('localDiag'): num += '; bionic-vs-glibc %d differing/%d practical' % (r['localDiag']['bionicVsGlibcDiffering'], r['localDiag']['bionicVsGlibcPractical'])
    res = 'PASS' if r['passed'] else 'FAIL'
    err = '' if r['passed'] else (' - ' + str(r.get('error'))[:140].replace('|', '/').replace('\n', ' '))
    w = ', '.join('%s %ss' % tuple(t) for t in (r.get('compileWarnings') or [])[:3]) or '-'
    return '| %s | %s | %s | %s | %s | %s%s | %s | %s | %s | %s |' % (r['corpus'], r['job'], r.get('s20Wall') if r.get('s20Wall') is not None else '-',
        r.get('tabletWall') if r.get('tabletWall') is not None else '-', r.get('tabletResult'), res, err, r.get('classification') or '-', num, w, r.get('evidence'))
md = ['| Corpus | Job | S20 wall s | Tablet wall s | Tablet result | S20 result | Class | Numbers | Compile >20 s | Evidence |', '| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |']
md += [fmt(r) for r in rows]
(Q/'checkpoint').mkdir(exist_ok=True)
(Q/'checkpoint'/'sweep-table.md').write_text('\n'.join(md)+'\n\nAdapters seen: %s\n\nTiers seen: %s\n' % (sorted(adapters), sorted(tiers, key=str)))
(Q/'checkpoint'/'sweep-table.json').write_text(json.dumps(dict(rows=rows, adapters=sorted(adapters), tiers=sorted([list(t) for t in tiers], key=str)), indent=1)+'\n')
print('\n'.join(md)); print('adapters', sorted(adapters)); print('tiers', sorted(tiers, key=str))
