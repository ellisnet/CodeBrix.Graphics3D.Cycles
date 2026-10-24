#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 6 part 2: cleanup on a device. Usage: cleanup6.sh DEV SERIAL [leftovers]. Removes codebrix-m6* paths (and codebrix-maintained-host-m6-hostgate-*); with
# "leftovers" also lists (with sizes, to device/DEV-leftovers-listing.txt) and removes codebrix-* scratch last modified before 2026-10-22 that no current lane prefix
# owns (codebrix-m1o-, codebrix-m6-); codebrix-ncdbg and every com.codebrix.* package are never touched. Caller holds the device lock.
dev=$1; serial=$2; M=$(realpath "$(dirname "$0")/.."); A="/home/jeremy/Android/Sdk/platform-tools/adb -s $serial"
lg(){ echo "[$(date +%H:%M:%S)] $dev $*" >> $M/timeline.log; }
$A shell 'rm -rf /data/local/tmp/codebrix-m6-* /data/local/tmp/codebrix-maintained-host-m6-hostgate-*'
if [ "$3" = leftovers ]; then
  out=$M/device/$dev-leftovers-listing.txt
  $A shell 'cd /data/local/tmp; for n in codebrix-*; do [ -e "$n" ] || continue; d=$(ls -ld "$n" | awk "{print \$6}"); k=$(du -sk "$n" 2>/dev/null | cut -f1); echo "$d $k $n"; done' | tr -d '\r' | sort > $out.all
  # candidates: older than 2026-10-22 and not owned by a current prefix
  awk '$1 < "2026-10-22" && $3 != "codebrix-ncdbg" && $3 !~ /^codebrix-m1o-/ && $3 !~ /^codebrix-m6-/ {print}' $out.all > $out
  kept=$(awk '!($1 < "2026-10-22" && $3 != "codebrix-ncdbg" && $3 !~ /^codebrix-m1o-/ && $3 !~ /^codebrix-m6-/) {print $3" ("$1")"}' $out.all | tr '\n' ' ')
  total=$(awk '{s+=$2} END {print s}' $out)
  lg "leftovers listed: $(wc -l < $out) folders, ${total} KB, listing $out; kept: $kept"
  for n in $(awk '{print $3}' $out); do case "$n" in codebrix-*) $A shell "rm -rf /data/local/tmp/$n";; esac; done
  lg "leftovers removed; remaining codebrix-*: $($A shell 'ls -d /data/local/tmp/codebrix-* 2>/dev/null' | tr '\n\r' '  ')"
fi
left=$($A shell 'ls -d /data/local/tmp/codebrix-m6* /data/local/tmp/codebrix-maintained-host-m6* 2>/dev/null; pm list packages | grep "\.m6$"'; $A shell 'ps -A -o PID,ARGS' | grep -E 'codebrix|render_replay' | grep -v grep)
lg "cleaned (left: '$left')"
