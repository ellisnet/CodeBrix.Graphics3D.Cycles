#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the README tables of a Metal comparison run: one table per scene from compare/*.json (roles as compare.py
used them; the FTZ+DAZ x64 set is labelled from the file name, since its backend directory is cpu-x64-linux too),
then the repeat statistics and Metal compile warnings of every meta.json. Usage: tables.py <run directory>."""
import json,sys
from pathlib import Path
R=Path(sys.argv[1])
order=['velvet-subnormal','bsdf__velvet','radial-sides2-r0','radial-sides2-r0.3','radial-sides2-r0.5','radial-sides2-r0.75','radial-sides2-r1','radial-sides3-r0.5']
label={'cpu-x64-linux-ftz':'cpu-x64-linux (FTZ+DAZ)'}
rows={}
for f in sorted((R/'compare').glob('*.json')):
 scene,cand,_,ref=f.stem.split('--');d=json.loads(f.read_text());p=d['pairs'][0]
 nf={b['directory']:b['nonfiniteComponents'] for b in d['backends']}
 refdir=[b['directory'] for b in d['backends'] if b['backend']==p['reference']][0];canddir=[b['directory'] for b in d['backends'] if b['backend']==p['candidate']][0]
 if p['reference']==ref.replace('-ftz',''):rl=label.get(ref,p['reference']);cl=label.get(cand,p['candidate'])
 else:rl=label.get(cand,p['reference']);cl=label.get(ref,p['candidate'])
 rows.setdefault(scene,[]).append((cl,rl,p,nf[refdir],nf[canddir]))
for scene in order:
 print(f'**{scene.replace("__","/")}**');print()
 print('| Candidate | Reference | Bitwise | Practical failures | Strict differences | Max abs | Max rel | Worst pixel (x, y, channel: reference / candidate) | NaN/Inf (ref, cand) |')
 print('|---|---|---|---|---|---|---|---|---|')
 for cl,rl,p,nr,nc in sorted(rows.get(scene,[]),key=lambda r:(not r[0].startswith('metal-m2pro'),r[0]!='metal-m2pro',r[1])):
  w=p['worstPixel'];wp=f"{w['x']}, {w['y']}, {w['channel']}: {w['reference']:.9g} / {w['candidate']:.9g}" if w else ''
  print(f"| {cl} | {rl} | {'yes' if p['bitwiseEqual'] else 'no'} | {p['practicalFailures']} | {p['strictDifferences']} | {p['maxAbsoluteDifference']:.3g} | {p['maxRelativeError']:.3g} | {wp} | {nr}, {nc} |")
 print()
print('| Scene | Backend | Repeat bitwise | repeatMaxAbsDifference | Render s (1st / 2nd) | Warnings |');print('|---|---|---|---|---|---|')
for m in sorted((R/'oracle').rglob('meta.json')):
 d=json.loads(m.read_text());w=d.get('warnings') or []
 print(f"| {d['sceneId']} | {d['backend']} | {d['repeatBitwiseIdentical']} | {d['repeatMaxAbsDifference']:.3g} | {' / '.join(f'{s:.3g}' for s in d['renderSeconds'])} | {len(w)} |")
