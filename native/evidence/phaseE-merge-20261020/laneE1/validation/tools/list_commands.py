#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Measurement only: list a capture's command stream (render_replay.cpp format)."""
import struct, sys
names = ['control','init_camera','intersect_closest','shade_background','shade_surface','prepare_surface','intersect_shadow','shade_shadow',
         'shade_light_forward','prepare_shader_data','intersect_volume_stack','shade_volume','shade_shadow_transparent','shade_surface_light',
         'shader_eval_volume_density','connect_surface_light','prepare_shadow_shader_data']
data = open(sys.argv[1], 'rb').read(); lo, hi = int(sys.argv[2]), int(sys.argv[3])
cursor = 0; number = 0
while cursor < len(data):
    kind, address, length = struct.unpack_from('<3I', data, cursor); cursor += 12; number += 1; extra = ''
    if kind == 1: cursor += length
    elif kind in (3, 5):
        wg = 64
        if kind == 5: wg = struct.unpack_from('<I', data, cursor)[0]; cursor += 4
        params = struct.unpack_from('<32I', data, cursor); cursor += 128; extra = f'{names[address]} n={length} wg={wg} p1={params[1]} p2={params[2]}'
    elif kind == 4:
        payload = data[cursor:cursor+length]; cursor += length; extra = 'readback ' + (str(struct.unpack_from(f'<{min(length//4,8)}I', payload)) if length >= 4 else '')
    if lo <= number <= hi: print(number, kind, address, length, extra)
