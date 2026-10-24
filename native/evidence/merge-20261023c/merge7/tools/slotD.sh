#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 7: gates new in this wave (environment gate, camera gate with per-field ray bit differences) + envelope of the environment entries.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge7; V=$M/validation; S=$M/tools/slot.sh
T=$R/native/tools; B=$R/native/work/cycles-build/bin
cd $R; mkdir -p $V/d-environment-dump
CODEBRIX_CYCLES_WGSL_DUMP=$V/d-environment-dump $S d-environment-gate 1200 $B/codebrix_cycles_environment_gate $V/d-environment-gate/out
mkdir -p $V/d-camera-gate $V/d-camera-motion
$S d-camera-gate 900 $B/codebrix_cycles_camera_gate $V/d-camera-gate/run
$S d-camera-motion 900 $B/codebrix_cycles_camera_gate $V/d-camera-motion/run --motion
$S d-envelope-environment 3600 python3 $T/intel_compile_envelope.py --dump $V/d-environment-dump --debug-file-directory $R/native/work/merge4/debug --output $V/d-envelope-environment/out
echo "[$(date +%H:%M:%S)] slotD DONE" >> $M/timeline.log
