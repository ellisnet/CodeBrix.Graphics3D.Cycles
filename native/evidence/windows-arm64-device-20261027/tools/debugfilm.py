"""Summarize CODEBRIX_CYCLES_DEBUG_OUTPUT films: python -I -X utf8 debugfilm.py DIR [DIR ...] (oracle runner output dirs)."""
import json,math,struct,sys
from collections import Counter
from pathlib import Path
for d in map(Path,sys.argv[1:]):
    meta=json.loads((d/'meta.json').read_text());w,h=meta['width'],meta['height']
    data=(d/'film-0.f32').read_bytes();px=[struct.unpack_from('<4f',data,16*i) for i in range(w*h)]
    marked=sum(any(abs(c)>=1e29 for c in p[:3]) for p in px);zero=sum(all(c==0 for c in p[:3]) for p in px)
    lens=[math.sqrt(sum(c*c for c in p[:3])) for p in px if not any(abs(c)>=1e29 for c in p[:3])]
    vals=Counter(tuple(round(c,4) for c in p[:3]) for p in px)
    print(f'{d.name}: {w}x{h}, non-finite marked {marked}, zero {zero}, |v| min {min(lens,default=0):.4f} max {max(lens,default=0):.4f}')
    for v,n in vals.most_common(6):print('   ',n,v)
