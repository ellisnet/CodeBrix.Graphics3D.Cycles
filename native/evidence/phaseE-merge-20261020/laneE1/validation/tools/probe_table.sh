#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Phase E1: cold compile table, one pipeline per process (tools/probe.py), sequential on one device.
# Args: DEVICE TIMEOUT_S  (base shade_surface_light mode-5 / base probe are expected to crash on the HP)
cd "$(dirname "$0")/.."
dev=$1; t=$2
for spec in "v1-mode5-surface shade_surface_light" "v1-mode5-surface shade_surface_light" "v1-mode5-surface connect_surface_light" "v1-mode5-surface prepare_surface" \
            "base-mode5-surface shade_surface_light" "base-mode5-surface connect_surface_light" "base-mode5-surface prepare_surface" \
            "v1-mode1-surface shade_surface_light" "base-mode1-surface shade_surface_light" \
            "v1-probe emitter_sample_probe" "v1-probe emitter_sample_probe" "base-probe emitter_sample_probe"; do
  set -- $spec
  python3 tools/probe.py run --device $dev --wgsl probe-inputs/real/$1.wgsl --entry $2 --timeout $t --tag real-$1 --log logs/probe-table-$dev.jsonl > /dev/null 2>&1
  echo "[$(date +%H:%M:%S)] $dev $1 $2 done"
done
