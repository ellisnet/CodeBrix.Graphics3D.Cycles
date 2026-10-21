#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 3: Android gate builds (arm64, x64) into native/work/merge3/build-<rid>, review + staging lists.
M=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge3; cd $M/tools
K="triangle_sample surface triangle_lighting emitter_sample glass_render glass_bounce lighting_mis lighting light_state broad_light_state broad_lighting transparent_lighting transparent_shadow feature_combination object_motion_render deforming_motion_render camera render volume volume_render volume_boundary closed_volume_render closest shadow shader_data shadow_terminator principled_svm principled_bounce principled_render nodes_svm nodes_world nodes_render"
for rid in android-arm64 android-x64; do
  echo "[$(date +%H:%M:%S)] DECLARED android gate build $rid (--jobs 6): budget 3600 s" >> $M/budgets.txt
  s=$(date +%s); timeout 3600 python3 build_android_mw3.py $rid $K > $M/validation/android-build-$rid.log 2>&1; c=$?
  echo "[$(date +%H:%M:%S)] android gate build $rid exit=$c $(( $(date +%s)-s ))s" >> $M/timeline.log
  [ $c = 0 ] && python3 stage.py review $rid >> $M/validation/android-build-$rid.log 2>&1; echo "[$(date +%H:%M:%S)] review $rid exit=$?" >> $M/timeline.log
done
