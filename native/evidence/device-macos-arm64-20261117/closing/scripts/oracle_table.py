#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""CPU-to-Metal table of an oracle run (read only): oracle_table.py <run> > table.md"""
import glob, json, os, sys
run = sys.argv[1]
r = json.load(open(os.path.join(run, 'run.json')))
scenes = sorted(r['scenes'].keys() if isinstance(r['scenes'], dict) else r['scenes'])
commits, clean, warnings, nonfinite = set(), set(), {}, {}
for meta in glob.glob(os.path.join(run, 'oracle', '**', 'meta.json'), recursive=True):
    m = json.load(open(meta))
    commits.add(m['machine'].get('repoCommit')); clean.add(m['machine'].get('repoWorkingTreeClean'))
    if m.get('warnings'): warnings[os.path.relpath(meta, run)] = m['warnings']
def g(e): return '%.3g' % e
print('| Scene | CPU vs Metal: practical / strict / max abs | CPU vs MetalRT: practical / strict / max abs | Metal vs MetalRT max abs | Metal repeat max abs | non-finite |')
print('| --- | --- | --- | --- | --- | --- |')
for s in scenes:
    d = json.load(open(os.path.join(run, 'comparisons', s.replace('/', '__') + '.json')))
    p = {(x['reference'], x['candidate']): x for x in d['pairs']}
    a = p[('cpu-arm64-macos', 'metal-m2pro')]; b = p[('cpu-arm64-macos', 'metal-m2pro-metalrt')]; c = p[('metal-m2pro', 'metal-m2pro-metalrt')]
    be = {x['backend']: x for x in d['backends']}
    nf = sum(x['nonfiniteComponents'] for x in d['backends'])
    rep = max(be['metal-m2pro']['repeatMaxAbsDifference'], be['metal-m2pro-metalrt']['repeatMaxAbsDifference'])
    print(f"| `{s}` | {a['practicalFailures']} / {a['strictDifferences']} / {g(a['maxAbsoluteDifference'])} | "
          f"{b['practicalFailures']} / {b['strictDifferences']} / {g(b['maxAbsoluteDifference'])} | {g(c['maxAbsoluteDifference'])} | {g(rep)} | {nf} |")
print()
print('repoCommit:', sorted(map(str, commits)), ' repoWorkingTreeClean:', sorted(map(str, clean)))
print('meta.json files with warnings:', len(warnings))
for k, v in sorted(warnings.items()): print('-', k, json.dumps(v))
