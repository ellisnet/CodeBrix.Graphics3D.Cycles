#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 2: Android gate builds (arm64, x64) into native/work/merge2/build-<rid>, review + staging lists.
M=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge2; cd $M/tools
K="glass_render principled_svm principled_bounce principled_render nodes_svm nodes_world nodes_render"
for rid in android-arm64 android-x64; do
  echo "[$(date +%H:%M:%S)] DECLARED android gate build $rid (--jobs 6): budget 3600 s" >> $M/budgets.txt
  s=$(date +%s); timeout 3600 python3 build_android_mw2.py $rid $K > $M/validation/android-build-$rid.log 2>&1; c=$?
  echo "[$(date +%H:%M:%S)] android gate build $rid exit=$c $(( $(date +%s)-s ))s" >> $M/timeline.log
  [ $c = 0 ] && python3 stage.py review $rid >> $M/validation/android-build-$rid.log 2>&1; echo "[$(date +%H:%M:%S)] review $rid exit=$?" >> $M/timeline.log
done
