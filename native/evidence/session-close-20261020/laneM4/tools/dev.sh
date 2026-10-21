#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Usage: dev.sh NAME GATE...: rebuild the gate targets, then run each (spec on) under native/work/m4/NAME.
set -u
root=/home/jeremy/ClaudeHome/cycles-worktrees/m4-bump-derivative; name=$1; shift
targets=(); for g in "$@"; do targets+=(--target codebrix_cycles_${g}_gate); done
python3 $root/native/tools/build_cycles.py --jobs 6 "${targets[@]}" > $root/native/work/m4/logs/build-$name.log 2>&1 || { echo build failed; grep -n "error" $root/native/work/m4/logs/build-$name.log | head; exit 1; }
for g in "$@"; do rm -rf $root/native/work/m4/$name/$g; $root/native/work/m4/tools/run_gate.sh $name-$g $root/native/work/m4/$name/$g 900 codebrix_cycles_${g}_gate; done
