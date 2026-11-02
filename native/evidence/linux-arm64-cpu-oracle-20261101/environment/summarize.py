#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Summarize an oracle run for an evidence README: per-scene repeat identity, render seconds,
failures and the comparison against a reference backend (vs-x64/*.json). Prints Markdown."""
import json,sys
from pathlib import Path
run=Path(sys.argv[1]);backend=sys.argv[2];compare=run/sys.argv[3] if len(sys.argv)>3 else None
r=json.loads((run/'run.json').read_text())
metas={}
for m in sorted((run/'oracle').rglob(backend+'/meta.json')):
 d=json.loads(m.read_text());metas[d['sceneId']]=d
rows=[];counts=dict(identical=0,practical=0,beyond=0,none=0)
for scene in sorted(set(r['scenes'])|set(r.get('failures',{}))):
 failure=r.get('failures',{}).get(scene,{}).get(backend)
 d=metas.get(scene)
 cmp=None
 if compare is not None:
  f=compare/(scene.replace('/','__')+'.json')
  if f.exists():
   pair=[p for p in json.loads(f.read_text())['pairs'] if p['candidate']==backend][0]
   cmp=pair
 if failure:rows.append((scene,'**failed**','','',failure.replace('|','\\|')));continue
 repeat='bitwise' if d['repeatBitwiseIdentical'] else f"differs (max {d['repeatMaxAbsDifference']:.3g})"
 secs=' / '.join(f'{s:.3g}' for s in d['renderSeconds'])
 if cmp is None:result='no x64 film';counts['none']+=1
 elif cmp['bitwiseEqual']:result='identical';counts['identical']+=1
 elif cmp['practicalFailures']==0:
  result=f"within practical (strict diffs {cmp['strictDifferences']}, max abs {cmp['maxAbsoluteDifference']:.3g}, max rel {cmp['maxRelativeError']:.3g})";counts['practical']+=1
 else:
  w=cmp['worstPixel']
  result=(f"**beyond practical**: {cmp['practicalFailures']} components, strict diffs {cmp['strictDifferences']}, max abs {cmp['maxAbsoluteDifference']:.3g}, "
          f"max rel {cmp['maxRelativeError']:.3g}, worst ({w['x']},{w['y']},{w['channel']}) ref {w['reference']:.9g} cand {w['candidate']:.9g}");counts['beyond']+=1
 rows.append((scene,repeat,f"{d['width']}x{d['height']}, {d['samples']} spp",secs,result))
print(f"Scenes: {len(rows)}; rendered: {len(metas)}; failed: {sum(1 for x in rows if x[1]=='**failed**')}; "
      f"repeat bitwise identical: {sum(1 for d in metas.values() if d['repeatBitwiseIdentical'])} of {len(metas)}")
if compare is not None:
 print(f"Against the x64 CPU oracle: identical {counts['identical']}, within the practical criterion {counts['practical']}, "
       f"beyond it {counts['beyond']}, no x64 film {counts['none']}")
print()
print('| Scene | Repeat | Film | Render s (1st / 2nd) | vs cpu-x64-linux |')
print('|---|---|---|---|---|')
for row in rows:print('| '+' | '.join(row)+' |')
