#!/bin/bash
# Lane Q: validation APK for the managed WebGPU app gate (needs both maintained Android host builds).
R=/home/jeremy/ClaudeHome/cycles-worktrees/q-s20-sweep; Q=$R/native/work/q; G=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge3; cd $R
echo "[$(date +%H:%M:%S)] DECLARED validation APK build (build_android_app.py, application id com.codebrix.cycles.hostvalidation.q) timeout=1200s" >> $Q/budgets.txt
s=$(date +%s); timeout 1200 python3 native/tools/build_android_app.py --output $Q/android-app/validation-q --references $G/validation/l1-host-gate/out --application-id com.codebrix.cycles.hostvalidation.q > $Q/logs/apk-build.log 2>&1; c=$?
echo "[$(date +%H:%M:%S)] APK build exit=$c $(( $(date +%s)-s ))s" >> $Q/timeline.log
