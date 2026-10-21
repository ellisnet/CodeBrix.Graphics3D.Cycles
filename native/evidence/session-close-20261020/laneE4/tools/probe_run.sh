#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Phase E4 (copy of E2): run probe_replay on the Samsung for one staged capture; pulls mismatch/dumps.
# Usage: probe_run.sh CAPTURE LABEL TIMEOUT_S [ENV=VAL ...]
set -u
E=/home/jeremy/ClaudeHome/cycles-worktrees/e4-mode4-adreno/native/work/e4
ADB="/home/jeremy/Android/Sdk/platform-tools/adb -s R52W207XQ1V"; R=/data/local/tmp/codebrix-e4-probe
cap=$1; label=$2; to=$3; shift 3
out=$E/probe/runs/$label; rm -rf $out; mkdir -p $out
echo "[$(date +%H:%M:%S)] probe START $label cap=$cap env=$*" | tee -a $E/handoff/timeline.log
$ADB shell "rm -rf $R/out-$label; mkdir -p $R/out-$label"
timeout $to $ADB shell "cd $R/out-$label && env LD_LIBRARY_PATH=/data/local/tmp/codebrix-e4-stage/lib $* $R/bin/probe_replay $R/$cap $R/out-$label/r" > $out/stdout.txt 2> $out/stderr.txt
rc=$?
echo "rc=$rc" > $out/rc.txt
$ADB pull $R/out-$label $out/pulled >/dev/null 2>&1
$ADB shell "rm -rf $R/out-$label"
echo "[$(date +%H:%M:%S)] probe END $label rc=$rc $(tail -1 $out/stderr.txt)" | tee -a $E/handoff/timeline.log
