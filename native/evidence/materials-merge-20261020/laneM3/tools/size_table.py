#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""M3: summarize wgsl_stats.py output (bytes, inlined bytes per entry) for module files."""
import json,subprocess,sys
from pathlib import Path
tool=Path(__file__).with_name('wgsl_stats.py')
for f in sys.argv[1:]:
    t=subprocess.run([sys.executable,str(tool),f],capture_output=True,text=True).stdout
    dec=json.JSONDecoder();i=0
    while i<len(t):
        t2=t[i:].lstrip()
        if not t2:break
        d,n=dec.raw_decode(t2);i=len(t)-len(t2)+n
        e=d['entries'];print(d['module'],d['bytes'],{k:(v['inlinedBytes'],v['privateArrays']) for k,v in e.items() if k in ('prepare_surface','shade_background','shade_surface_light','shade_surface','connect_surface_light')})
