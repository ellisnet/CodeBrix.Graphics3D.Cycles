#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 6, Linux slot A: all-lanes, host gate, .NET Release and Debug build/test/pack (managed Linux reference films
# from the Release test run, as merge wave 4).
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge8; V=$M/validation; S=$M/tools/slot.sh
cd $R
$S a-dotnet-build 900 dotnet build --configuration Release CodeBrix.Graphics3D.Cycles.slnx
CODEBRIX_CYCLES_REFERENCE_EXPORT=$M/reference/managed-linux-x64 $S a-dotnet-test 1500 dotnet test --solution CodeBrix.Graphics3D.Cycles.slnx --configuration Release --no-build
$S a-dotnet-pack 600 dotnet pack --configuration Release --no-build CodeBrix.Graphics3D.Cycles.slnx
$S a-dotnet-build-debug 900 dotnet build --configuration Debug CodeBrix.Graphics3D.Cycles.slnx
$S a-dotnet-test-debug 1500 dotnet test --solution CodeBrix.Graphics3D.Cycles.slnx --configuration Debug --no-build
$S a-dotnet-pack-debug 600 dotnet pack --configuration Debug --no-build CodeBrix.Graphics3D.Cycles.slnx
$S a-all-lanes 2700 python3 native/evidence/phaseC-closeout-20261020/closeout/tools/linux_validate.py --tree $R --output $V/a-all-lanes/out --skip-build
$S a-host-gate 1500 python3 native/tools/host_gate.py --output $V/a-host-gate/out
echo "[$(date +%H:%M:%S)] slotA DONE" >> $M/timeline.log
