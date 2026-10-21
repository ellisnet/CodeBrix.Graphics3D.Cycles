#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Phase E1: corpora sequentially on one device, then the Linux v1 triangle captures replayed there.
# Args: DEVICE LABEL corpus:budget:jobtimeout ...
cd "$(dirname "$0")"
dev=$1; label=$2; shift 2
for spec in "$@"; do IFS=: read c b t <<<"$spec"; python3 run_corpus.py $dev $c $label $b $t; done
R=../runs/v1-triangle/triangle_lighting/run
python3 replay_host_captures.py $dev linux-v1-$label 1500 600 tri0=$R/case-0 tri1=$R/case-1 tri2=$R/case-2 tri3=$R/case-3 tri4=$R/case-4
