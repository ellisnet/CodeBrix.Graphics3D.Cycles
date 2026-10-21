#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 1: Pixel 4 XL closest/shadow precision fixtures (X2 device list), after the Pixel app run. Single run.
cd "$(dirname "$0")"; until grep -q "pixel app-webgpu END" ../timeline.log; do sleep 15; done
python3 run_corpus.py pixel precision mw1 4800 2400 --pipeline-budget-ms 20000
echo "[$(date +%H:%M:%S)] pixel precision DONE" >> ../timeline.log
