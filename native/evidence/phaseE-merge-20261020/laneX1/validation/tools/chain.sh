#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Phase X1: corpora sequentially on one device with the Linux glibc reference imported, then the Linux triangle
# captures (from the reference export run) replayed there. Args: DEVICE LABEL REFERENCE corpus:budget:jobtimeout ...
# X1_FLAGS (environment): extra run_corpus flags, e.g. --device-scene for the authoritative device-scene flow.
cd "$(dirname "$0")"
dev=$1; label=$2; ref=$3; shift 3
for spec in "$@"; do IFS=: read c b t <<<"$spec"; python3 run_corpus.py $dev $c $label $b $t --reference $ref $X1_FLAGS; done
R=$ref/runs/triangle_lighting/run
python3 replay_host_captures.py $dev linux-$label 1500 600 tri0=$R/case-0 tri1=$R/case-1 tri2=$R/case-2 tri3=$R/case-3 tri4=$R/case-4
