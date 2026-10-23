#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 4: start Linux slots A and B once the v2 target build succeeded; then the validation APK once the host gate
# and both Android host builds succeeded.
M=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge4; R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles
until grep -q "v2 gate build exit=" $M/timeline.log; do sleep 10; done
grep -q "v2 gate build exit=0" $M/timeline.log || { echo "[$(date +%H:%M:%S)] slots NOT started (v2 build failed)" >> $M/timeline.log; exit 1; }
bash $M/tools/slotA.sh > $M/validation/slotA.log 2>&1 &
bash $M/tools/slotB.sh > $M/validation/slotB.log 2>&1 &
until grep -q "END l1-host-gate exit=" $M/timeline.log && grep -q "android host build android-arm64 exit=" $M/timeline.log; do sleep 15; done
if grep -q "END l1-host-gate exit=0" $M/timeline.log && grep -q "android host build android-arm64 exit=0" $M/timeline.log && grep -q "android host build android-x64 exit=0" $M/timeline.log; then
  echo "[$(date +%H:%M:%S)] DECLARED validation APK build (build_android_app.py, application id com.codebrix.cycles.hostvalidation.m4): budget 1200 s" >> $M/budgets.txt
  cd $R; s=$(date +%s); timeout 1200 python3 native/tools/build_android_app.py --output $M/android-app/validation-m4 --references $M/validation/l1-host-gate/out --application-id com.codebrix.cycles.hostvalidation.m4 > $M/validation/apk-build.log 2>&1; c=$?
  echo "[$(date +%H:%M:%S)] APK build exit=$c $(( $(date +%s)-s ))s" >> $M/timeline.log
else echo "[$(date +%H:%M:%S)] APK build exit=9 (prerequisite failed)" >> $M/timeline.log; fi
wait
