#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Lane E4: the five Samsung corpora that the final-2 chain could not start (stale remote dirs), label final3; waits for chain 1.
cd "$(dirname "$0")"; true
dev=samsung; ref=$(realpath ../reference/linux-x64); F="--reference $ref --device-scene"
run(){ python3 run_corpus.py $dev "$@" $F; }
run triangle final3 2400 900
run regression final3 1500 600
run emitter final3 600 300
run precision final3 900 600
run spec-refusal final3 600 300
echo "[$(date +%H:%M:%S)] samsung chain3 DONE" >> ../handoff/timeline.log
