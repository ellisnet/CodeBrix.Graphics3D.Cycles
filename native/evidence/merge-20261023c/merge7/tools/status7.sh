#!/bin/bash
# Merge wave 7 part 2 status checkpoint -> checkpoint-2/status.md. usage: status7.sh "<note>"
M=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge7; C=$M/checkpoint-2; mkdir -p $C
{ echo "# Part 2 (evidence) status $(date '+%Y-%m-%d %H:%M:%S')  (cap 7 h from launch 10:38, i.e. 17:38)"; echo; echo "$1"; echo
  echo "## locks held"; ls /home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/device-locks/; echo
  echo "## linux slot ends"; grep -E "END (p2-|p2v-)" $M/timeline.log | sed 's/^/  /'; echo
  echo "## device corpora (passed per run)"
  python3 - <<'P'
import json
from pathlib import Path
M=Path('/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge7/runs')
for d in sorted(M.glob('*/*')):
    s=d/'status.json'
    if not s.exists(): continue
    st=json.loads(s.read_text()); r=json.loads((d/'results.json').read_text()) if (d/'results.json').exists() else {}
    bad=[k for k,v in r.items() if not v.get('passed')]
    print('  %s: complete=%s passed=%s wall=%s jobs=%d notPassed=%s' % (d.relative_to(M), st.get('complete'), st.get('passed'), st.get('wallSeconds'), len(r), bad))
P
  echo; echo "## timeline (tail)"; tail -30 $M/timeline.log; } > $C/status.md
echo "[$(date +%H:%M:%S)] checkpoint-2 status written" >> $M/timeline.log
