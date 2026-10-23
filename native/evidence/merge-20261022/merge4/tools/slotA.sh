#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 4, Linux slot A (sequential): all-lanes, host gate, .NET Release and Debug build/test/pack (+ managed Linux
# reference films from the Release test run).
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge4; V=$M/validation; S=$M/tools/slot.sh
cd $R
$S l1-all-lanes 2700 python3 native/evidence/phaseC-closeout-20261020/closeout/tools/linux_validate.py --tree $R --output $V/l1-all-lanes/out --skip-build
$S l1-host-gate 1500 python3 native/tools/host_gate.py --output $V/l1-host-gate/out
$S l1-dotnet-build 900 dotnet build --configuration Release CodeBrix.Graphics3D.Cycles.slnx
CODEBRIX_CYCLES_REFERENCE_EXPORT=$M/reference/managed-linux-x64 $S l1-dotnet-test 1500 dotnet test --solution CodeBrix.Graphics3D.Cycles.slnx --configuration Release --no-build
$S l1-dotnet-pack 600 dotnet pack --configuration Release --no-build CodeBrix.Graphics3D.Cycles.slnx
$S l1-dotnet-build-debug 900 dotnet build --configuration Debug CodeBrix.Graphics3D.Cycles.slnx
$S l1-dotnet-test-debug 1500 dotnet test --solution CodeBrix.Graphics3D.Cycles.slnx --configuration Debug --no-build
$S l1-dotnet-pack-debug 600 dotnet pack --configuration Debug --no-build CodeBrix.Graphics3D.Cycles.slnx
echo "[$(date +%H:%M:%S)] slotA DONE" >> $M/timeline.log
