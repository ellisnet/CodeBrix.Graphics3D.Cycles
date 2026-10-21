#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Compare baseline vs after gate run folders: every output file by sha256 and the
stdout reports with timing/pipeline fields removed."""
import hashlib,json,sys
from pathlib import Path
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def clean(o):
    if isinstance(o,dict):return {k:clean(v) for k,v in o.items() if k not in ('pipelineReport','pipelines','totalPipelineMs','maxPipelineMs','maxPipelineEntry','decision')}
    if isinstance(o,list):return [clean(v) for v in o]
    return o
base,after=Path(sys.argv[1]),Path(sys.argv[2]);result={}
for d in sorted(p for p in base.iterdir() if p.is_dir()):
    a=after/d.name;r={'identicalFiles':[],'differentFiles':[],'missing':[]}
    for f in sorted(d.iterdir()):
        if not f.is_file() or f.name in ('stdout.txt','stderr.txt','pipelines.jsonl'):continue
        g=a/f.name
        if not g.exists():r['missing'].append(f.name)
        elif sha(f)==sha(g):r['identicalFiles'].append(f.name)
        else:r['differentFiles'].append(f.name)
    try:
        jb=clean(json.loads((d/'stdout.txt').read_text()));ja=clean(json.loads((a/'stdout.txt').read_text()))
        r['reportIdentical']=jb==ja
        if jb!=ja:r['reportDifferences']={k:[jb.get(k),ja.get(k)] for k in set(jb)|set(ja) if jb.get(k)!=ja.get(k)}
    except Exception as e:r['reportError']=repr(e)
    result[d.name]=r
print(json.dumps(result,indent=1))
