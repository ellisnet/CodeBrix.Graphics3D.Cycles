#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
from pathlib import Path

root=Path(__file__).resolve().parents[1]
output=root/'work/generated'
output.mkdir(parents=True,exist_ok=True)
shader=(root.parent/'research/work/translated.wgsl').read_text()+'\n'+(root/'shaders/render.wgsl').read_text()
assert ')CBWGSL"' not in shader
(output/'render_shader.inc').write_text('R"CBWGSL('+shader+')CBWGSL"\n')
(output/'render.wgsl').write_text(shader)
print(output)
