#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 2: Android host builds (maintained location) and the validation APK for the managed WebGPU app gate.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge2; cd $R
echo "[$(date +%H:%M:%S)] DECLARED validation APK build (build_android_app.py, application id com.codebrix.cycles.hostvalidation.mw2): budget 1200 s" >> $M/budgets.txt
s=$(date +%s); timeout 1200 python3 native/tools/build_android_app.py --output $M/android-app/validation-mw2 --references $M/validation/l1-host-gate/out --application-id com.codebrix.cycles.hostvalidation.mw2 > $M/validation/apk-build.log 2>&1; c=$?
echo "[$(date +%H:%M:%S)] APK build exit=$c $(( $(date +%s)-s ))s" >> $M/timeline.log
