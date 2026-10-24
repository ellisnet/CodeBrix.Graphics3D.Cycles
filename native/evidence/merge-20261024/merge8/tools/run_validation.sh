#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 8 part 1: no-op build proof (b2), then the Linux slots in parallel (A B C D E F N M) and the Android
# gate + host builds (--jobs 4). Waits only on its own children.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge8; V=$M/validation; T=$M/tools
$T/build_linux.sh b2 8; c=$?
n=$(grep -c 'Building\|Linking' $V/b2-build.log); echo "[$(date +%H:%M:%S)] b2 no-op proof: exit=$c compile/link lines=$n" >> $M/timeline.log
[ $c = 0 ] || exit 1
pids=()
for s in A B C D E F N M; do $T/slot8_$s.sh > $V/slot8_$s.outer.log 2>&1 & pids+=($!); done
$T/android_builds.sh 1 > $V/android_builds-1.outer.log 2>&1 & pids+=($!)
for p in "${pids[@]}"; do wait $p; done
echo "[$(date +%H:%M:%S)] run_validation ALL DONE" >> $M/timeline.log
