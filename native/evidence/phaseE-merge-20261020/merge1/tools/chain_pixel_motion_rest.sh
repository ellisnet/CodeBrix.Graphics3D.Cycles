#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 1: the Pixel motion jobs not reached at 12:40 (runner passed '--arg --stationary'; fixed), single run.
cd "$(dirname "$0")"; until grep -q "pixel chain DONE" ../timeline.log; do sleep 15; done
ref=$(realpath ../reference/linux-x64)
python3 run_corpus.py pixel pixel-motion mw1b 9600 2400 --jobs object-stationary-images,deforming-moving-images,deforming-stationary-images,camera-static,camera-motion --replays perjob1 --reference $ref --device-scene --pipeline-budget-ms 20000
echo "[$(date +%H:%M:%S)] pixel motion-rest DONE" >> ../timeline.log
