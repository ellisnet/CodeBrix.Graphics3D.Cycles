#!/usr/bin/env python3
# Builds the sealed D3 handoff from the worktree without touching git refs/index.
import hashlib, json, shutil, subprocess, sys
from pathlib import Path
TREE = Path('/home/jeremy/ClaudeHome/cycles-worktrees/d3-native-abi')
SEED = json.loads(Path('/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/baseline-phaseD.json').read_text())
OUT = TREE/'native/work/d3/handoff'
def sha(p): return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def git(*a): return subprocess.run(['git', '-C', str(TREE), *a], capture_output=True, check=True).stdout
assert git('rev-parse', 'HEAD').decode().strip() == SEED['head']
paths = sorted(line[3:] for line in git('status', '--porcelain', '--untracked-files=all').decode().splitlines())
if OUT.exists(): shutil.rmtree(OUT)
(OUT/'source').mkdir(parents=True)
changes, patch = [], b''
for path in paths:
    before, after = SEED['sourceSha256'].get(path), sha(TREE/path)
    assert before != after, path
    changes.append(dict(path=path, change='added' if before is None else 'modified', beforeSha256=before, afterSha256=after))
    target = OUT/'source'/path; target.parent.mkdir(parents=True, exist_ok=True); shutil.copy2(TREE/path, target)
    if before is None:
        patch += subprocess.run(['git', '-C', str(TREE), 'diff', '--no-index', '--binary', '--', '/dev/null', path], capture_output=True).stdout
    else:
        assert hashlib.sha256(git('show', SEED['head']+':'+path)).hexdigest() == before, path
        patch += git('diff', '--binary', SEED['head'], '--', path)
(OUT/'changes.patch').write_bytes(patch)
(OUT/'changes.json').write_text(json.dumps(dict(seedManifest='/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/baseline-phaseD.json', seedHead=SEED['head'], changes=changes), indent=2)+'\n')
validation = OUT/'validation'; validation.mkdir()
for name in sys.argv[1:]:
    src = TREE/name
    if src.is_dir(): shutil.copytree(src, validation/src.name, dirs_exist_ok=True, ignore=shutil.ignore_patterns('*.bin', '*.f32', '*.ppm', 'mesa', 'cache'))
    else: shutil.copy2(src, validation/src.name)
(OUT/'handoff.json').write_text(json.dumps(dict(agent='phaseD3-native-abi', seedHead=SEED['head'], changedPaths=len(changes),
    added=sum(c['change'] == 'added' for c in changes), modified=sum(c['change'] == 'modified' for c in changes),
    patch='changes.patch', patchSha256=sha(OUT/'changes.patch'), changesJsonSha256=sha(OUT/'changes.json'),
    apply='git apply --check changes.patch on a tree matching the seed manifest, then git apply'), indent=2)+'\n')
print(json.dumps(dict(paths=len(changes), patch=sha(OUT/'changes.patch'))))
