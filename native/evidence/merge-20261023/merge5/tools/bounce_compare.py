#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Compare before/after surface_gate.cpp gate runs: every stdout field except pipelineReport must be equal;
pipelineReport is summarised (pipelines compiled, distinct entries); output files (other than stdout/stderr and the
Mesa cache) are compared by sha256; stderr is compared with timestamps stripped."""
import hashlib, json, re, sys
from pathlib import Path
M = Path(__file__).resolve().parents[1]/'bounce'
out = {}; ok = True
def files(d): return {p.relative_to(d).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(d.rglob('*')) if p.is_file() and 'shader-cache' not in p.parts and p.name not in ('stdout.txt', 'stderr.txt')}
def stderr(d): return [re.sub(r'^\d\d:\d\d\.\d\d \| ', '', l) for l in (d/'stderr.txt').read_text().splitlines()]
for g in ['surface', 'microfacet_bounce', 'glass_bounce', 'principled_bounce']:
    b, a = M/'before'/g, M/'after'/g
    jb, ja = json.loads((b/'stdout.txt').read_text()), json.loads((a/'stdout.txt').read_text())
    pb, pa = jb.pop('pipelineReport'), ja.pop('pipelineReport')
    def summ(p):
        return {k: p.get(k) for k in ('tier', 'degraded', 'pipelines', 'totalPipelineMs', 'maxPipelineMs', 'maxPipelineEntry', 'warnings', 'sizeGuard')}
    fb, fa = files(b), files(a); sb, sa = stderr(b), stderr(a)
    r = dict(fieldsEqual=jb == ja, differingFields=sorted(k for k in set(jb) | set(ja) if jb.get(k) != ja.get(k)),
             filesEqual=fb == fa, files=fa, stderrLinesEqual=sb == sa, stderrDiff=[(x, y) for x, y in zip(sb, sa) if x != y][:10] + ([('len', (len(sb), len(sa)))] if len(sb) != len(sa) else []),
             pipelineReportBefore=summ(pb), pipelineReportAfter=summ(pa),
             stdoutWithoutPipelineReportSha256={'before': hashlib.sha256(json.dumps(jb, sort_keys=True).encode()).hexdigest(), 'after': hashlib.sha256(json.dumps(ja, sort_keys=True).encode()).hexdigest()})
    ok &= r['fieldsEqual'] and r['filesEqual']; out[g] = r
out['identical'] = ok
(M/'compare.json').write_text(json.dumps(out, indent=1)+'\n'); print(json.dumps({g: (v if g == 'identical' else {k: v[k] for k in ('fieldsEqual', 'differingFields', 'filesEqual', 'stderrLinesEqual', 'pipelineReportBefore', 'pipelineReportAfter')}) for g, v in out.items()}, indent=1))
