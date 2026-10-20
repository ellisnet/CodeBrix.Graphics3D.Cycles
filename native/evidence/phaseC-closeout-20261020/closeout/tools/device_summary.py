# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase C close-out: collect device confirmation numbers into device-summary.json."""
import json
from pathlib import Path
C4 = Path(__file__).resolve().parents[1]
out = {}
for status in sorted((C4/'runs').glob('*/*/status.json')):
    run = status.parent
    if run.name == 'host-gate': continue
    st = json.loads(status.read_text()); res = json.loads((run/'results.json').read_text())
    rows = {}
    for k, v in res.items():
        rep = {}
        try: rep = json.loads((run/(k+'.stdout.txt')).read_text())
        except Exception: pass
        d = v.get('decision') or {}; pr = v.get('pipelineReport') or {}; ps = v.get('pipelineSummary') or {}
        rows[k] = dict(passed=v.get('passed'), expect=v.get('expect'), exit=v.get('exit'), wallSeconds=v.get('wallSeconds'),
            tier=d.get('tier'), reason=d.get('reason'), detail=d.get('detail'),
            adapterDevice=(d.get('adapter') or {}).get('device'), adapterDescription=(d.get('adapter') or {}).get('description'),
            knownAdapter=(d.get('knownAdapter') or {}).get('name'), budgetSource=d.get('budgetSource'),
            canary=d.get('canary'), record={k2: (d.get('record') or {}).get(k2) for k2 in ('mode', 'status', 'fullTierCompiles', 'conservativeTierCompiles', 'breadcrumbs')},
            degraded=pr.get('degraded'), warnings=pr.get('warnings'), pipelineTiers=ps.get('tiers'), workgroupSizes=ps.get('workgroupSizes'),
            maxPipelineSeconds=ps.get('maxSeconds'), maxPipelineByEntry={e: max(t) for e, t in (ps.get('secondsByEntry') or {}).items()},
            maximumImageError=rep.get('maximumImageError', rep.get('maximumAbsoluteError')),
            strict=rep.get('strictImageFailures', rep.get('strictDifferences')), strictStage=rep.get('strictStageFailures'))
    out[run.parent.name+'/'+run.name] = dict(passed=st.get('passed'), wallSeconds=st.get('wallSeconds'), declaredBudgetSeconds=st.get('declaredBudgetSeconds'),
        withinBudget=st.get('withinBudget'), inputsUnchanged=st.get('inputsUnchanged'), remoteProcessesAfter=st.get('remoteProcessesAfter'),
        deviceProperties=st.get('deviceProperties'), jobs=rows)
(C4/'device-summary.json').write_text(json.dumps(out, indent=1)+'\n')
for name, r in out.items():
    print(name, 'passed=%s wall=%s/%s inputsUnchanged=%s' % (r['passed'], r['wallSeconds'], r['declaredBudgetSeconds'], r['inputsUnchanged']))
    for k, j in r['jobs'].items():
        print('  %-26s %-5s %6s s %s/%s err=%s strict=%s maxPipe=%.1f deg=%s warn=%d %s' % (k, j['passed'], j['wallSeconds'], j['tier'], j['reason'],
              j['maximumImageError'], j['strict'], j['maxPipelineSeconds'] or 0, j['degraded'], len(j['warnings'] or []), j['adapterDevice'] or ''))
