#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""M2 handoff: changes.json/changes.patch against baseline-materials.json, source/ copies,
validation copies and handoff.json (patch sha256 and every file hash)."""
import hashlib,json,os,shutil,subprocess,difflib
from pathlib import Path
TREE=Path('/home/jeremy/ClaudeHome/cycles-worktrees/m2-svm-nodes');M2=TREE/'native/work/m2';H=M2/'handoff'
base=json.load(open('/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/baseline-materials.json'))['sourceSha256']
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def skip(rel):
    parts=rel.split('/')
    return rel.startswith('native/work/') or '__pycache__' in parts or rel.startswith('.git') or (parts[0] in('tests','src') and ('bin' in parts or 'obj' in parts))
cur={}
for root,dirs,files in os.walk(TREE):
    dirs[:]=[d for d in dirs if d!='.git']
    for f in files:
        p=Path(root)/f;rel=str(p.relative_to(TREE))
        if skip(rel) or p.is_symlink():continue
        cur[rel]=sha(p)
b={k:v for k,v in base.items() if not skip(k)}
changed=sorted(k for k in cur if k in b and cur[k]!=b[k]);added=sorted(k for k in cur if k not in b);removed=sorted(k for k in b if k not in cur)
if H.exists():shutil.rmtree(H)
H.mkdir(parents=True)
json.dump(dict(baseline='baseline-materials.json',changed=changed,added=added,removed=removed,rows=[dict(path=k,before=b.get(k),after=cur.get(k)) for k in changed+added+removed]),open(H/'changes.json','w'),indent=1)
patch=''
for k in changed+added:
    old=subprocess.run(['git','show','HEAD:'+k],cwd=TREE,capture_output=True,text=True).stdout if k in changed else ''
    new=(TREE/k).read_text()
    patch+=''.join(difflib.unified_diff(old.splitlines(True),new.splitlines(True),'a/'+k if k in changed else '/dev/null','b/'+k))
(H/'changes.patch').write_text(patch)
for k in changed+added:
    d=H/'source'/k;d.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(TREE/k,d)
for name in ['budgets.txt','timeline.txt','census.md']:shutil.copy2(M2/name,H/name)
for d in ['final/nodes-gate','final/linux-all-lanes','final/audits','final/compile','after','baseline']:
    src=M2/d
    if src.exists():shutil.copytree(src,H/'validation'/d,ignore=shutil.ignore_patterns('*.bin','*.ppm','bin','lib','overlay','mesa-cache','shader-cache','*.wgsl'),dirs_exist_ok=True)
shutil.copytree(M2/'tools',H/'tools',dirs_exist_ok=True)
files={str(p.relative_to(H)):sha(p) for p in sorted(H.rglob('*')) if p.is_file()}
json.dump(dict(worktree=str(TREE),head='01eb27b6',patchSha256=sha(H/'changes.patch'),changed=changed,added=added,removed=removed,fileSha256=files),open(H/'handoff.json','w'),indent=1)
print(len(changed),'changed',len(added),'added',len(removed),'removed;',len(files),'files sealed')
