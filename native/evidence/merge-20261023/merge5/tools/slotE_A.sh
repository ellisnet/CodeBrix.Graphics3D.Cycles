#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 5 part 2, Linux stream A (labels e-*): all-lanes, host gate, dotnet TEST only (no build/pack: nothing is built here).
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge5; V=$M/validation; S=$M/tools/slot.sh
cd $R
$S e-all-lanes 2700 python3 native/evidence/phaseC-closeout-20261020/closeout/tools/linux_validate.py --tree $R --output $V/e-all-lanes/out --skip-build
$S e-host-gate 1500 python3 native/tools/host_gate.py --output $V/e-host-gate/out
CODEBRIX_CYCLES_REFERENCE_EXPORT=$M/reference-e/managed-linux-x64 $S e-dotnet-test 1500 dotnet test --solution CodeBrix.Graphics3D.Cycles.slnx --configuration Release --no-build
$S e-dotnet-test-debug 1500 dotnet test --solution CodeBrix.Graphics3D.Cycles.slnx --configuration Debug --no-build
echo "[$(date +%H:%M:%S)] slotE_A DONE" >> $M/timeline.log
