#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Phase E5: Pixel 4 XL final confirmation runs (one per corpus), device-scene glibc reference flow,
# 20 s per-pipeline compile budget (warnings only), 2400 s per gate.
cd "$(dirname "$0")"; export E5_BUILD=after; F="--device-scene --pipeline-budget-ms 20000"
E=$(realpath ..)
run(){ echo "[$(date +%H:%M:%S)] DECLARED pixel corpus $1 ($2): wall budget $3 s, 2400 s per gate" >> $E/budgets.txt
       s=$(date +%s); python3 run_corpus.py pixel $1 $2 $3 2400 "${@:4}" $F > $E/device/chain-$1-$2.log 2>&1; c=$?
       echo "[$(date +%H:%M:%S)] pixel corpus $1 $2 exit=$c $(( $(date +%s)-s ))s" >> $E/timeline.log; }
run triangle e5final 7200 --replays perjob1
run emitter e5final 1200
run pixel-lighting e5final 2400 --jobs feature_combination --replays perjob1
run pixel-motion e5final 2400 --jobs object-moving-images --replays perjob1
echo "[$(date +%H:%M:%S)] pixel chain DONE" >> $E/timeline.log
