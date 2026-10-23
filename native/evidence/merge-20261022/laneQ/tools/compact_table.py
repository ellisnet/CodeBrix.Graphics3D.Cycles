#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Lane Q: compact sweep table (replays of one source grouped) for DEVICE.md and the report."""
import json, re
from pathlib import Path
Q = Path(__file__).resolve().parents[1]
rows = json.loads((Q/'checkpoint/sweep-table.json').read_text())['rows']
groups = []
def key(r):
    j = r['job']
    m = re.match(r'(.*?)-?replay-(motion-)?[a-z0-9]+$', j) or re.match(r'(tri|term)\d$', j)
    return (r['corpus'], ((m.group(1) or 'triangle_lighting') + ' replays') if m else j)
seen = {}
for r in rows:
    k = key(r)
    if k not in seen: seen[k] = []; groups.append(k)
    seen[k].append(r)
def num(r):
    g, rp = r.get('gpuVsGlibc'), r.get('replay')
    if g: return '%d / %d / %.3g' % (g['practical'], g['strict'], g['max'])
    if r.get('numbers'): return r['numbers']
    lc = r.get('local') or {}
    for s, m in (('strictDifferences', 'maximumAbsoluteError'), ('strictFloatFailures', 'maximumAbsoluteError'), ('strictImageFailures', 'maximumImageError'), ('strictFailures', 'maximumError'), ('strictStageFailures', 'maximumStageError')):
        if s in lc: return 'local %s / %.3g' % (lc[s], lc.get(m) or 0)
    return '-'
out = ['| Corpus | Job | S20 wall s | Tablet wall s | Tablet | S20 | Class | practical / strict / max (vs glibc unless "local") | Compile > 20 s | Evidence |', '| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |']
for k in groups:
    rs = seen[k]
    if len(rs) == 1 and not k[1].endswith('replays'):
        r = rs[0]
        res = 'PASS' if r['passed'] else 'FAIL'
        w = ', '.join('%s %.0f' % (t[0], t[1]) for t in (r.get('compileWarnings') or [])[:2]) or '-'
        out.append('| %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |' % (k[0], k[1], r.get('s20Wall') or '-', r.get('tabletWall') or '-', r.get('tabletResult'), res,
                   r.get('classification') or '-', num(r), w, r['evidence'].replace('native/work/q/', 'q/')))
    else:
        npass = sum(x['passed'] for x in rs); notrun = sum(bool(x.get('classification', '').startswith('not run')) for x in rs)
        walls = [x['s20Wall'] for x in rs if x.get('s20Wall')]; tw = [x['tabletWall'] for x in rs if x.get('tabletWall')]
        reps = [x['replay'] for x in rs if x.get('replay')]
        res = ('PASS %d/%d' % (npass, len(rs))) if npass == len(rs) else ('not run %d/%d (source failed)' % (notrun, len(rs)) if notrun == len(rs) else 'PASS %d/%d' % (npass, len(rs)))
        n = ('readbacks %s; strict %s; max %.3g' % ('/'.join(str(x['readbacks']) for x in reps), '/'.join(str(x['strict']) for x in reps), max(x['max'] or 0 for x in reps))) if reps else '-'
        tres = sorted({x.get('tabletResult') for x in rs})
        out.append('| %s | %s (%d) | %s | %s | %s | %s | %s | %s | - | %s |' % (k[0], k[1], len(rs), ('%.0f-%.0f' % (min(walls), max(walls))) if walls else '-',
                   ('%.0f-%.0f' % (min(tw), max(tw))) if tw else '-', ', '.join(tres), res, '-' if npass == len(rs) else 'not run: source job failed', n, rs[0]['evidence'].replace('native/work/q/', 'q/')))
(Q/'checkpoint/sweep-table-compact.md').write_text('\n'.join(out)+'\n'); print('\n'.join(out))
