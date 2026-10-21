#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 1: Pixel 4 XL (old-hardware gauge). Single confirmation run per corpus, 2400 s per gate,
# 20 s per-pipeline compile budget (warnings only). Device-scene reference flow.
cd "$(dirname "$0")"; ref=$(realpath ../reference/linux-x64); F="--reference $ref --device-scene --pipeline-budget-ms 20000"
run(){ python3 run_corpus.py pixel "$@" $F; }
run pixel-lighting mw1 4800 2400 --replays perjob1
run pixel-motion mw1 9600 2400 --replays perjob1
run pixel-volume mw1 4800 2400 --replays perjob1
run triangle mw1 7200 2400 --replays perjob1
echo "[$(date +%H:%M:%S)] pixel chain DONE" >> ../timeline.log
