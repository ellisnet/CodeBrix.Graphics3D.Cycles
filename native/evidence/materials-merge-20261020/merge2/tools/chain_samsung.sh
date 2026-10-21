#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 2: wait for the Samsung lock (lock protocol, 60 s plain sleep), stage, run the materials corpora,
# the glass refusal/film corpus and the managed app scenes, clean the device and release the lock.
M=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge2; L=/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/device-locks
until mkdir $L/R52W207XQ1V 2>/dev/null; do sleep 60; done
echo "merge-wave-2 agent (materials merge) $(date -Iseconds)" > $L/R52W207XQ1V/owner.txt
echo "[$(date +%H:%M:%S)] samsung lock taken" >> $M/timeline.log
cd $M/tools
echo "[$(date +%H:%M:%S)] DECLARED samsung staging push (stage.py push samsung): budget 1200 s" >> $M/budgets.txt
timeout 1200 python3 stage.py push samsung
python3 run_corpus.py samsung materials mw2a 7200 2400 --jobs principled_svm,principled_bounce,nodes_svm,nodes_world
bash chain_dev.sh samsung
A="/home/jeremy/Android/Sdk/platform-tools/adb -s R52W207XQ1V"
$A shell 'rm -rf /data/local/tmp/codebrix-mw2-*'
left=$($A shell 'ls -d /data/local/tmp/codebrix-mw2* 2>/dev/null; pm list packages | grep mw2')
echo "[$(date +%H:%M:%S)] samsung cleaned (left: '$left')" >> $M/timeline.log
rm $L/R52W207XQ1V/owner.txt && rmdir $L/R52W207XQ1V && echo "[$(date +%H:%M:%S)] samsung lock released" >> $M/timeline.log
