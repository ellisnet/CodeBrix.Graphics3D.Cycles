#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Seal the M1 handoff: patch vs baseline manifest, changes.json, source copies, handoff.json.
Read-only on git (no index/ref changes): new files are diffed with --no-index."""
import hashlib,json,shutil,subprocess,sys
from pathlib import Path
ROOT=Path('/home/jeremy/ClaudeHome/cycles-worktrees/m1-principled')
OUT=ROOT/'native/work/m1/handoff'
BASE=json.loads(Path('/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/baseline-materials.json').read_text())
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
status=subprocess.run(['git','status','--porcelain','--untracked-files=all'],cwd=ROOT,capture_output=True,text=True,check=True).stdout.splitlines()
paths=sorted(l[3:] for l in status if not l[3:].startswith('native/work/'))
OUT.mkdir(parents=True,exist_ok=True)
patch=''
changes={}
for p in paths:
    before=BASE['sourceSha256'].get(p);after=sha(ROOT/p)
    assert before!=after,p
    changes[p]={'baselineSha256':before,'newSha256':after,'added':before is None}
    if before is None:
        r=subprocess.run(['git','diff','--no-index','--binary','/dev/null',p],cwd=ROOT,capture_output=True,text=True)
    else:
        r=subprocess.run(['git','diff','--binary','HEAD','--',p],cwd=ROOT,capture_output=True,text=True)
    assert r.returncode in (0,1) and r.stdout,p
    patch+=r.stdout
    dest=OUT/'source'/p;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/p,dest)
(OUT/'changes.patch').write_text(patch)
json.dump({'baselineManifest':'/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/baseline-materials.json','baselineHead':BASE['head'],
           'worktree':str(ROOT),'changes':changes},open(OUT/'changes.json','w'),indent=1)
extra=json.loads(sys.argv[1]) if len(sys.argv)>1 else {}
handoff={'lane':'M1 Principled BSDF closures (WGSL backend)','worktree':str(ROOT),'baselineManifest':'/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/baseline-materials.json',
         'baselineHead':BASE['head'],'patchSha256':sha(OUT/'changes.patch'),'changedPaths':len(paths),
         'runtime':'CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433 (unchanged)',**extra}
json.dump(handoff,open(OUT/'handoff.json','w'),indent=1)
print(json.dumps({'paths':paths,'patchSha256':handoff['patchSha256']},indent=1))
