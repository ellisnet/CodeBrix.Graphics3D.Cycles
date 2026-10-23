#!/bin/bash
# Lane Q: Linux x64 host build (needed for the device-scene glibc references). Copy of merge3 build_all.sh for this worktree.
R=/home/jeremy/ClaudeHome/cycles-worktrees/q-s20-sweep; Q=$R/native/work/q; cd $R
echo "[$(date +%H:%M:%S)] DECLARED linux native build (tools/build.py --jobs 6 --skip-dependencies) timeout=3600s" >> $Q/budgets.txt
s=$(date +%s); timeout 3600 python3 tools/build.py --jobs 6 --skip-dependencies > $Q/logs/linux-native-build.log 2>&1; c=$?
echo "[$(date +%H:%M:%S)] linux native build exit=$c $(( $(date +%s)-s ))s" >> $Q/timeline.log; [ $c = 0 ] || exit $c
T=$(cmake --build native/work/cycles-build --target help | grep -o 'codebrix_[a-z0-9_]*' | sort -u | tr '\n' ' ')
echo "[$(date +%H:%M:%S)] DECLARED linux gate build (all codebrix targets, --jobs 6) timeout=2400s" >> $Q/budgets.txt
s=$(date +%s); timeout 2400 python3 native/tools/build_cycles.py --jobs 6 $(for t in $T; do echo --target $t; done) > $Q/logs/linux-gate-build.log 2>&1; c=$?
echo "[$(date +%H:%M:%S)] linux gate build exit=$c $(( $(date +%s)-s ))s" >> $Q/timeline.log; exit $c
