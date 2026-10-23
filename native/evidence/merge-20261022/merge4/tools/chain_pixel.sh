#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 4: Pixel 4 XL provisional materials jobs ONLY (lane P's table), one job per run, each with a hard cutoff
# max(5 s, 3 x this merge's Samsung wall for the same job; lane S's device-times.tsv when this merge has none).
# run_corpus.py kills the exact PID (pid file + readlink check) at the cutoff. Nothing else runs on the Pixel.
cd "$(dirname "$0")"; M=$(realpath ..); L=/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/device-locks
ref=$M/reference/linux-x64; serial=98271FFBA0048D; A="/home/jeremy/Android/Sdk/platform-tools/adb -s $serial"
lg(){ echo "[$(date +%H:%M:%S)] pixel $*" >> $M/timeline.log; }
until grep -q "samsung corpus materials-m4 exit=" $M/timeline.log; do grep -q "samsung chain not started\|samsung lock HELD" $M/timeline.log && { lg "not started (no Samsung materials walls)"; exit 1; }; sleep 20; done
python3 pixel_cutoffs.py > $M/device/pixel-cutoffs.json || { lg "cutoff computation failed"; exit 1; }
mkdir $L/pixel 2>/dev/null || { lg "lock HELD, chain not started"; exit 2; }
echo "merge-wave-4 agent (merge S+L+P into main) $(date -Iseconds) chain_pixel.sh (provisional materials only)" > $L/pixel/owner.txt; lg "lock taken"
echo "[$(date +%H:%M:%S)] DECLARED pixel staging push (stage.py push pixel): budget 1200 s" >> $M/budgets.txt
timeout 1200 python3 stage.py push pixel >> $M/device/pixel-chain.log 2>&1; lg "staged exit=$?"
for job in principled_svm principled_bounce principled_render nodes_svm nodes_render; do
  cut=$(python3 -c "import json;print(json.load(open('$M/device/pixel-cutoffs.json'))['$job']['cutoffSeconds'])")
  python3 run_corpus.py pixel materials p-$job $((cut+1800)) $cut --pipeline-budget-ms 20000 --reference $ref --device-scene --jobs $job >> $M/device/pixel-chain.log 2>&1
  lg "corpus materials-p-$job exit=$? cutoff=${cut}s"
done
$A shell 'rm -rf /data/local/tmp/codebrix-m4-*'
left=$($A shell 'ls -d /data/local/tmp/codebrix-m4* 2>/dev/null'; $A shell 'ps -A -o PID,ARGS' | grep -E 'codebrix|render_replay' | grep -v grep)
lg "cleaned (left: '$left')"
rm $L/pixel/owner.txt && rmdir $L/pixel && lg "lock released"
