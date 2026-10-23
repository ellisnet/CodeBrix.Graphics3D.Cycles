#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 3: compact per-device, per-job table from runs/*/*/results.json (authoritative glibc rows summed,
local GPU-vs-device-CPU numbers, replays, compile-budget warnings)."""
import json
from pathlib import Path
M = Path(__file__).resolve().parents[1]
out = ['| Device | Corpus | Job | Result | vs glibc practical / strict / max | local strict / max | replay readbacks / strict / max | compile warnings | job wall |', '| --- | --- | --- | --- | --- | --- | --- | --- | --- |']
for dev in ('hp', 'samsung', 'pixel'):
    for run in sorted((M/'runs'/dev).iterdir()):
        rf = run/'results.json'
        if not rf.exists(): continue
        res = json.loads(rf.read_text()); st = json.loads((run/'status.json').read_text()) if (run/'status.json').exists() else {}
        for job, v in res.items():
            if not isinstance(v, dict): continue
            rp = v.get('referencePolicy') or {}
            rows = [r.get('authoritative', {}) for r in rp.get('rows', [])]
            g = ('%d / %d / %.3g' % (sum(r.get('practicalFailures', 0) for r in rows), sum(r.get('strictDifferences', 0) for r in rows), max(r.get('maximumAbsoluteError', 0) for r in rows))) if rows else '-'
            rep = v.get('report') if isinstance(v.get('report'), dict) else {}
            loc = '-'
            for sk, mk in (('strictDifferences', 'maximumAbsoluteError'), ('strictFloatFailures', 'maximumAbsoluteError'), ('strictImageFailures', 'maximumImageError')):
                if sk in rep: loc = '%s / %.3g' % (rep[sk], rep.get(mk) or 0); break
            rv = v.get('validation') if isinstance(v.get('validation'), dict) else {}
            rr = ('%s / %s / %.3g' % (v.get('readbacks', rv.get('readbacks', '?')), v.get('strict', rv.get('strictImageFailures', '?')), float(v.get('max', rv.get('maximumAbsoluteImageError', 0)) or 0))) if job.startswith(('replay', 'nodes-replay', 'principled-replay')) or 'readbacks' in v else '-'
            warns = [p for p in (v.get('pipelines') or []) if p.get('budgetExceeded') or (p.get('pipelineMs') or 0) > (v.get('budgetMs') or 1e18)]
            pw = v.get('budgetWarnings') or v.get('compileWarnings') or []
            nwarn = len(pw) if isinstance(pw, list) else pw
            result = 'PASS' if v.get('passed') else 'FAIL: ' + str(v.get('error') or v.get('assertion') or v.get('failure') or '')[:110].replace('|', '/')
            out.append('| %s | %s | %s | %s | %s | %s | %s | %s | %s |' % (dev, run.name, job, result, g, loc, rr, nwarn or '-', v.get('wallSeconds', '-')))
        out.append('| %s | %s | (corpus) | %s | | | | | wall %s / budget %s |' % (dev, run.name, 'PASS' if st.get('passed') else 'FAIL', st.get('wallSeconds', '?'), st.get('declaredBudgetSeconds', st.get('budget', '?'))))
(M/'device'/'device-table.md').write_text('\n'.join(out)+'\n'); print('\n'.join(out))
