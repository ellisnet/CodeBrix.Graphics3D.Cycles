#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase X1: per-device, per-gate table of the device runs (authoritative vs glibc, diagnostics)."""
import json
from pathlib import Path
X1 = Path(__file__).resolve().parents[1]
rows = []
for device in ['samsung', 'hp']:
    for run in ['r1', 'r2']:
        for corpus in ['emitter', 'triangle', 'regression']:
            folder = X1/'runs'/device/(corpus+'-'+run)
            if not (folder/'results.json').exists(): continue
            for label, entry in json.loads((folder/'results.json').read_text()).items():
                if label.startswith('replay-'): continue
                policy = entry.get('referencePolicy', {})
                row = dict(device=device, run=run, gate=label, devicePassed=entry.get('passed'), exit=entry.get('exit'),
                           flow=policy.get('flow', 'local only' if not policy else None), hostPassed=policy.get('hostRejudgedPassed'))
                reached = [r for r in policy.get('rows', []) if not r['authoritative'].get('notReached')]
                if reached:
                    a = [r['authoritative'] for r in reached]
                    d = [r['diagnosticLocalVersusGlibc'] for r in reached if r.get('diagnosticLocalVersusGlibc')]
                    g = [r['diagnosticGpuVersusLocal'] for r in reached if r.get('diagnosticGpuVersusLocal')]
                    row.update(casesReached=len(reached), casesTotal=len(policy['rows']),
                               gpuVsGlibc=dict(practical=sum(x['practicalFailures'] for x in a), strict=sum(x['strictDifferences'] for x in a),
                                               max=max(x['maximumAbsoluteError'] for x in a)),
                               bionicVsGlibc=dict(differing=sum(x['bitwiseDiffering'] for x in d), maxUlps=max((x['maximumUlps'] for x in d), default=0),
                                                  practical=sum(x['practicalFailures'] for x in d), max=max((x['maximumAbsoluteError'] for x in d), default=0)),
                               gpuVsBionic=dict(practical=sum(x['practicalFailures'] for x in g), strict=sum(x['strictDifferences'] for x in g),
                                                max=max((x['maximumAbsoluteError'] for x in g), default=0)))
                elif entry.get('report'):
                    rep = entry['report']; row.update(report={k: rep.get(k) for k in ['strictDifferences', 'maximumAbsoluteError', 'cases'] if k in rep})
                if not entry.get('passed'): row['error'] = (entry.get('error') or [entry.get('assertion', '')])[-1] if isinstance(entry.get('error'), list) else entry.get('assertion') or entry.get('error')
                row['wallSeconds'] = entry.get('wallSeconds')
                rows.append(row)
        cap = X1/'runs'/device/('hostcap-linux-'+run)
        if (cap/'status.json').exists():
            st = json.loads((cap/'status.json').read_text()); res = json.loads((cap/'results.json').read_text()) if (cap/'results.json').exists() else {}
            rows.append(dict(device=device, run=run, gate='linux-triangle-captures', flow='Linux capture (unchanged)',
                             results={k: (v.get('passed'), v.get('report', {}).get('results', {}).get('exactSchedulingReadbacks')) for k, v in res.items()},
                             wallSeconds=st.get('wallSeconds')))
(X1/'summary.json').write_text(json.dumps(rows, indent=1)+'\n')
for r in rows:
    print(json.dumps(r))
