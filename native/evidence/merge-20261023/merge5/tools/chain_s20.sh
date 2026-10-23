#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 4: rerun on the S20 (RFCRB0MPHYH) the jobs lane Q marked failing, on the merged tree; one job per run,
# timeout max(600 s, 3 x this merge's Samsung tablet wall). Record only; no fixing.
cd "$(dirname "$0")"; M=$(realpath ..); L=/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/device-locks
ref=$M/reference/linux-x64; serial=RFCRB0MPHYH; A="/home/jeremy/Android/Sdk/platform-tools/adb -s $serial"
lg(){ echo "[$(date +%H:%M:%S)] s20 $*" >> $M/timeline.log; }
until grep -q "samsung lock released" $M/timeline.log; do sleep 20; done
python3 s20_timeouts.py > $M/device/s20-timeouts.json || { lg "timeout computation failed"; exit 1; }
mkdir $L/s20 2>/dev/null || { lg "lock HELD, chain not started"; exit 2; }
echo "merge-wave-4 agent (merge S+L+P into main) $(date -Iseconds) chain_s20.sh (lane Q failing jobs rerun)" > $L/s20/owner.txt; lg "lock taken"
echo "[$(date +%H:%M:%S)] DECLARED s20 staging push (stage.py push s20): budget 1200 s" >> $M/budgets.txt
timeout 1200 python3 stage.py push s20 >> $M/device/s20-chain.log 2>&1; lg "staged exit=$?"
for spec in transparent:transparent_lighting transparent:transparent_shadow pixel-motion:object-moving-images pixel-motion:deforming-moving-images materials:nodes_world materials:principled_svm materials:principled_bounce materials:principled_render materials:nodes_svm materials:nodes_render; do
  c=${spec%%:*}; job=${spec#*:}
  to=$(python3 -c "import json;print(json.load(open('$M/device/s20-timeouts.json'))['$job']['timeoutSeconds'])")
  python3 run_corpus.py s20 $c q-$job $((to*2+1800)) $to --reference $ref --device-scene --jobs $job >> $M/device/s20-chain.log 2>&1
  lg "corpus $c-q-$job exit=$? timeout=${to}s"
done
$A shell 'rm -rf /data/local/tmp/codebrix-m5-*'
left=$($A shell 'ls -d /data/local/tmp/codebrix-m5* 2>/dev/null'; $A shell 'ps -A -o PID,ARGS' | grep -E 'codebrix|render_replay' | grep -v grep)
lg "cleaned (left: '$left')"
rm $L/s20/owner.txt && rmdir $L/s20 && lg "lock released"
