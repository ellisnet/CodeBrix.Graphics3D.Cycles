#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""M3: compare gate reports of a run with one or more sealed runs.

Flattens every results.json and JSON stdout report below each directory into
path:key -> scalar. Count-like keys must be equal; error/failure keys must be
<= the best sealed value (match or beat); booleans/strings must be equal.
Usage: compare_sealed.py OUTPUT.json NEW_DIR SEALED_DIR [SEALED_DIR...] [--map new=old ...]
"""
import json,sys
from pathlib import Path
def flat(base):
    out={}
    for p in sorted(Path(base).rglob('*')):
        if p.name not in ('results.json','stdout.txt') or not p.is_file():continue
        try:data=json.loads(p.read_text())
        except Exception:continue
        rel=str(p.parent.relative_to(base))
        def walk(prefix,v):
            if isinstance(v,dict):
                for k,x in v.items():walk(prefix+'.'+k if prefix else k,x)
            elif isinstance(v,(int,float,bool,str)) or v is None:out[rel+':'+prefix]=v
        if p.name=='results.json':
            for k,v in data.items():walk(k,v)
        else:walk('',data)
    return out
WORSE_IF_GREATER=('error','failure','failures','ratio','strict','seconds','ms')
def kind(key):
    k=key.split(':',1)[1];last=k.split('.')[-1].lower()
    if 'pipelinereport' in k.lower() or last.endswith('ms') or any(w in last for w in ('seconds','sha256','library','field','actual','expected','output','path')) and last not in ('paths',):return 'info'
    if any(w in last for w in ('error','failure','ratio')) or last.startswith('strict'):return 'metric'
    return 'exact'
def main():
    args=sys.argv[1:];out=args.pop(0);new=flat(args.pop(0));sealed=[flat(a) for a in args]
    rows=[];bad=0
    for key in sorted(new):
        olds=[s[key] for s in sealed if key in s]
        if not olds:rows.append((key,new[key],None,'new-only'));continue
        k=kind(key);v=new[key]
        if k=='info':status='info'
        elif k=='metric' and isinstance(v,(int,float)) and not isinstance(v,bool):
            best=min(o for o in olds if isinstance(o,(int,float)));status='match' if v==best else ('better' if v<best else ('within-sealed' if v<=max(olds) else 'WORSE'))
        else:status='match' if v in olds else 'DIFF'
        if status in ('WORSE','DIFF'):bad+=1
        rows.append((key,v,olds,status))
    missing=[k for s in sealed for k in s if k not in new]
    Path(out).write_text(json.dumps({'regressions':bad,'missingKeys':sorted(set(missing)),'rows':rows},indent=1))
    for r in rows:
        if r[3] in ('WORSE','DIFF','better','within-sealed'):print(r[3],r[0],r[1],r[2])
    print('regressions',bad,'missing',len(set(missing)))
if __name__=='__main__':main()
