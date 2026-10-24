#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 8 part 1b (after m2-mnee): b3 reconfigure + defaults, b4 every codebrix target, b5 no-op proof, replay
# rebuild, then every Linux slot again (labels b8-*) and the Android gate + host builds.
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge8; V=$M/validation; T=$M/tools
cp $V/b0-build.log $V/b0-1a-build.log; $T/build_linux.sh b0 8 && cp $V/b0-build.log $V/b3-build.log && $T/build_linux.sh b4 8 && $T/build_linux.sh b5 8 || exit 1
n=$(grep -c 'Building\|Linking' $V/b5-build.log); echo "[$(date +%H:%M:%S)] b5 no-op proof (1b): compile/link lines=$n" >> $M/timeline.log
python3 $T/build_replay.py > $V/replay-build-1b.log 2>&1; echo "[$(date +%H:%M:%S)] render_replay rebuilt (1b) exit=$?" >> $M/timeline.log
S=$T/slot.sh; cd $R
$S b8-dotnet-build 900 dotnet build --configuration Release CodeBrix.Graphics3D.Cycles.slnx
$S b8-dotnet-build-debug 900 dotnet build --configuration Debug CodeBrix.Graphics3D.Cycles.slnx
pids=()
for s in A B C D E F N M R; do $T/slot1b_$s.sh > $V/slot1b_$s.outer.log 2>&1 & pids+=($!); done
$T/android_builds.sh 2 > $V/android_builds-2.outer.log 2>&1 & pids+=($!)
for p in "${pids[@]}"; do wait $p; done
echo "[$(date +%H:%M:%S)] run_validation_1b ALL DONE" >> $M/timeline.log
