#!/bin/bash
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
# Merge wave 6, slot S: lane S3's Linux stage-judge self-test (nodes_render, principled_render) and the same flow for the
# render_gate.cpp jobs that join the device-scene flow at merge wave 6 (microfacet_render, render static/motion, volume renders).
R=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles; M=$R/native/work/merge6; V=$M/validation; S=$M/tools/slot.sh; F=$M/tools/linux_flow.sh
cd $R
$S s-nodes_render 2400 bash $F nodes_render $V/s-nodes_render/flow
$S s-principled_render 2400 bash $F principled_render $V/s-principled_render/flow
$S s-microfacet_render 1800 bash $F microfacet_render $V/s-microfacet_render/flow
$S s-render-static 1800 bash $F render $V/s-render-static/flow
$S s-render-motion-1 1800 bash $F render $V/s-render-motion-1/flow --motion=1
CODEBRIX_VOLUME_ABSORPTION_ONLY=1 $S s-volume-absorption 1800 bash $F volume_render $V/s-volume-absorption/flow
CODEBRIX_VOLUME_G=.45 $S s-volume-forward 1800 bash $F volume_render $V/s-volume-forward/flow
CODEBRIX_VOLUME_CLOSED_COUNT=1 $S s-closed-single 1800 bash $F closed_volume_render $V/s-closed-single/flow
CODEBRIX_VOLUME_CLOSED_COUNT=4 CODEBRIX_VOLUME_CAMERA_INSIDE=1 $S s-closed-inside 1800 bash $F closed_volume_render $V/s-closed-inside/flow
echo "[$(date +%H:%M:%S)] slotS DONE" >> $M/timeline.log
