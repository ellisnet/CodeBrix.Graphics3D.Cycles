#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""The Linux ARM64 run of 24b74e09 against its own meta.json and against linux-arm64-cpu-oracle-20261113 (read only).
linux_compare.py RUN OLD > comparison-20261113.json"""
import collections, hashlib, json, sys
from pathlib import Path
B = 'cpu-arm64-linux'
run, old = Path(sys.argv[1]), Path(sys.argv[2])
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
r = json.loads((run/'run.json').read_text()); o = json.loads((old/'run.json').read_text())
rep = dict(run=str(run), compared=str(old), scenes=len(r['scenes']), failures=r.get('failures') or {},
           commits=collections.Counter(), nonIdentical=[], filmBytesNotMatchingMeta=[], warnings=[],
           shared=dict(scenes=0, films=0, filmsIdentical=0, moved=[], passFilms=0, passFilmsIdentical=0, passesMoved=[],
                       wallClockPassesNotCompared=[]),
           onlyInCompared=sorted(set(o['scenes']) - set(r['scenes'])), newScenes=sorted(set(r['scenes']) - set(o['scenes'])))
for s in sorted(r['scenes']):
    d = run/'oracle'/s/B; m = json.loads((d/'meta.json').read_text())
    rep['commits'][f"{m['machine'].get('repoCommit')} clean={m['machine'].get('repoWorkingTreeClean')}"] += 1
    if not m['repeatBitwiseIdentical']: rep['nonIdentical'].append(s)
    for i, h in enumerate(m['filmSha256']):
        if sha(d/f'film-{i}.f32') != h: rep['filmBytesNotMatchingMeta'].append([s, f'film-{i}.f32'])
    for p in m.get('passes') or []:
        if p.get('written'):
            for i, h in enumerate(p['filmSha256']):
                if sha(d/f"pass-{p['index']}-{i}.f32") != h: rep['filmBytesNotMatchingMeta'].append([s, f"pass-{p['index']}-{i}.f32"])
    rep['warnings'] += [[s, w] for w in m.get('warnings') or []]
    if s not in o['scenes']: continue
    om = json.loads((old/'oracle'/s/B/'meta.json').read_text()); sh = rep['shared']; sh['scenes'] += 1
    for i, (a, b) in enumerate(zip(om['filmSha256'], m['filmSha256'])):
        sh['films'] += 1; sh['filmsIdentical'] += a == b
        if a != b: sh['moved'].append([s, i])
    op = {p['index']: p for p in om.get('passes') or [] if p.get('written')}
    np_ = {p['index']: p for p in m.get('passes') or [] if p.get('written')}
    for k in sorted(set(op) | set(np_)):
        if k not in op or k not in np_: sh['passesMoved'].append([s, k, 'present in one run only']); continue
        if op[k]['name'] == 'RenderTime': sh['wallClockPassesNotCompared'].append([s, k, 'RenderTime']); continue
        for i, (a, b) in enumerate(zip(op[k]['filmSha256'], np_[k]['filmSha256'])):
            sh['passFilms'] += 1; sh['passFilmsIdentical'] += a == b
            if a != b: sh['passesMoved'].append([s, k, i])
rep['commits'] = dict(rep['commits'])
rep['newScenesByFamily'] = dict(sorted(collections.Counter(s.split('/')[0] for s in rep['newScenes']).items()))
print(json.dumps(rep, indent=1))
