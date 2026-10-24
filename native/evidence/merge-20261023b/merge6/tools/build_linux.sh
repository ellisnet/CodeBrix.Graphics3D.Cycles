#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 6: incremental Linux build of every codebrix target in main's own tree (native/work/cycles-build).
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge6; V=$M/validation; cd $R
L=${1:-b1}; J=${2:-8}
T=$(cmake --build native/work/cycles-build --target help | grep -o 'codebrix_[a-z0-9_]*' | sort -u | tr '\n' ' ')
echo "$T" > $V/$L-targets.txt
echo "[$(date +%H:%M:%S)] DECLARED $L Linux build (all codebrix targets, incremental, --jobs $J) budget=2400s" >> $M/budgets.txt
s=$(date +%s); timeout 2400 python3 native/tools/build_cycles.py --jobs $J $(for t in $T; do echo --target $t; done) > $V/$L-build.log 2>&1; c=$?
echo "[$(date +%H:%M:%S)] $L Linux build exit=$c $(( $(date +%s)-s ))s" >> $M/timeline.log
