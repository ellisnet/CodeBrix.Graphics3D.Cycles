#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Lane E4 (copy of merge wave 1 chain_full.sh + chain_hostcap.sh): Samsung full-fidelity chain on the final build.
# Device-scene reference flow (X1 policy) for every sampler/image gate; budgets declared by the runners before each run.
cd "$(dirname "$0")"; dev=samsung; ref=$(realpath ../reference/linux-x64); F="--reference $ref --device-scene"
run(){ python3 run_corpus.py $dev "$@" $F; }
run triangle final 2400 900
run emitter final 600 300
run precision final 900 600
run spec-refusal final 600 300
run regression final 1500 600
run transparent final 2400 900
run lighting final 2400 900 --jobs broad_lighting,feature_combination --replays perjob2
run motion final 2400 900 --jobs object-moving-images,deforming-moving-images,camera-static,camera-motion --replays perjob1
run terminator final 1800 900
run volume final 1800 600 --replays perjob1
V=$(realpath ../linux); T=$V/f2-triangle/out/triangle_lighting/run; S=$V/f2-shadow-terminator/out/shadow_terminator/run
python3 replay_host_captures.py $dev final-linux-triangle 3600 900 $(for c in 0 1 2 3 4 5; do echo tri$c=$T/case-$c; done)
python3 replay_host_captures.py $dev final-linux-terminator 3600 900 $(for c in 0 1 2 3 4 5 6 7 8 9; do echo term$c=$S/case-$c; done)
echo "[$(date +%H:%M:%S)] samsung chain DONE" >> ../handoff/timeline.log
