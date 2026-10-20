#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
cd /home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles
O=native/work/phaseC4/02-linux-build
args=(); while read t; do args+=(--target "$t"); done < $O/targets.txt
t0=$(date +%s)
timeout --signal=TERM --kill-after=30 900 python3 native/tools/build_cycles.py --jobs 8 "${args[@]}" > $O/build-incremental.stdout.txt 2> $O/build-incremental.stderr.txt
rc=$?; echo "rc=$rc seconds=$(( $(date +%s)-t0 ))" > $O/result-incremental.txt
