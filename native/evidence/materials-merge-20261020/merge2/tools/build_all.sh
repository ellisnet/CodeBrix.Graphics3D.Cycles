#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 2: clean native build (linux-x64 package + default gates), then every codebrix gate/host target.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge2; V=$M/validation
cd $R
echo "[$(date +%H:%M:%S)] DECLARED v1-native-build (clean: removed native/work/cycles-build) budget=3600s" >> $M/budgets.txt
rm -rf $R/native/work/cycles-build
s=$(date +%s); timeout 3600 python3 tools/build.py --jobs 6 --skip-dependencies > $V/v1-native-build.log 2>&1; c=$?
echo "[$(date +%H:%M:%S)] v1 native build exit=$c $(( $(date +%s)-s ))s" >> $M/timeline.log; [ $c = 0 ] || exit $c
T=$(cmake --build native/work/cycles-build --target help | grep -o 'codebrix_[a-z0-9_]*' | sort -u | tr '\n' ' ')
echo "$T" > $V/v2-targets.txt
echo "[$(date +%H:%M:%S)] DECLARED v2 gate build (all codebrix targets, --jobs 6) budget=2400s" >> $M/budgets.txt
s=$(date +%s); timeout 2400 python3 native/tools/build_cycles.py --jobs 6 $(for t in $T; do echo --target $t; done) > $V/v2-gate-build.log 2>&1; c=$?
echo "[$(date +%H:%M:%S)] v2 gate build exit=$c $(( $(date +%s)-s ))s" >> $M/timeline.log; exit $c
