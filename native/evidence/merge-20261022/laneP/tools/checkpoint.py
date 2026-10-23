#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Lane P checkpoint: triage table (JSON + Markdown) from plan.json + Pixel run records, status.md, device state.
Usage: checkpoint.py [lock-held|released]"""
import json, sys, datetime, subprocess, re
from pathlib import Path
P = Path(__file__).resolve().parents[1]; CP = P/'checkpoint'; CP.mkdir(exist_ok=True)
plan = json.loads((P/'plan.json').read_text())['rows']
ALIAS = {('regression', 'glass_render'): ('spec-refusal', 'glass_render-spec'), ('regression', 'lighting_mis'): ('lighting', 'lighting_mis'),
         ('regression', 'transparent_lighting'): ('transparent', 'transparent_lighting')}
EXTRA = [dict(corpus='motion (item 2)', job='object-moving-images, device oracle, no reference import', samsungWall=84.9, samsungSource='main:native/work/merge1/runs/samsung/motion-mw1 (object-moving-images)', samsungPassed=True, cutoff=255, key='item2-device-oracle'),
         dict(corpus='motion (item 2)', job='object-moving-images, CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2, no reference import', samsungWall=84.9, samsungSource='main:native/work/merge1/runs/samsung/motion-mw1 (object-moving-images)', samsungPassed=True, cutoff=255, key='item2-x64-oracle')]
runs = []
for d in sorted((P/'runs/pixel').glob('*')) if (P/'runs/pixel').exists() else []:
    try: res = json.loads((d/'results.json').read_text()); st = json.loads((d/'status.json').read_text())
    except Exception: continue
    runs.append((d, st, res))
def find(corpus, job, key=None):
    hit = None
    for d, st, res in runs:
        name = d.name
        if key: ok = ('-'+key+'-') in name
        elif corpus.startswith('hostcap-'): ok = name.startswith(corpus+'-') or name.startswith(corpus)
        else: ok = name.startswith(corpus+'-p-') and '-item2-' not in name or (corpus == 'motion' and job in ('object-moving-images', 'object-replay-motion-0') and '-item2-chain-' in name)
        if ok and job in res: hit = (d, st, res[job])
    return hit
def verdict(row, hit):
    mat = row['corpus'] == 'materials'
    if row['cutoff'] is None: return 'NO BASELINE', None, 'not run (no Samsung baseline, deferred to merge)'
    if not hit: return 'NOT RUN', None, 'not run yet'
    d, st, e = hit; wall = e.get('wallSeconds') or (e.get('report') or {}).get('wallSeconds')
    log = str(d.relative_to(P.parents[2]))
    if e.get('timeout'): v, res = 'NOT SUPPORTED (3x)', e.get('verdictText') or 'FAIL (3x cutoff at %s s)' % e.get('cutoffSeconds')
    elif e.get('notRun'): v, res = 'NOT RUN', 'not run (%s)' % e['notRun']
    elif e.get('passed'): v, res = 'SUPPORTED', 'PASS'
    else:
        err = e.get('error') or e.get('assertion') or ''
        lines = err if isinstance(err, list) else [str(err)]
        key = [l.strip() for l in lines if re.search(r'ERROR|error|lost|failed|Assertion|assert', l) and 'OpenColorIO' not in l]
        v, res = 'NOT SUPPORTED (fails)', 'FAIL: ' + (' / '.join(key) if key else ' / '.join(l.strip() for l in lines if 'OpenColorIO' not in l))[:220]
    if e.get('notRun') == 'source job failed': v = 'NOT RUN (source job not supported)'
    if mat and not row['samsungPassed'] and not v.startswith('NOT RUN'): v = 'PROVISIONAL (' + v + ')'
    if isinstance(wall, (int, float)) and row['cutoff'] and wall > row['cutoff'] and v == 'SUPPORTED': v = 'NOT SUPPORTED (3x)'
    return v, wall, res, log, e.get('maxPipelineMs')
rows = []
for row in plan + EXTRA:
    key = row.get('key'); c, j = ALIAS.get((row['corpus'], row['job']), (row['corpus'], row['job']))
    hit = find('motion', 'object-moving-images', key) if key else find(c, j)
    if row['corpus'] == 'app':
        hit = None
        for d in sorted((P/'android-app').glob('webgpu-pixel-*')) if (P/'android-app').exists() else []:
            s = d/'pixel'/'status.json'
            if s.exists():
                a = json.loads(s.read_text()); err = a.get('error') or a.get('failures') or ''
                to = 'result timeout' in str(err)
                hit = (d, a, dict(passed=a.get('passed'), wallSeconds=a.get('appSeconds') or ('>%s (result timeout)' % row['cutoff'] if to else None), error=err, timeout=to, cutoffSeconds=row['cutoff'],
                                  verdictText=('FAIL (3x cutoff at %s s)' % row['cutoff']) if to else None))
    out = verdict(row, hit) if row['cutoff'] is not None else ('NO BASELINE', None, 'not run (no Samsung baseline, deferred to merge)')
    v, wall, res = out[:3]; log = out[3] if len(out) > 3 else None; pms = out[4] if len(out) > 4 else None
    rows.append(dict(corpus=row['corpus'], job=row['job'], samsungWall=row['samsungWall'], samsungSource=row['samsungSource'], samsungPassed=row['samsungPassed'],
                     cutoff=row['cutoff'], pixelWall=wall, pixelResult=res, verdict=v, maxPipelineMs=pms, log=log))
(CP/'triage-table.json').write_text(json.dumps(rows, indent=1)+'\n')
md = ['| Corpus | Job | Samsung wall (s) | Pixel wall (s) | Cutoff (s) | Pixel result | Verdict | Max pipeline ms | Log |', '| --- | --- | --- | --- | --- | --- | --- | --- | --- |']
for r in rows:
    md.append('| %s | %s | %s%s | %s | %s | %s | %s | %s | %s |' % (r['corpus'], r['job'], r['samsungWall'], '' if r['samsungPassed'] in (True, None) else ' (Samsung FAIL)',
              r['pixelWall'], r['cutoff'], r['pixelResult'].replace('|', '/'), r['verdict'], r['maxPipelineMs'], r['log'] or '-'))
(CP/'triage-table.md').write_text('\n'.join(md)+'\n')
cmd = ['/home/jeremy/Android/Sdk/platform-tools/adb', '-s', '98271FFBA0048D', 'shell', 'ls -d /data/local/tmp/codebrix-p-* 2>/dev/null']
try: remote = subprocess.run(cmd, capture_output=True, text=True, timeout=30).stdout.strip()
except Exception as e: remote = 'adb error '+repr(e)
lock = Path('/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/device-locks/pixel')
remaining = [r['corpus']+'/'+r['job'] for r in rows if r['verdict'] == 'NOT RUN' and r['pixelResult'] == 'not run yet']
now = datetime.datetime.now().strftime('%Y-%m-%d %H:%M:%S')
(CP/'status.md').write_text(f"""# Lane P checkpoint ({now})

Read this first if you are resuming lane P (Pixel 4 XL triage under the device reference policy).
Worktree: /home/jeremy/ClaudeHome/cycles-worktrees/p-pixel-triage (main @ 3392df3d). Outputs: native/work/p/.
Brief: plan section 46 of /home/jeremy/ClaudeHome/PLAN_Cycles_Targetting_WGSL_WebGPU.md; cutoff = 3 x Samsung wall per job.

- Device state: lock dir {lock} {'EXISTS (owner: ' + (lock/'owner.txt').read_text().strip() + ')' if (lock/'owner.txt').exists() else 'absent (released)'}; caller says {sys.argv[1] if len(sys.argv) > 1 else '?'}.
- Remote folders on the Pixel (codebrix-p-*): {remote or 'none'}
- Cutoff table + sources: native/work/p/plan.json (generator tools/make_plan.py; rerun it after Samsung timing runs).
- Triage table so far: checkpoint/triage-table.md / .json (generator tools/checkpoint.py).
- Run steps: `python3 native/work/p/tools/pixel_batch.py STEP...` (`list` shows step ids); it takes/releases the lock
  `.../device-locks/pixel`, stages, runs each job with its cutoff as the hard timeout, cleans the device.
- Samsung baseline timing (lock 'samsung', max 20 min): tools/samsung_timing.sh.
- DOCS (native/DEVICE.md, AGENT-README.txt) MUST be edited only after all device runs: render_gate.inputs() hashes them and the
  runner refuses to run if sources changed since the Android build.
- Not run yet ({len(remaining)}): {', '.join(remaining) or 'none'}
""")
(CP/'cutoff-table.json').write_text((P/'plan.json').read_text())
print('checkpoint', now, len(rows), 'rows,', len(remaining), 'not run')
