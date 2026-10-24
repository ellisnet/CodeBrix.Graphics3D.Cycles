#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 7 part 2: device lock helpers (sourced). acquire DEV CHAIN: mkdir lock, retry every 120 s up to 60 min (never deletes a lock). release DEV.
L=/home/jeremy/ClaudeHome/cycles-wgsl-2026-10-19/device-locks
acquire(){ local dev=$1 chain=$2 w=0
  until mkdir $L/$dev 2>/dev/null; do
    [ $w -eq 0 ] && echo "[$(date +%H:%M:%S)] $dev lock HELD by $(cat $L/$dev/owner.txt 2>/dev/null | head -c 120); waiting ($chain)" >> $M/timeline.log
    w=$((w+120)); [ $w -gt 3600 ] && { echo "[$(date +%H:%M:%S)] $dev lock HELD for 60 min: $chain NOT RUN" >> $M/timeline.log; return 1; }; sleep 120; done
  echo "merge-wave-7 evidence agent $(date -Iseconds) $chain" > $L/$dev/owner.txt; echo "[$(date +%H:%M:%S)] $dev lock taken ($chain, waited ${w}s)" >> $M/timeline.log; return 0; }
release(){ local dev=$1; rm -f $L/$dev/owner.txt; rmdir $L/$dev && echo "[$(date +%H:%M:%S)] $dev lock released" >> $M/timeline.log; }
