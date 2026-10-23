#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 4: every codebrix Linux target (after the clean package build), in parallel with the Android gate builds
# (arm64 then x64) into native/work/merge4/build-<rid>, their review/staging lists, then the Android host builds.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge4; V=$M/validation; cd $R
K="triangle_sample surface triangle_lighting emitter_sample glass_render glass_bounce lighting_mis lighting light_state broad_light_state broad_lighting transparent_lighting transparent_shadow feature_combination object_motion_render deforming_motion_render camera render volume volume_render volume_boundary closed_volume_render closest shadow shader_data shadow_terminator principled_svm principled_bounce principled_render nodes_svm nodes_world nodes_render"
(
T=$(cmake --build native/work/cycles-build --target help | grep -o 'codebrix_[a-z0-9_]*' | sort -u | tr '\n' ' ')
echo "$T" > $V/v2-targets.txt
echo "[$(date +%H:%M:%S)] DECLARED v2 gate build (all codebrix targets, --jobs 6) budget=2400s" >> $M/budgets.txt
s=$(date +%s); timeout 2400 python3 native/tools/build_cycles.py --jobs 6 $(for t in $T; do echo --target $t; done) > $V/v2-gate-build.log 2>&1; c=$?
echo "[$(date +%H:%M:%S)] v2 gate build exit=$c $(( $(date +%s)-s ))s" >> $M/timeline.log
) &
for rid in android-arm64 android-x64; do
  echo "[$(date +%H:%M:%S)] DECLARED android gate build $rid (--jobs 6): budget 3600 s" >> $M/budgets.txt
  s=$(date +%s); (cd $M/tools && timeout 3600 python3 build_android_m4.py $rid $K > $V/android-build-$rid.log 2>&1); c=$?
  echo "[$(date +%H:%M:%S)] android gate build $rid exit=$c $(( $(date +%s)-s ))s" >> $M/timeline.log
  [ $c = 0 ] && (cd $M/tools && python3 stage.py review $rid >> $V/android-build-$rid.log 2>&1); echo "[$(date +%H:%M:%S)] review $rid exit=$?" >> $M/timeline.log
done
for rid in android-x64 android-arm64; do
  echo "[$(date +%H:%M:%S)] DECLARED android host build $rid (tools/build_android.py --skip-dependencies --jobs 6): budget 2400 s" >> $M/budgets.txt
  s=$(date +%s); timeout 2400 python3 tools/build_android.py --rid $rid --skip-dependencies --jobs 6 > $V/android-host-build-$rid.log 2>&1; c=$?
  echo "[$(date +%H:%M:%S)] android host build $rid exit=$c $(( $(date +%s)-s ))s" >> $M/timeline.log
done
wait
echo "[$(date +%H:%M:%S)] builds DONE" >> $M/timeline.log
