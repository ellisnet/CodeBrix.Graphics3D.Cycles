#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 5: the validation APK (as merge wave 4's launch_linux.sh) once the Linux host gate and both Android host
# builds succeeded. No device is touched.
M=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge5; R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles
until grep -q "END a-host-gate exit=" $M/timeline.log; do sleep 15; done
if grep -q "END a-host-gate exit=0" $M/timeline.log && grep -q "android host build android-arm64 exit=0" $M/timeline.log && grep -q "android host build android-x64 exit=0" $M/timeline.log; then
  echo "[$(date +%H:%M:%S)] DECLARED validation APK build (build_android_app.py, application id com.codebrix.cycles.hostvalidation.m5): budget 1200 s" >> $M/budgets.txt
  cd $R; s=$(date +%s); timeout 1200 python3 native/tools/build_android_app.py --output $M/android-app/validation-m5 --references $M/validation/a-host-gate/out --application-id com.codebrix.cycles.hostvalidation.m5 > $M/validation/apk-build.log 2>&1; c=$?
  echo "[$(date +%H:%M:%S)] APK build exit=$c $(( $(date +%s)-s ))s" >> $M/timeline.log
else echo "[$(date +%H:%M:%S)] APK build exit=9 (prerequisite failed)" >> $M/timeline.log; fi
