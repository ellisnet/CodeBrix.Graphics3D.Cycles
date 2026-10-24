#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 7 part 2: re-run of the host-capture replays. The first attempt (inside chain7_device.sh batch B) failed in ~22 s on both devices because
# replay_host_captures.py still named the wave-6 stage folder (codebrix-m6-stage); the tool is fixed (m7), the failed run folders were renamed
# runs/<dev>/hostcap-*-failed-m6path, the stage is pushed again (hash-verified), the replays run, everything is cleaned.
cd "$(dirname "$0")"; dev=$1; M=$(realpath ..); V=$M/validation; source ./lock7.sh
case $dev in hp) serial=192.168.86.225:5555;; samsung) serial=R52W207XQ1V;; esac
A="/home/jeremy/Android/Sdk/platform-tools/adb -s $serial"; LOG=$M/device/$dev-chain.log
lg(){ echo "[$(date +%H:%M:%S)] $dev $*" >> $M/timeline.log; }
T=$V/c-triangle/out/triangle_lighting/run; S=$V/c-shadow-terminator/out/shadow_terminator/run
acquire $dev "chain7_hostcap.sh $dev (re-stage, host-capture replays, cleanup)" || exit 2
echo "[$(date +%H:%M:%S)] DECLARED $dev staging push for host-capture re-run (stage.py push $dev): budget 1200 s" >> $M/budgets.txt
timeout 1200 python3 stage.py push $dev >> $LOG 2>&1; c=$?; lg "re-staged exit=$c"
if [ $c -ne 0 ]; then lg "STAGING FAILED: hostcap not run"; $A shell 'rm -rf /data/local/tmp/codebrix-m7-*'; release $dev; exit 3; fi
python3 replay_host_captures.py $dev linux-triangle 3600 900 $(for c in 0 1 2 3 4 5; do echo tri$c=$T/case-$c; done) >> $LOG 2>&1; lg "hostcap triangle exit=$?"
python3 replay_host_captures.py $dev linux-terminator 3600 900 $(for c in 0 1 2 3 4 5 6 7 8 9; do echo term$c=$S/case-$c; done) >> $LOG 2>&1; lg "hostcap terminator exit=$?"
bash cleanup7.sh $dev $serial
release $dev
