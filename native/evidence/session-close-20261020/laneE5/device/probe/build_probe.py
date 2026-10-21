#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Phase E5: build the diagnostic sampler_probe (work folder only, never shipped) by reusing the
triangle_sample gate's own compile and link commands from a finished build tree, with only the
gate source swapped for probe/sampler_probe.cpp.
Usage: build_probe.py BUILD_DIR OUTPUT_EXECUTABLE"""
import json, shlex, subprocess, sys
from pathlib import Path
HERE = Path(__file__).resolve().parent
build = Path(sys.argv[1]).resolve(); out = Path(sys.argv[2]).resolve(); out.parent.mkdir(parents=True, exist_ok=True)
rows = [r for r in json.loads((build/'compile_commands.json').read_text())
        if r['file'].endswith('cycles/emitter_sample_gate.cpp') and 'codebrix_cycles_triangle_sample_gate.dir' in r['command']]
assert len(rows) == 1, rows
row = rows[0]; cmd = shlex.split(row['command'])
obj = out.with_suffix('.o')
i = cmd.index('-o'); cmd[i+1] = str(obj)
j = cmd.index('-c'); cmd[j+1] = str(HERE/'sampler_probe.cpp'); cmd[j:j] = ['-I', str(Path(row['file']).parent)]
subprocess.run(cmd, cwd=row['directory'], check=True)
link_dir = build/'src/app' if False else None
link = next(build.rglob('codebrix_cycles_triangle_sample_gate.dir/link.txt'))
words = shlex.split(link.read_text())
words = [str(obj) if w.endswith('emitter_sample_gate.cpp.o') else w for w in words]
k = words.index('-o'); words[k+1] = str(out)
subprocess.run(words, cwd=link.parents[2], check=True)
print(out)
