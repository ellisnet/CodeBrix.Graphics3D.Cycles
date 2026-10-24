#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 8 part 2: S20 re-runs of the corpora that ended early on two defects of MY run_corpus.py adaptation (list-guard TypeError after the job PASS in
# transparent_lighting and transparent_shadow; strict-UTF-8 read of the failure-path logcat in mnee). Same jobs, same references, fixed tool; the first folders are kept.
cd "$(dirname "$0")"; M=$(realpath ..); source ./lock8.sh
ref=$M/reference-1b/linux-x64; serial=RFCRB0MPHYH; A="/home/jeremy/Android/Sdk/platform-tools/adb -s $serial"
lg(){ echo "[$(date +%H:%M:%S)] s20 $*" >> $M/timeline.log; }
acquire s20 "chain8_s20_rerun.sh (re-runs after tool fixes)" || exit 2
echo "[$(date +%H:%M:%S)] DECLARED s20 staging push (stage.py push s20): budget 1200 s" >> $M/budgets.txt
timeout 1200 python3 stage.py push s20 >> $M/device/s20-chain.log 2>&1; c=$?; lg "staged exit=$c"
if [ $c -ne 0 ]; then lg "STAGING FAILED"; $A shell 'rm -rf /data/local/tmp/codebrix-m8-*'; release s20; exit 3; fi
one(){ c=$1; job=$2; label=$3; budget=$4; to=$5
  python3 run_corpus.py s20 $c $label $budget $to --reference $ref --device-scene --jobs $job >> $M/device/s20-chain.log 2>&1
  lg "corpus $c-$label ($job) exit=$? timeout=${to}s"; }
one transparent transparent_lighting s20-transparent_lighting-rerun 3000 900
one transparent transparent_shadow s20-transparent_shadow-rerun 1200 600
one mnee mnee s20-mnee-rerun 1500 900
bash cleanup8.sh s20 $serial
release s20
