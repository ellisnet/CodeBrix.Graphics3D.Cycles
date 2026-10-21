#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 3: managed WebGPU app (D5 scenes, E3 default shadow-terminator offsets) on one device, judged against the
# Linux glibc films (X1 --linux-reference); waits for WAIT_MARKER. Usage: chain_app.sh LABEL RID SERIAL WAIT_MARKER
cd "$(dirname "$0")"; label=$1; rid=$2; serial=$3; marker=$4; R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge3
w=0; until grep -q "$marker" $M/timeline.log; do grep -q "APK build exit=[1-9]" $M/timeline.log && { echo "[$(date +%H:%M:%S)] $label app SKIPPED (APK build failed)" >> $M/timeline.log; exit 1; }; w=$((w+15)); [ $w -gt 10800 ] && { echo "[$(date +%H:%M:%S)] $label app SKIPPED (no APK after 3 h)" >> $M/timeline.log; exit 1; }; sleep 15; done
echo "[$(date +%H:%M:%S)] DECLARED managed app WebGPU mode on $label (--linux-reference, result timeout 2400 s, --uninstall): budget 2700 s" >> $M/budgets.txt
echo "[$(date +%H:%M:%S)] START $label app-webgpu" >> $M/timeline.log
cd $R; s=$(date +%s)
timeout 2700 python3 native/tools/android_app_gate.py --build-output $M/android-app/validation-mw3 --output $M/android-app/webgpu-$label --device $label,$rid,$serial --mode webgpu --linux-reference $M/reference/managed-linux-x64 --result-timeout 2400 --uninstall > $M/device/app-$label.log 2>&1; c=$?
echo "[$(date +%H:%M:%S)] $label app-webgpu END exit=$c $(( $(date +%s)-s ))s" >> $M/timeline.log
