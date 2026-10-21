#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 1: replay the merged-tree Linux x64 captures on a device (triangle modes 0-5, E3's 10 shadow-terminator cases).
# Usage: chain_hostcap.sh DEVICE WAIT_MARKER   (waits until WAIT_MARKER appears in the timeline; device runs never overlap)
cd "$(dirname "$0")"; dev=$1; marker=$2
until grep -q "$marker" ../timeline.log; do sleep 15; done
V=$(realpath ../validation); T=$V/l2-triangle/out/triangle_lighting/run; S=$V/l2-shadow-terminator/out/shadow_terminator/run
python3 replay_host_captures.py $dev linux-triangle 3600 900 $(for c in 0 1 2 3 4 5; do echo tri$c=$T/case-$c; done)
python3 replay_host_captures.py $dev linux-terminator 3600 900 $(for c in 0 1 2 3 4 5 6 7 8 9; do echo term$c=$S/case-$c; done)
echo "[$(date +%H:%M:%S)] $dev hostcap DONE" >> ../timeline.log
