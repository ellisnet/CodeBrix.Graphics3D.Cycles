#!/usr/bin/env python3
# Merge wave 8 part 2 (wind-down): compact per-run / per-job listing of runs/<device>/*/results.json for the report (no DEVICE.md, no tables applied).
import json, sys
from pathlib import Path
M = Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge8')
for dev in sys.argv[1:]:
    print('###', dev)
    for run in sorted((M/'runs'/dev).iterdir(), key=lambda p: p.stat().st_mtime):
        st = json.loads((run/'status.json').read_text()) if (run/'status.json').exists() else {}
        res = json.loads((run/'results.json').read_text()) if (run/'results.json').exists() else {}
        print('RUN %s runPassed=%s complete=%s wall=%s srcUnchanged=%s failures=%s' % (run.name, st.get('passed'), st.get('complete'), st.get('wallSeconds'), st.get('productionSourcesUnchanged'), len(st.get('failures') or [])))
        for k, v in res.items():
            rp = v.get('referencePolicy') or {}; rows = [r['authoritative'] for r in rp.get('rows', []) if isinstance(r.get('authoritative'), dict)]
            nums = ''
            if rows: nums = ' glibc practical=%d strict=%d max=%.3g rejudge=%s' % (sum(r.get('practicalFailures', 0) for r in rows), sum(r.get('strictDifferences', 0) for r in rows), max(r.get('maximumAbsoluteError', 0) for r in rows), rp.get('hostRejudgedPassed'))
            rep = v.get('report') or {}
            if isinstance(rep.get('results'), dict): nums += ' replay readbacks=%s max=%.3g strict=%s' % (rep['results'].get('exactSchedulingReadbacks'), rep.get('maximumAbsoluteImageError', 0), rep.get('strictImageFailures'))
            st_ = 'PASS' if v.get('passed') else ('NA' if v.get('notApplicable') else 'FAIL')
            err = '' if v.get('passed') else ' :: ' + str(v.get('assertion') or v.get('error') or v.get('notRun') or v.get('exit') or v.get('notApplicable'))[:260]
            print('  %-4s %-42s wall=%s%s%s' % (st_, k, v.get('wallSeconds'), nums, err))
