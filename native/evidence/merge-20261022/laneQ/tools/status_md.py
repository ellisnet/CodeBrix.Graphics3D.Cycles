#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Lane Q: checkpoint/status.md (completed steps, remaining steps, device state) for a resuming agent."""
import json, subprocess, sys, datetime
from pathlib import Path
Q = Path(__file__).resolve().parents[1]
ALL = 'stage hostgate canary triangle emitter lighting regression transparent terminator specrefusal featurecomb motion precision volume hostcap app materials'.split()
tl = (Q/'timeline.log').read_text().splitlines()
done = [l for l in tl if ' END ' in l or 'staged exit' in l]
dev = subprocess.run(['adb', '-s', 'RFCRB0MPHYH', 'shell', 'ls -d /data/local/tmp/codebrix-q* /data/local/tmp/codebrix-maintained-host-codebrix-q* 2>/dev/null; pm list packages | grep hostvalidation.q'], capture_output=True, text=True, timeout=60).stdout.strip()
lock = Path('/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/device-locks/s20/owner.txt')
rows = json.loads((Q/'checkpoint/sweep-table.json').read_text())['rows'] if (Q/'checkpoint/sweep-table.json').exists() else []
out = ['# Lane Q checkpoint status (written %s, last step: %s)' % (datetime.datetime.now().strftime('%H:%M:%S'), sys.argv[1] if len(sys.argv) > 1 else '?'),
       'Worktree /home/jeremy/ClaudeHome/cycles-worktrees/q-s20-sweep (base 3392df3d). Chain: native/work/q/tools/chain_s20.sh [STEP ...];',
       'steps in order: ' + ' '.join(ALL) + '. A step whose runs/s20/<corpus>-<label>/status.json (or device/android-host-gate, android-app/webgpu-s20) exists is skipped on re-run.',
       'Builds: Linux native/work/cycles-build; Android gates native/work/q/build-android-arm64 (staged-binaries-android-arm64.json); APK native/work/q/android-app/validation-q.',
       'Lock: %s' % (lock.read_text().strip() if lock.exists() else 'not held (s20 lock dir absent)'),
       'S20 remote folders/apps of lane Q: %s' % (dev or 'none'), '', '## Completed (timeline END lines)'] + ['- '+l for l in done] + \
      ['', '## Jobs so far: %d rows, %d pass, %d fail (table: checkpoint/sweep-table.md)' % (len(rows), sum(r['passed'] for r in rows), sum(not r['passed'] for r in rows)),
       '', '## Remaining', 'Every step after the last END line above; then: DEVICE.md section, report, handoff (see the lane brief).']
(Q/'checkpoint'/'status.md').write_text('\n'.join(out)+'\n')
