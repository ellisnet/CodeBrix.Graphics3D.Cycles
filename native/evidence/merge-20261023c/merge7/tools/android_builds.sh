#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 7: Android gate builds (arm64 then x64) in merge 5's build trees + review/staging lists into native/work/merge7,
# then the Android host builds (as merge wave 5's android_builds.sh, --jobs 4). No device is touched.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge7; V=$M/validation; cd $R
K="triangle_sample surface triangle_lighting emitter_sample glass_render glass_bounce lighting_mis lighting light_state broad_light_state broad_lighting transparent_lighting transparent_shadow feature_combination object_motion_render deforming_motion_render camera render volume volume_render volume_boundary closed_volume_render closest shadow shader_data shadow_terminator principled_svm principled_bounce principled_render nodes_svm nodes_world nodes_render microfacet_render microfacet_svm microfacet_bounce glass_svm environment"
L=${1:-1}
for rid in android-arm64 android-x64; do
  echo "[$(date +%H:%M:%S)] DECLARED android gate build $rid run $L (--jobs 4): budget 3600 s" >> $M/budgets.txt
  s=$(date +%s); (cd $M/tools && timeout 3600 python3 build_android_m6.py $rid $K > $V/android-build-$rid-$L.log 2>&1); c=$?
  echo "[$(date +%H:%M:%S)] android gate build $rid run $L exit=$c $(( $(date +%s)-s ))s" >> $M/timeline.log
  [ $c = 0 ] && (cd $M/tools && python3 stage.py review $rid >> $V/android-build-$rid-$L.log 2>&1); echo "[$(date +%H:%M:%S)] review $rid run $L exit=$?" >> $M/timeline.log
done
for rid in android-x64 android-arm64; do
  echo "[$(date +%H:%M:%S)] DECLARED android host build $rid run $L (tools/build_android.py --skip-dependencies --jobs 4): budget 2400 s" >> $M/budgets.txt
  s=$(date +%s); timeout 2400 python3 tools/build_android.py --rid $rid --skip-dependencies --jobs 4 > $V/android-host-build-$rid-$L.log 2>&1; c=$?
  echo "[$(date +%H:%M:%S)] android host build $rid run $L exit=$c $(( $(date +%s)-s ))s" >> $M/timeline.log
done
echo "[$(date +%H:%M:%S)] android builds run $L DONE" >> $M/timeline.log
