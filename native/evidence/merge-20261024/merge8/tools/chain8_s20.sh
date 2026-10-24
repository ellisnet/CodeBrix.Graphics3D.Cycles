#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 8 part 2: S20 (RFCRB0MPHYH, Adreno 650, fix-or-degrade) A650-2 regression set, principled_svm FIRST. Per-job timeout = s20_timeouts.py
# (max(600 s, 3 x tablet wall)); principled_bounce gets max(that, 2400 s). Chain cap 3 hours: jobs not started by then are logged as not run.
cd "$(dirname "$0")"; M=$(realpath ..); source ./lock8.sh
ref=$M/reference-1b/linux-x64; serial=RFCRB0MPHYH; A="/home/jeremy/Android/Sdk/platform-tools/adb -s $serial"
lg(){ echo "[$(date +%H:%M:%S)] s20 $*" >> $M/timeline.log; }
python3 s20_timeouts.py > $M/device/s20-timeouts.json || { lg "timeout computation failed"; exit 1; }
acquire s20 "chain8_s20.sh" || exit 2; T0=$(date +%s)
echo "[$(date +%H:%M:%S)] DECLARED s20 staging push (stage.py push s20): budget 1200 s; s20 chain cap 10800 s" >> $M/budgets.txt
timeout 1200 python3 stage.py push s20 >> $M/device/s20-chain.log 2>&1; c=$?; lg "staged exit=$c"
if [ $c -ne 0 ]; then lg "STAGING FAILED: chain stopped"; $A shell 'rm -rf /data/local/tmp/codebrix-m8-*'; release s20; exit 3; fi
tmo(){ python3 -c "import json;print(json.load(open('$M/device/s20-timeouts.json'))['$1']['timeoutSeconds'])"; }
one(){ c=$1; job=$2; label=$3; budget=$4; to=$5
  if [ $(( $(date +%s)-T0 )) -gt 10800 ]; then lg "NOT RUN (3 h cap) $c $job"; return; fi
  python3 run_corpus.py s20 $c $label $budget $to --reference $ref --device-scene --jobs $job >> $M/device/s20-chain.log 2>&1
  lg "corpus $c-$label ($job) exit=$? timeout=${to}s"; }
to=$(tmo principled_svm); one materials principled_svm s20-principled_svm $((to*2+1800)) $to
python3 run_corpus.py s20 a650-refusals m8 600 300 --reference $ref --device-scene >> $M/device/s20-chain.log 2>&1; lg "corpus a650-refusals-m8 exit=$?"
for spec in triangle:triangle_sample regression:glass_render regression:lighting_mis triangle:surface transparent:transparent_lighting transparent:transparent_shadow materials:principled_render pixel-motion:object-moving-images pixel-motion:deforming-moving-images; do
  c=${spec%%:*}; job=${spec#*:}; to=$(tmo $job); one $c $job s20-$job $((to*2+1800)) $to
done
one environment environment s20-environment 4200 1800
# wave 8 additions (new gates on the S20): shadow catcher (case 7 refused by design), AO (node + shadow rays refused, fast-GI replace passes),
# MNEE (fails as on the tablet, DEVICE.md item 19), light tree (never run on the S20 before: a finding, not a regression)
one shadowcatcher shadow_catcher s20-shadow_catcher 5400 2700
one s20-ao ao_state-refused,ao_render-additive-refused,ao_render-node-refused,ao_render-replace s20-ao 1800 900
one mnee mnee_render,mnee s20-mnee 2400 900
one lighttree light_tree s20-light_tree 5400 2700
to=$(tmo principled_bounce); [ $to -lt 2400 ] && to=2400; one materials principled_bounce s20-principled_bounce $((to*2+1800)) $to
bash cleanup8.sh s20 $serial
release s20
