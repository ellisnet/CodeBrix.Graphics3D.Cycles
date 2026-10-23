#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 4 diagnostic: replay the captured Samsung node-SVM compiled-graph dispatch with WGSL variants on the
# Samsung (requires the merge-wave-4 samsung lock). Usage: samsung_variants.sh VARIANT.wgsl ...
D=$(cd "$(dirname "$0")" && pwd); M=$D/../..; A="/home/jeremy/Android/Sdk/platform-tools/adb -s R52W207XQ1V"; R=/data/local/tmp/codebrix-m4-arena
$A shell "mkdir -p $R"; $A push $M/arena/android-arm64/arena_replay $M/arena/android-arm64/libcodebrix_wgpu.so $D/run.arena.bin $D/run.params.bin $R/ >/dev/null; $A shell "chmod 755 $R/arena_replay"
for v in "$@"; do $A push $D/$v $R/ >/dev/null; t=$(date +%s)
  timeout 600 $A shell "cd $R; echo \$\$ > active.pid; exec env LD_LIBRARY_PATH=$R ./arena_replay $v run.arena.bin run.params.bin $v.out surface_svm_probe rep 64" > $D/sam-$v.log 2>&1; rc=$?
  $A pull $R/$v.out $D/sam-$v.out >/dev/null 2>&1; $A shell "rm -f $R/$v.out $R/$v"
  echo "[$(date +%H:%M:%S)] samsung arena-replay $v rc=$rc $(( $(date +%s)-t ))s" >> $M/timeline.log
  (cd $D && python3 cmp.py sam-$v.out)
done
$A shell "rm -rf $R"
