#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 8 (copied from merge wave 7): validation APK (bundle mode included) from this tree, application id com.codebrix.cycles.hostvalidation.m8.
# usage: build_apk_m7.sh HOST_GATE_OUT   (a host_gate.py output whose status.json says passed)
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge8; V=$M/validation; cd $R
REFS=$1; [ -f "$REFS/status.json" ] || { echo "no host gate output $REFS"; exit 2; }
echo "[$(date +%H:%M:%S)] DECLARED validation APK build (build_android_app.py, application id com.codebrix.cycles.hostvalidation.m8, references $REFS): budget 1200 s" >> $M/budgets.txt
s=$(date +%s); timeout 1200 python3 native/tools/build_android_app.py --output $M/android-app/${2:-validation-m8} --references "$REFS" --application-id com.codebrix.cycles.hostvalidation.m8 > $V/apk-build.log 2>&1; c=$?
echo "[$(date +%H:%M:%S)] APK build exit=$c $(( $(date +%s)-s ))s" >> $M/timeline.log; exit $c
