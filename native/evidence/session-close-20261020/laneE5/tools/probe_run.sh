#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Phase E5 (pattern of E2's probe_run.sh): run the diagnostic sampler_probe on the Pixel; pulls the dumps.
# Usage: probe_run.sh LABEL TIMEOUT_S [WGSL_FILE|-] [ENV=VAL ...]
set -u
E=/home/jeremy/ClaudeHome/cycles-worktrees/e5-pixel-sampler/native/work/e5
ADB="/home/jeremy/Android/Sdk/platform-tools/adb -s 98271FFBA0048D"; S=/data/local/tmp/codebrix-e5-stage; R=/data/local/tmp/codebrix-e5-probe
label=$1; to=$2; wgsl=$3; shift 3
out=$E/probe/runs/$label; rm -rf $out; mkdir -p $out
$ADB shell "rm -rf $R/out-$label; mkdir -p $R/out-$label"
extra=""
if [ "$wgsl" != "-" ]; then $ADB push $wgsl $R/out-$label/probe.wgsl >/dev/null; extra="E5_WGSL=$R/out-$label/probe.wgsl"; cp $wgsl $out/probe.wgsl; fi
echo "[$(date +%H:%M:%S)] probe START $label wgsl=$wgsl env=$*" | tee -a $E/timeline.log
s=$(date +%s)
timeout $to $ADB shell "cd $R/out-$label && env -u OCIO LD_LIBRARY_PATH=$S/lib CODEBRIX_CYCLES_PIPELINE_BUDGET_MS=20000 CODEBRIX_CYCLES_PIPELINE_LOG=$R/out-$label/pipelines.jsonl $extra $* $R/sampler_probe $R/out-$label/run" > $out/stdout.txt 2> $out/stderr.txt
rc=$?
echo "rc=$rc seconds=$(( $(date +%s)-s ))" > $out/rc.txt
$ADB pull $R/out-$label $out/pulled >/dev/null 2>&1
$ADB shell "rm -rf $R/out-$label"
echo "[$(date +%H:%M:%S)] probe END $label rc=$rc $(( $(date +%s)-s ))s $(grep -v OpenColorIO $out/stderr.txt | tail -1)" | tee -a $E/timeline.log
