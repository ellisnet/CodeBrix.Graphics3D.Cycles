#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Collect Phase C per-gate numbers (cases, max film error, strict count, replay readbacks)."""
import json, sys
from pathlib import Path
V = Path(sys.argv[1])
def load(p):
    try: return json.loads(Path(p).read_text())
    except Exception: return None
def nums(r):
    if not isinstance(r, dict): return {}
    out = {}
    for k in ('passed', 'cases', 'maximumAbsoluteError', 'strictDifferences', 'strictFloatFailures', 'strictStageFailures',
              'strictImageFailures', 'maximumImageError', 'paths', 'stages', 'samples', 'invalidCasesRejected', 'evaluations'):
        if k in r: out[k] = r[k]
    res = r.get('results') if isinstance(r.get('results'), dict) else None
    if res and 'exactSchedulingReadbacks' in res: out['replayReadbacks'] = res['exactSchedulingReadbacks']
    if 'maximumAbsoluteImageError' in r: out['replayMaxImageError'] = r['maximumAbsoluteImageError']; out['replayStrict'] = r.get('strictImageFailures')
    return out
summary = {}
for run in [json.loads(l) for l in (V/'runs.jsonl').read_text().splitlines() if l.strip()]:
    name = run['name']; d = V/name; entry = {'run': run}
    if (d/'status.json').exists():
        st = load(d/'status.json')
        if all(isinstance(v, dict) and 'returncode' in v for v in st.values()):
            entry['gates'] = {g: {**{k: v[k] for k in ('returncode', 'seconds', 'passed', 'timedOut')}, **nums(load(d/g/'stdout.txt'))} for g, v in st.items()}
        else:
            entry['status'] = {k: st.get(k) for k in ('complete', 'passed', 'phase', 'inputsUnchanged', 'productionSourcesUnchanged', 'binaryRuntimeInputsUnchanged', 'error')}
    for res in sorted(d.rglob('results.json')) if d.exists() else []:
        data = load(res)
        if isinstance(data, dict):
            entry.setdefault('results', {})[str(res.relative_to(d))] = {k: nums(v if not (isinstance(v, dict) and 'report' in v) else v['report']) for k, v in data.items()}
    for val in sorted(d.rglob('validation.json')) if d.exists() else []:
        if 'replay' in str(val) or val.parent == d:
            entry.setdefault('validations', {})[str(val.relative_to(d))] = nums(load(val))
    summary[name] = entry
print(json.dumps(summary, indent=1))
