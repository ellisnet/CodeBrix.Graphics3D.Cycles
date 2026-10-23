#!/bin/bash
M=/home/jeremy/GitHome/CodeBrix.Graphics3D.Cycles/native/work/merge5
while [ ! -e $M/checkpoint-2/STOP ]; do sleep 1800; [ -e $M/checkpoint-2/STOP ] || bash $M/tools/status5.sh "automatic 30-minute checkpoint"; done
