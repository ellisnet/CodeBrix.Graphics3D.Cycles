#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Lane P: wait until the timeline (after line N) shows PATTERN; print new result lines. Usage: wait_for.sh N PATTERN
F=$(dirname "$0")/../timeline.log; n=$1; until tail -n +$n $F | grep -q -E "$2"; do sleep 10; done
tail -n +$n $F | grep -E "PASS|FAIL|CUTOFF|END step|ERROR|SKIP|lock" ; wc -l < $F
