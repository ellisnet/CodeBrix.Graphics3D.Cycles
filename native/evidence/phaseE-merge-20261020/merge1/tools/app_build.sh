#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 1: Android host builds (maintained location) and the validation APK for the managed WebGPU app gate.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge1; cd $R
for rid in android-arm64 android-x64; do
  echo "[$(date +%H:%M:%S)] DECLARED android host build $rid (tools/build_android.py --skip-dependencies --jobs 6): budget 2400 s" >> $M/budgets.txt
  s=$(date +%s); timeout 2400 python3 tools/build_android.py --rid $rid --skip-dependencies --jobs 6 > $M/validation/android-host-build-$rid.log 2>&1; c=$?
  echo "[$(date +%H:%M:%S)] android host build $rid exit=$c $(( $(date +%s)-s ))s" >> $M/timeline.log; [ $c = 0 ] || exit 1
done
echo "[$(date +%H:%M:%S)] DECLARED validation APK build (build_android_app.py, application id com.codebrix.cycles.hostvalidation.mw1): budget 1200 s" >> $M/budgets.txt
s=$(date +%s); timeout 1200 python3 native/tools/build_android_app.py --output $M/android-app/validation-mw1 --references $M/validation/l1-host-gate/out --application-id com.codebrix.cycles.hostvalidation.mw1 > $M/validation/apk-build.log 2>&1; c=$?
echo "[$(date +%H:%M:%S)] APK build exit=$c $(( $(date +%s)-s ))s" >> $M/timeline.log
