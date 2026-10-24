#!/bin/bash
# print new notable timeline lines since last call (offset in checkpoint-2/.off)
M=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge8; o=$(cat $M/checkpoint-2/.off 2>/dev/null || echo 0); n=$(wc -l < $M/timeline.log)
sed -n "$((o+1)),${n}p" $M/timeline.log | grep -E "END |PASS|FAIL|TIMEOUT|ERROR|lock|DONE|exit=|refus|STAGE-JUDGE|REFERENCE" | cut -c1-230; echo $n > $M/checkpoint-2/.off; df -h / | tail -1 | awk '{print "disk avail",$4}'
