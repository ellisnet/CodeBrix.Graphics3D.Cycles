#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# usage: run_gates.sh OUTDIR BINDIR SPEC(0|1|default) gate...   (cold shader cache per gate)
out=$1; bin=$2; spec=$3; shift 3; mkdir -p "$out"
for g in "$@"; do
  d="$out/$g"; mkdir -p "$d"; cache="$d/mesa-cache"; rm -rf "$cache"; mkdir -p "$cache"
  start=$(date +%s.%N)
  if [ "$spec" = default ]; then
    MESA_SHADER_CACHE_DIR="$cache" CODEBRIX_CYCLES_PIPELINE_LOG="$d/pipelines.jsonl" timeout 900 "$bin/codebrix_cycles_${g}_gate" "$d/run" > "$d/stdout.txt" 2> "$d/stderr.txt"
  else
    MESA_SHADER_CACHE_DIR="$cache" CODEBRIX_CYCLES_SHADER_SPECIALIZATION=$spec CODEBRIX_CYCLES_PIPELINE_LOG="$d/pipelines.jsonl" timeout 900 "$bin/codebrix_cycles_${g}_gate" "$d/run" > "$d/stdout.txt" 2> "$d/stderr.txt"
  fi
  rc=$?; end=$(date +%s.%N); rm -rf "$cache"
  echo "[$(date +%H:%M:%S)] $g rc=$rc seconds=$(echo "$end-$start"|bc)" | tee -a "$out/summary.txt"
done
