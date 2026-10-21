#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Seal the M4 handoff: ONLY M4's changes on top of the sealed M3 patch (baseline-m4.json).
Before-content of a path is the M3 handoff copy (M3-touched paths) or HEAD. Read-only on git.
The patch is checked by applying it to a scratch copy of the before files and comparing hashes."""
import hashlib,json,shutil,subprocess,sys,tempfile
from pathlib import Path
ROOT=Path('/home/jeremy/ClaudeHome/cycles-worktrees/m4-bump-derivative')
OUT=ROOT/'native/work/m4/handoff'
M3=Path('/home/jeremy/ClaudeHome/cycles-worktrees/m3-procedural/native/work/m3/handoff')
MANIFEST='/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/baseline-m4.json'
BASE=json.loads(Path(MANIFEST).read_text())
M3C=json.loads((M3/'changes.json').read_text())['changes']
sha=lambda b:hashlib.sha256(b).hexdigest()
status=subprocess.run(['git','status','--porcelain','--untracked-files=all'],cwd=ROOT,capture_output=True,text=True,check=True).stdout.splitlines()
candidates=sorted(l[3:] for l in status if not l[3:].startswith('native/work/'))
def before(p):
    if p in M3C:return (M3/'source'/p).read_bytes()
    r=subprocess.run(['git','show','HEAD:'+p],cwd=ROOT,capture_output=True)
    return r.stdout if r.returncode==0 else None
paths=[];changes={}
tmp=Path(tempfile.mkdtemp(prefix='m4seal-',dir='/tmp/claude-1000/-home-jeremy/c2281c28-97ca-4e0e-87ce-8b9b6c8aba81/scratchpad'))
for p in candidates:
    after=(ROOT/p).read_bytes();b=before(p);bsha=None if b is None else sha(b)
    assert bsha==BASE['sourceSha256'].get(p),('before content does not match baseline-m4.json',p)
    if bsha==sha(after):continue
    paths.append(p);changes[p]={'baselineSha256':bsha,'newSha256':sha(after),'added':b is None}
    for side,data in (('a',b),('b',after)):
        if data is not None:(tmp/side/p).parent.mkdir(parents=True,exist_ok=True);(tmp/side/p).write_bytes(data)
patch=''
for p in paths:
    a='a/'+p if changes[p]['baselineSha256'] else '/dev/null'
    r=subprocess.run(['git','diff','--no-index','--binary',a,'b/'+p],cwd=tmp,capture_output=True,text=True)
    assert r.returncode==1 and r.stdout,p
    patch+=r.stdout.replace('a/a/','a/').replace('b/b/','b/')
    dest=OUT/'source'/p;dest.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(ROOT/p,dest)
OUT.mkdir(parents=True,exist_ok=True);(OUT/'changes.patch').write_text(patch)
check=tmp/'check';shutil.copytree(tmp/'a',check) if (tmp/'a').exists() else check.mkdir()
subprocess.run(['git','apply','--binary',str(OUT/'changes.patch')],cwd=check,check=True)
for p in paths:assert sha((check/p).read_bytes())==changes[p]['newSha256'],('patch check',p)
shutil.rmtree(tmp)
json.dump({'baselineManifest':MANIFEST,'baselineHead':BASE['head'],'m3PatchSha256':'00895dfae46f32f4f9a406fb02b6997739dbcabdd18426d3e44c39f0fd6d53a7',
           'worktree':str(ROOT),'changes':changes},open(OUT/'changes.json','w'),indent=1)
extra=json.loads(sys.argv[1]) if len(sys.argv)>1 else {}
handoff={'lane':'M4 materials: bump stages and derivative opcodes','worktree':str(ROOT),'baselineManifest':MANIFEST,'baselineFiles':BASE['files'],
         'baselineHead':BASE['head'],'appliesOn':'01eb27b6 + sealed M3 patch 00895dfa...','patchSha256':sha((OUT/'changes.patch').read_bytes()),'changedPaths':len(paths),
         'patchVerified':'applied to a scratch copy of the before files; every after-hash matches',
         'runtime':'CodeBrix.WebGpu.Native.MitLicenseForever 1.0.290.1433 (unchanged)',**extra}
json.dump(handoff,open(OUT/'handoff.json','w'),indent=1)
print(json.dumps({'paths':paths,'patchSha256':handoff['patchSha256']},indent=1))
