#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# docs/TESTING-MACOS-ARM64.md step 7, exactly: restore, Release build, Release test, pack, Debug build, Debug test.
# Each command runs through run_step.sh (start, end, wall, exit, output). A failure is recorded and the sequence continues.
cd /Volumes/VideoWork/GitHome/CodeBrix.Graphics3D.Cycles || exit 99
R=native/work/macos-arm64-closing-20261115
dotnet --version > $R/step7-dotnet-version.txt
ls nugets/Release > $R/step7-nugets-before.txt 2>&1
$R/run_step.sh step7-1-restore dotnet restore CodeBrix.Graphics3D.Cycles.slnx
$R/run_step.sh step7-2-build-release dotnet build --configuration Release CodeBrix.Graphics3D.Cycles.slnx
$R/run_step.sh step7-3-test-release dotnet test --solution CodeBrix.Graphics3D.Cycles.slnx --configuration Release --no-build
$R/run_step.sh step7-4-pack-release dotnet pack --configuration Release --no-build CodeBrix.Graphics3D.Cycles.slnx
$R/run_step.sh step7-5-build-debug dotnet build --configuration Debug CodeBrix.Graphics3D.Cycles.slnx
$R/run_step.sh step7-6-test-debug dotnet test --solution CodeBrix.Graphics3D.Cycles.slnx --configuration Debug --no-build
ls nugets/Release > $R/step7-nugets-after.txt 2>&1
git status --porcelain > $R/step7-git-status-after.txt
