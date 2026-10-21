#!/bin/bash
# Usage: run_gate.sh NAME OUTDIR BUDGET_SECONDS GATE_BINARY_BASENAME [extra args...]; env passes through.
set -u
name=$1; out=$2; budget=$3; bin=$4; shift 4
root=/home/jeremy/ClaudeHome/cycles-worktrees/m4-bump-derivative
mkdir -p "$out"
export MESA_SHADER_CACHE_DIR=$root/native/work/m4/mesa-$name
rm -rf "$MESA_SHADER_CACHE_DIR"; mkdir -p "$MESA_SHADER_CACHE_DIR"
export CODEBRIX_CYCLES_PIPELINE_LOG=$out/pipelines.jsonl
start=$(date +%s)
timeout "$budget" "${BIN_DIR:-$root/native/work/cycles-build/bin}/$bin" "$out/run" "$@" > "$out/stdout.txt" 2> "$out/stderr.txt"
code=$?
echo "{\"name\":\"$name\",\"exit\":$code,\"seconds\":$(( $(date +%s)-start )),\"budget\":$budget}" > "$out/exit.json"
cat "$out/exit.json"
