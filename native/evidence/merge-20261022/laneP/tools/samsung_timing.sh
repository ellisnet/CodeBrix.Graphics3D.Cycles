#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Lane P: one Samsung lock session (<= 20 min) to time jobs that have no Samsung wall in any record.
cd "$(dirname "$0")"; P=$(realpath ..); L=/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/device-locks; serial=R52W207XQ1V
A="/home/jeremy/Android/Sdk/platform-tools/adb -s $serial"; ref=$P/reference/linux-x64
lg(){ echo "[$(date +%H:%M:%S)] samsung-timing $*" >> $P/timeline.log; }
for x in samsung $serial; do [ -e $L/$x ] && { lg "lock $x HELD by another lane; deferred"; exit 2; }; done
mkdir $L/samsung 2>/dev/null || { lg "lock HELD; deferred"; exit 2; }
echo "lane P (p-pixel-triage) pid $$ $(date -Iseconds) samsung_timing.sh (timing run, max 20 min)" > $L/samsung/owner.txt; lg "lock taken"
END=$(( $(date +%s) + 1200 ))
echo "[$(date +%H:%M:%S)] DECLARED samsung staging push budget 300 s" >> $P/budgets.txt
timeout 300 python3 stage.py push samsung >> $P/device/samsung-timing.log 2>&1; lg "staged exit=$?"
left(){ echo $(( END - $(date +%s) )); }
r=$(left); [ $r -gt 120 ] && { echo "[$(date +%H:%M:%S)] DECLARED samsung timing object/deforming-stationary-images jobTimeout=300 wall<=$r" >> $P/budgets.txt
  timeout $r python3 run_job.py samsung motion timing $(( r - 20 )) 300 --reference $ref --device-scene --jobs object-stationary-images,deforming-stationary-images >> $P/device/samsung-timing.log 2>&1; lg "motion stationary exit=$?"; }
r=$(left); [ $r -gt 120 ] && { echo "[$(date +%H:%M:%S)] DECLARED samsung timing lighting_mis + replays 0,1 jobTimeout=300 wall<=$r" >> $P/budgets.txt
  timeout $r python3 run_job.py samsung pixel-lighting timing $(( r - 20 )) 300 --reference $ref --device-scene --jobs lighting_mis --replays perjob2 >> $P/device/samsung-timing.log 2>&1; lg "lighting_mis exit=$?"; }
$A shell 'rm -rf /data/local/tmp/codebrix-p-*'
left=$($A shell 'ls -d /data/local/tmp/codebrix-p-* 2>/dev/null'; $A shell 'ps -A -o PID,ARGS' | grep -E 'codebrix-p-' | grep -v grep)
lg "cleaned (left: '$left')"; rm $L/samsung/owner.txt && rmdir $L/samsung && lg "lock released"
