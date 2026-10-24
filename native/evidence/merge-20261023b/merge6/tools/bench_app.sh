#!/bin/bash
# Merge wave 6 part 2: managed bundle-mode run of the bench bundle (scene.json = copy of bench_01.json) on one device, under the lock.
cd "$(dirname "$0")"; dev=$1; M=$(realpath ..); R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; source ./lock6.sh
case $dev in hp) serial=192.168.86.225:5555; rid=android-x64;; samsung) serial=R52W207XQ1V; rid=android-arm64;; esac
lg(){ echo "[$(date +%H:%M:%S)] $dev $*" >> $M/timeline.log; }
acquire $dev "bench_app.sh $dev (bench bundle managed app)" || exit 2
echo "[$(date +%H:%M:%S)] DECLARED managed app bundle (bench, scene.json copy of bench_01.json) on $dev: budget 2700 s" >> $M/budgets.txt
lg "START app-bundle-bench"; cd $R; s=$(date +%s)
timeout 2700 python3 native/tools/android_app_gate.py --build-output $M/android-app/validation-m6 --output $M/android-app/bundle-bench-$dev --device $dev,$rid,$serial --mode bundle --bundle $M/bundles-p2/bench-device --reference-film $M/oracles/bench/oracle/assets/bench_01/cpu-x64-linux/film-0.f32 --result-timeout 2400 --uninstall > $M/device/app-bundle-bench-$dev.log 2>&1; c=$?
lg "app-bundle-bench END exit=$c $(( $(date +%s)-s ))s"; cd $M/tools
$A shell true 2>/dev/null; bash cleanup6.sh $dev $serial
release $dev
