#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E4 (copy of E2): derive a probe capture (copy) with textual WGSL replacements; stage it on the Samsung.
Usage: mkprobe.py SRC_CAPTURE NEW_NAME FILE.wgsl 'old=>new' ['old=>new' ...]"""
import shutil, subprocess, sys
from pathlib import Path
E=Path(__file__).resolve().parents[1]; src=E/'captures'/sys.argv[1]; dst=E/'captures'/sys.argv[2]; f=sys.argv[3]
if dst.exists(): shutil.rmtree(dst)
dst.mkdir(); [shutil.copy2(p, dst/p.name) for p in src.iterdir() if p.suffix=='.wgsl']
s=(dst/f).read_text()
for r in sys.argv[4:]:
    a,b=r.split('=>',1); assert s.count(a)>=1,(a,s.count(a)); s=s.replace(a,b)
(dst/f).write_text(s)
adb=['/home/jeremy/Android/Sdk/platform-tools/adb','-s','R52W207XQ1V']; R='/data/local/tmp/codebrix-e4-probe'
subprocess.run(adb+['shell',f'rm -rf {R}/{dst.name}; mkdir -p {R}/{dst.name}; cp {R}/{sys.argv[1].split("-")[0]}/run.* {R}/{dst.name}/'],check=True)
subprocess.run(adb+['push']+[str(p) for p in dst.iterdir()]+[f'{R}/{dst.name}/'],check=True,capture_output=True)
print('staged',dst.name)
