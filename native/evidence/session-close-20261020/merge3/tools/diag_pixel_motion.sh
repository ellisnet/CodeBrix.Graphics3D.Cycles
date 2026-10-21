#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 3 diagnostic on the Pixel 4 XL: object_motion_render (moving images) from the merged tree vs from the
# sealed E4 worktree (baseline-merge1 + E4, no M3/M4), same staged libraries and runtime. Lock protocol applies.
cd "$(dirname "$0")"; M=$(realpath ..); L=/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/device-locks; serial=98271FFBA0048D
A="/home/jeremy/Android/Sdk/platform-tools/adb -s $serial"; R=/data/local/tmp/codebrix-mw3-diag; S=/data/local/tmp/codebrix-mw3-stage
lg(){ echo "[$(date +%H:%M:%S)] pixel diag $*" >> $M/timeline.log; }
mkdir $L/$serial 2>/dev/null || { lg "lock HELD"; exit 2; }
echo "merge-wave-3 agent diag_pixel_motion.sh $(date -Iseconds)" > $L/$serial/owner.txt; lg "lock taken"
echo "[$(date +%H:%M:%S)] DECLARED pixel diag object motion A/B (stage push + 2 runs, 900 s each): budget 2700 s" >> $M/budgets.txt
python3 stage.py push pixel > $M/device/pixel-diag.log 2>&1; lg "staged exit=$?"
D=$(find $M/diag-e4-build-android-arm64 -name codebrix_cycles_object_motion_render_gate -type f); $A shell "mkdir -p $R" && $A push $D $R/e4_object_motion_render_gate >> $M/device/pixel-diag.log 2>&1 && $A shell "chmod 755 $R/e4_object_motion_render_gate"
sha256sum $D >> $M/device/pixel-diag.log; $A shell "sha256sum $R/e4_object_motion_render_gate $S/lib/libcodebrix_wgpu.so $S/bin/codebrix_cycles_object_motion_render_gate" >> $M/device/pixel-diag.log
mkdir -p $M/device/pixel-diag
for v in merged:$S/bin/codebrix_cycles_object_motion_render_gate e4:$R/e4_object_motion_render_gate; do n=${v%%:*}; b=${v#*:}; s=$(date +%s)
  timeout 900 $A shell "mkdir -p $R/$n; cd $R/$n; env -u OCIO LD_LIBRARY_PATH=$S/lib CODEBRIX_CYCLES_PIPELINE_LOG=$R/$n/pipelines.jsonl CODEBRIX_CYCLES_PIPELINE_BUDGET_MS=20000 $b $R/$n/run > $R/$n/stdout.txt 2> $R/$n/stderr.txt; echo exit=\$? > $R/$n/exit.txt"
  $A pull $R/$n/stdout.txt $M/device/pixel-diag/$n.stdout.txt >/dev/null; $A pull $R/$n/stderr.txt $M/device/pixel-diag/$n.stderr.txt >/dev/null; $A pull $R/$n/exit.txt $M/device/pixel-diag/$n.exit.txt >/dev/null; $A pull $R/$n/pipelines.jsonl $M/device/pixel-diag/$n.pipelines.jsonl >/dev/null
  lg "$n $(cat $M/device/pixel-diag/$n.exit.txt 2>/dev/null) $(( $(date +%s)-s ))s"
done
$A shell "rm -rf /data/local/tmp/codebrix-mw3-*"; left=$($A shell 'ls -d /data/local/tmp/codebrix-mw3* 2>/dev/null; ps -A -o PID,ARGS | grep -E "codebrix|render_replay" | grep -v grep')
lg "cleaned (left: '$left')"; rm $L/$serial/owner.txt && rmdir $L/$serial && lg "lock released"
