#!/bin/bash
# Lane Q: Android arm64 gate build (q/build-android-arm64) + review, then maintained Android host builds (both RIDs, needed by the APK) and the APK.
R=/home/jeremy/ClaudeHome/cycles-worktrees/q-s20-sweep; Q=$R/native/work/q; cd $Q/tools
K="triangle_sample surface triangle_lighting emitter_sample glass_render glass_bounce lighting_mis lighting light_state broad_light_state broad_lighting transparent_lighting transparent_shadow feature_combination object_motion_render deforming_motion_render camera render volume volume_render volume_boundary closed_volume_render closest shadow shader_data shadow_terminator principled_svm principled_bounce principled_render nodes_svm nodes_world nodes_render"
echo "[$(date +%H:%M:%S)] DECLARED android gate build android-arm64 (--jobs 6) timeout=3600s" >> $Q/budgets.txt
s=$(date +%s); timeout 3600 python3 build_android_q.py android-arm64 $K > $Q/logs/android-gate-build-arm64.log 2>&1; c=$?
echo "[$(date +%H:%M:%S)] android gate build arm64 exit=$c $(( $(date +%s)-s ))s" >> $Q/timeline.log; [ $c = 0 ] || exit $c
python3 stage.py review android-arm64 >> $Q/logs/android-gate-build-arm64.log 2>&1; c=$?; echo "[$(date +%H:%M:%S)] review arm64 exit=$c" >> $Q/timeline.log; [ $c = 0 ] || exit $c
cd $R
for rid in android-arm64 android-x64; do
  echo "[$(date +%H:%M:%S)] DECLARED android host build $rid (tools/build_android.py --skip-dependencies --jobs 6) timeout=2400s" >> $Q/budgets.txt
  s=$(date +%s); timeout 2400 python3 tools/build_android.py --rid $rid --skip-dependencies --jobs 6 > $Q/logs/android-host-build-$rid.log 2>&1; c=$?
  echo "[$(date +%H:%M:%S)] android host build $rid exit=$c $(( $(date +%s)-s ))s" >> $Q/timeline.log; [ $c = 0 ] || exit 1
done
