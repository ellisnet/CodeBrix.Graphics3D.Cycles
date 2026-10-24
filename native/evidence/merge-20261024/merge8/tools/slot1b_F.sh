#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 8 (from merge wave 7 part 2) part 2, Linux stream F: environment gate, camera gate (static + motion), environment envelope, ledger gate for environment-light.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge8; V=$M/validation; S=$M/tools/slot.sh
T=$R/native/tools; B=$R/native/work/cycles-build/bin
cd $R; mkdir -p $V/b8-environment-dump $V/b8-camera-gate $V/b8-camera-motion
CODEBRIX_CYCLES_WGSL_DUMP=$V/b8-environment-dump $S b8-environment-gate 1200 $B/codebrix_cycles_environment_gate $V/b8-environment-gate/out
$S b8-camera-gate 900 $B/codebrix_cycles_camera_gate $V/b8-camera-gate/run
$S b8-camera-motion 900 $B/codebrix_cycles_camera_gate $V/b8-camera-motion/run --motion
$S b8-envelope-environment 3600 python3 $T/intel_compile_envelope.py --dump $V/b8-environment-dump --debug-file-directory $R/native/work/merge4/debug --output $V/b8-envelope-environment/out
echo "[$(date +%H:%M:%S)] slot1b_F DONE" >> $M/timeline.log
