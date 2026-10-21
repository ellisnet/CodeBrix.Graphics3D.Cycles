#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Merge wave 2: per-device, per-job table of every device run (authoritative GPU vs glibc reference, bionic
diagnostic, local GPU-vs-device-CPU, replays, tier decision, compile-budget warnings, wall vs budget)."""
import json, sys
from pathlib import Path
M = Path(__file__).resolve().parents[1]
rows = []
ADAPTERS = {}
def pipes(entry, budget_ms, dev):
    p = entry.get('pipelines') or []
    tiers = sorted({(x.get('tier'), x.get('tierReason')) for x in p if x.get('tier')})
    for x in p:
        if x.get('adapter'): ADAPTERS.setdefault(dev, set()).add(x['adapter'].strip())
    warn = sorted({(x.get('entry'), round(x.get('pipelineMs', 0)/1000, 1)) for x in p if x.get('pipelineMs', 0) > budget_ms}, key=lambda t: -t[1])
    return tiers, warn
for dev in ['samsung', 'hp', 'pixel']:
    for run in sorted((M/'runs'/dev).glob('*')) if (M/'runs'/dev).exists() else []:
        st = json.loads((run/'status.json').read_text()) if (run/'status.json').exists() else {}
        res = json.loads((run/'results.json').read_text()) if (run/'results.json').exists() else {}
        head = dict(device=dev, run=run.name, passed=st.get('passed'), wall=st.get('wallSeconds'), budget=st.get('declaredBudgetSeconds'),
                    withinBudget=st.get('withinBudget'), remoteLeft=st.get('remoteProcessesAfter'), inputsUnchanged=st.get('inputsUnchanged'), failures=st.get('failures'))
        rows.append(dict(kind='run', **head))
        for label, e in res.items():
            r = dict(kind='job', device=dev, run=run.name, job=label, passed=e.get('passed'), wall=e.get('wallSeconds'), maxPipelineMs=e.get('maxPipelineMs'))
            if e.get('notRun'): r['notRun'] = e['notRun']
            if not e.get('passed'): r['error'] = e.get('error') or e.get('assertion') or e.get('notRun') or e.get('exit')
            tiers, warn = pipes(e, st.get('pipelineBudgetMs') or 60000, dev); r['tiers'] = tiers; r['budgetWarnings'] = warn
            pol = e.get('referencePolicy') or {}
            reached = [x for x in pol.get('rows', []) if not x['authoritative'].get('notReached')]
            if reached:
                a = [x['authoritative'] for x in reached]; d = [x.get('diagnosticLocalVersusGlibc') or {} for x in reached]; g = [x.get('diagnosticGpuVersusLocal') or {} for x in reached]
                r['gpuVsGlibc'] = dict(cases=f"{len(reached)}/{len(pol['rows'])}", practical=sum(x['practicalFailures'] for x in a), strict=sum(x['strictDifferences'] for x in a), max=max(x['maximumAbsoluteError'] for x in a))
                r['bionicVsGlibc'] = dict(differing=sum(x.get('bitwiseDiffering', 0) for x in d), practical=sum(x.get('practicalFailures', 0) for x in d))
                r['gpuVsBionic'] = dict(practical=sum(x.get('practicalFailures', 0) for x in g), strict=sum(x.get('strictDifferences', 0) for x in g))
                r['hostRejudged'] = pol.get('hostRejudgedPassed')
            rep = e.get('report') or {}
            if 'results' in rep and isinstance(rep['results'], dict):
                r['replay'] = dict(readbacks=rep['results'].get('exactSchedulingReadbacks'), max=rep.get('maximumAbsoluteImageError'), strict=rep.get('strictImageFailures'), fmt=rep.get('captureFormat'))
            elif rep:
                r['local'] = {k: v for k, v in rep.items() if isinstance(v, (int, float)) and not isinstance(v, bool) and any(t in k for t in ('strict', 'maximum', 'Mismatch', 'cases'))}
            rows.append(r)
(M/'device'/'device-summary.json').write_text(json.dumps(dict(rows=rows, adapters={k: sorted(v) for k, v in ADAPTERS.items()}), indent=1)+'\n')
for r in rows:
    if r['kind'] == 'run': print('\n#', r['device'], r['run'], 'passed', r['passed'], 'wall %s/%s' % (r['wall'], r['budget']), 'remoteLeft', r['remoteLeft'])
    else: print('  ', json.dumps({k: v for k, v in r.items() if k not in ('kind', 'device', 'run')}))
