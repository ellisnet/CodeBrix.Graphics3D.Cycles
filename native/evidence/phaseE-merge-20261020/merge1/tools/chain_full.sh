#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 1: full-fidelity device chain (samsung | hp). Device-scene reference flow (X1 policy) for every
# sampler/image gate; budgets are declared by run_corpus.py / replay_host_captures.py before each run.
cd "$(dirname "$0")"; dev=$1; ref=$(realpath ../reference/linux-x64); F="--reference $ref --device-scene"
run(){ python3 run_corpus.py $dev "$@" $F; }
run spec-refusal mw1 600 300
run emitter mw1 600 300
run triangle mw1 2400 900
run precision mw1 900 600
run terminator mw1 1800 900
run regression mw1 1500 600
run transparent mw1 2400 900
run lighting mw1 2400 900 --jobs broad_lighting,feature_combination --replays perjob2
run volume mw1 1800 600 --replays perjob1
run motion mw1 2400 900 --jobs object-moving-images,deforming-moving-images,camera-static,camera-motion --replays perjob1
echo "[$(date +%H:%M:%S)] $dev chain_full DONE" >> ../timeline.log
