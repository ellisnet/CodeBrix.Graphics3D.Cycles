#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 2 device chain after the SVM/bounce/world corpora: materials renders + on-device replays (glibc Linux
# reference export), glass specialization refusal + specialized film, compile probes (HP), managed app scenes.
# Usage: chain_dev.sh DEVICE   (hp | pixel | samsung)
cd "$(dirname "$0")"; dev=$1; M=$(realpath ..); ref=$M/reference/linux-x64
case $dev in
  hp) F="--reference $ref --mesa-scratch-cache"; rid=android-x64; serial=192.168.86.225:5555;;
  pixel) F="--reference $ref --pipeline-budget-ms 20000"; rid=android-arm64; serial=98271FFBA0048D;;
  samsung) F="--reference $ref"; rid=android-arm64; serial=R52W207XQ1V;;
esac
python3 run_corpus.py $dev materials mw2r 7200 2400 --jobs principled_render,nodes_render $F
python3 run_corpus.py $dev spec-refusal mw2 1800 1200 $F
if [ $dev = hp ]; then
  for spec in nodes_render nodes_render-unspecialized; do for e in prepare_surface shade_surface_light; do
    echo "[$(date +%H:%M:%S)] DECLARED hp compile probe $spec/$e (fresh process, cache disabled): budget 1200 s" >> $M/budgets.txt
    timeout 1300 python3 probe.py run --device hp --wgsl $M/validation/l2-nodes-gate/out/$spec/surface.wgsl --entry $e --timeout 1200 --tag $spec --log $M/device/hp-compile-probes.jsonl > /dev/null 2>&1
    echo "[$(date +%H:%M:%S)] hp compile probe $spec/$e exit=$?" >> $M/timeline.log
  done; done
  /home/jeremy/Android/Sdk/platform-tools/adb -s $serial shell 'rm -rf /data/local/tmp/codebrix-mw2-probe'
fi
bash chain_app.sh $dev $rid $serial "lock taken"
echo "[$(date +%H:%M:%S)] $dev chain_dev DONE" >> $M/timeline.log
