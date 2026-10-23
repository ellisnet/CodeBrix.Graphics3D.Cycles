#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 4: per-device markdown tables (job, wall, result, judged against, evidence) from runs/<device>/*/results.json."""
import json, sys
from pathlib import Path
M = Path(__file__).resolve().parents[1]
def judged(r):
    p = r.get('report') if isinstance(r.get('report'), dict) else None
    rp = r.get('referencePolicy')
    if r.get('expectedRefusal'): return 'expected refusal'
    if rp and 'reference' in rp: return 'glibc, '+rp.get('flow', '')
    if p and p.get('judgedAgainst'): return 'glibc device-scene (replay)'
    if p and 'exactSchedulingReadbacks' in str(p.get('results', '')): return 'source capture CPU film'
    return "device's own CPU / gate"
def numbers(r):
    p = r.get('report') if isinstance(r.get('report'), dict) else {}
    keys = [k for k in ('maximumAbsoluteImageError', 'strictImageFailures', 'strictFloatFailures', 'maximumAbsoluteError', 'strictStageFailures') if k in p]
    rp = r.get('referencePolicy') or {}
    s = ', '.join('%s=%s' % (k, ('%.3g' % p[k]) if isinstance(p[k], float) else p[k]) for k in keys[:3])
    if rp.get('rows'):
        a = [x['authoritative'] for x in rp['rows'] if isinstance(x.get('authoritative'), dict)]
        if a: s += '; host re-judged vs glibc %s: practical=%s strict=%s max=%.3g' % ('PASS' if rp.get('hostRejudgedPassed') else 'FAIL', sum(x.get('practicalFailures', 0) for x in a), sum(x.get('strictDifferences', 0) for x in a), max(x.get('maximumAbsoluteError', 0) for x in a))
    return s
for dev in sys.argv[1:]:
    print('\n### '+dev+'\n\n| Corpus | Job | Wall s | Result | Judged against | Numbers |\n| --- | --- | --- | --- | --- | --- |')
    for run in sorted((M/'runs'/dev).iterdir(), key=lambda p: p.stat().st_mtime):
        f = run/'results.json'
        if not f.exists(): continue
        for job, r in json.loads(f.read_text()).items():
            if r.get('passed'): res = 'PASS'
            elif r.get('timeout'): res = 'TIMEOUT/cutoff'
            elif r.get('notRun'): res = 'not run ('+r['notRun']+')'
            else:
                err = r.get('error') or r.get('assertion') or ''
                if isinstance(err, list): err = ' '.join(e for e in err if e.strip())[-150:]
                res = 'FAIL: '+str(err)[-150:].replace('|', '/')
            w = r.get('wallSeconds') or (r.get('report') or {}).get('wallSeconds') if isinstance(r.get('report'), dict) else r.get('wallSeconds')
            print('| %s | %s | %s | %s | %s | %s |' % (run.name, job, w if w is not None else '-', res, judged(r), numbers(r)))
