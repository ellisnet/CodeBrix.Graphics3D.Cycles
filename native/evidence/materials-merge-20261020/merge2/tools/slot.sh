#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 2. Usage: slot.sh LABEL BUDGET_SECONDS COMMAND...  (cold scratch Mesa cache, timeout, exit record)
label=$1; budget=$2; shift 2
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge2; V=$M/validation
export MESA_SHADER_CACHE_DIR=$M/mesa/$label; rm -rf "$MESA_SHADER_CACHE_DIR"; mkdir -p "$MESA_SHADER_CACHE_DIR" $V/$label
cd $R
echo "[$(date +%H:%M:%S)] DECLARED $label budget=${budget}s: $*" >> $M/budgets.txt
start=$(date +%s); echo "[$(date +%H:%M:%S)] START $label" >> $M/timeline.log
timeout "$budget" "$@" > $V/$label/slot.stdout.txt 2> $V/$label/slot.stderr.txt; code=$?
echo "{\"label\":\"$label\",\"exit\":$code,\"seconds\":$(( $(date +%s)-start )),\"budget\":$budget}" > $V/$label/exit.json
echo "[$(date +%H:%M:%S)] END $label exit=$code $(( $(date +%s)-start ))s" >> $M/timeline.log
rm -rf "$MESA_SHADER_CACHE_DIR"
exit $code
