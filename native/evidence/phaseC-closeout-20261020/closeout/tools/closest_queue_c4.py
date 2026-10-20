# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Phase C close-out: run the maintained native/tools/closest_queue_gate.py 'run' action unchanged,
# with its (and its device/camera prerequisites') output folders redirected under phaseC4.
import sys
from pathlib import Path
sys.path.insert(0, '/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/tools')
import device_gate, camera_queue_gate, closest_queue_gate
out = Path(sys.argv[1]).resolve()
device_gate.OUT = out/'device-queue'; camera_queue_gate.OUT = out/'camera-queue'; closest_queue_gate.OUT = out/'closest-queue'
closest_queue_gate.run()
