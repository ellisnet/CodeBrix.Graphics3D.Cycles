#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 2, run 4 (final tree): clean build, then Linux slots 1 and 2 in parallel with the Android builds.
M=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge2; cd $M
echo "[$(date +%H:%M:%S)] run 4 start (probe refresh + glass canonical-record region)" >> timeline.log
tools/build_all.sh || exit 1
tools/slot1.sh > validation/slot1.log 2>&1 &
tools/slot2.sh > validation/slot2.log 2>&1 &
(tools/android_builds.sh; tools/host_builds.sh) > validation/android-builds.log 2>&1 &
wait; echo "[$(date +%H:%M:%S)] run 4 Linux+Android DONE" >> timeline.log
