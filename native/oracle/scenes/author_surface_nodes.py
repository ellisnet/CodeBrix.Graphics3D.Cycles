#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the surface node oracle scenes (scenes/surface-nodes/) byte for byte.

The Bevel node (rounded box edges and a smooth sphere), the Raycast node (hit
distance and hit normal along the shading normal, and an only_local ray into a
box), the Radial Tiling node (regular and irregular, rounded polygons), plain
attributes named like standard attributes and a Bump node with a strong noise
height (of the position and of an attribute), through the public C API, lit by a sphere lamp with MIS and a constant world.
The Integer Math, Boolean Math and Get Vector Component nodes with linked inputs, as tables of one cell per case under an
orthographic camera (integer results as exact base-256 digits in AOVs; only cases whose pinned evaluation is defined), the
Get Vector Component node as a Bump height, and the Missing and Missing Alpha inputs of Image Texture, Attribute and
Environment Texture nodes that have no image or attribute.
Usage: python3 native/oracle/scenes/author_surface_nodes.py [--check]."""
import argparse
import json
import struct
from pathlib import Path
from author_bsdf import single, shader, quad, sphere, BACK, FLOOR, f32

HERE = Path(__file__).resolve().parent/'surface-nodes'


def box(handle, material, center, size):
    positions = [f32(center[k]+(size[k]*0.5 if i >> k & 1 else -size[k]*0.5)) for i in range(8) for k in range(3)]
    indices = [0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5]
    return [{'handle': handle, 'create': 'mesh', 'mesh': {'positions': positions, 'indices': indices}, 'set': {'used_shaders': [material]}},
            {'handle': handle+'-object', 'create': 'object', 'set': {'geometry': handle}}]


def scene(scene_id, description, materials, shapes):
    nodes = [{'handle': 'integrator', 'scene': 'integrator', 'set': {
                 'use_guiding': False, 'use_light_tree': False, 'use_adaptive_sampling': False, 'use_denoise': False,
                 'min_bounce': 2, 'max_bounce': 4, 'filter_glossy': 0.0, 'caustics_reflective': True, 'caustics_refractive': True}},
             {'handle': 'film', 'scene': 'film', 'set': {'use_sample_count': False}},
             {'handle': 'camera', 'scene': 'camera', 'set': {'fov': 0.9, 'matrix': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0]}},
             single('wall', 'diffuse_bsdf', {'color': [0.6, 0.62, 0.65], 'roughness': 0.25}),
             single('floor-material', 'diffuse_bsdf', {'color': [0.5, 0.45, 0.35], 'roughness': 0.0}),
             *materials, *quad('back', 'wall', BACK), *quad('floor', 'floor-material', FLOOR), *shapes,
             shader('world', [{'handle': 'b', 'type': 'background_shader', 'set': {'color': [0.3, 0.35, 0.42], 'strength': 1.0}}],
                    [['b', 'Background', 'output', 'Surface']]),
             {'handle': 'bg', 'scene': 'background', 'set': {'shader': 'world'}},
             shader('emitter', [{'handle': 'e', 'type': 'emission', 'set': {'color': [1, 1, 1], 'strength': 1}}], [['e', 'Emission', 'output', 'Surface']]),
             {'handle': 'lamp', 'create': 'pointlight', 'set': {'radius': 0.45, 'is_sphere': True, 'use_mis': True, 'strength': [110, 90, 70], 'used_shaders': ['emitter']}},
             {'handle': 'lamp-object', 'create': 'object', 'set': {'geometry': 'lamp', 'tfm': [1.0, 0.0, 0.0, 2.2, 0.0, 1.0, 0.0, 2.0, 0.0, 0.0, 1.0, -2.0]}}]
    return {'cbcOracleScene': 1, 'id': scene_id, 'description': description,
            'sourceFixture': 'native/cycles/raytrace_nodes_fixture.h and bsdf_fixture.h (the same nodes)',
            'render': {'width': 32, 'height': 24, 'samples': 8, 'repeats': 2}, 'colorConfig': None, 'nodes': nodes, 'images': []}


def bevel_material(handle, radius, samples):
    return shader(handle, [{'handle': 'bevel', 'type': 'bevel', 'set': {'radius': radius, 'samples': samples}},
                           {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': [0.7, 0.5, 0.35], 'roughness': 0.2}},
                           {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'color': [0.3, 0.4, 0.8], 'roughness': 0.3}},
                           {'handle': 'm', 'type': 'mix_closure', 'set': {'fac': 0.4}}],
                  [['bevel', 'Normal', 'd', 'Normal'], ['bevel', 'Normal', 'g', 'Normal'], ['g', 'BSDF', 'm', 'Closure1'],
                   ['d', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']])


def raycast_material(handle, local):
    nodes = [{'handle': 'ray', 'type': 'raycast', 'set': {'length': 1.6 if local else 2.5, 'only_local': local}},
             {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': [0.6, 0.5, 0.35], 'roughness': 0.2}},
             {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'roughness': 0.3}},
             {'handle': 'm', 'type': 'mix_closure', 'set': {}},
             {'handle': 't', 'type': 'math', 'set': {'math_type': 'multiply', 'value2': 0.3}},
             {'handle': 'c', 'type': 'combine_xyz', 'set': {}}]
    connect = [['ray', 'Is Hit', 'm', 'Fac'], ['ray', 'Hit Distance', 't', 'Value1'], ['t', 'Value', 'c', 'X'], ['t', 'Value', 'c', 'Z'],
               ['c', 'Vector', 'd', 'Color'], ['ray', 'Hit Normal', 'g', 'Color'], ['g', 'BSDF', 'm', 'Closure1'], ['d', 'BSDF', 'm', 'Closure2'],
               ['m', 'Closure', 'output', 'Surface']]
    if local:
        nodes.append({'handle': 'dir', 'type': 'color', 'set': {'value': [0.15, -1.0, 0.35]}})
        connect.append(['dir', 'Color', 'ray', 'Direction'])
    return shader(handle, nodes, connect)


def radial_material(handle, sides, roundness, normalize):
    return shader(handle, [{'handle': 'geo', 'type': 'geometry', 'set': {}},
                           {'handle': 'rt', 'type': 'radial_tiling', 'set': {'r_gon_sides': sides, 'r_gon_roundness': roundness, 'use_normalize': normalize}},
                           {'handle': 'c', 'type': 'combine_xyz', 'set': {}},
                           {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.3}},
                           {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'roughness': 0.4}},
                           {'handle': 'm', 'type': 'mix_closure', 'set': {'fac': 0.3}}],
                  [['geo', 'Position', 'rt', 'Vector'], ['rt', 'Segment Coordinates', 'd', 'Color'], ['rt', 'Segment ID', 'c', 'X'],
                   ['rt', 'Segment Width', 'c', 'Y'], ['rt', 'Segment Rotation', 'c', 'Z'], ['c', 'Vector', 'g', 'Color'],
                   ['g', 'BSDF', 'm', 'Closure1'], ['d', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']])


def named_like_standard_material(handle):
    """Attribute nodes asking for the names "color" and "pointiness", which are also standard attribute names."""
    return shader(handle, [{'handle': 'c', 'type': 'attribute', 'set': {'attribute': 'color'}},
                           {'handle': 'p', 'type': 'attribute', 'set': {'attribute': 'pointiness'}},
                           {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.0}},
                           {'handle': 'e', 'type': 'emission', 'set': {'color': [0.2, 0.5, 0.9]}},
                           {'handle': 'a', 'type': 'add_closure', 'set': {}}],
                  [['c', 'Color', 'd', 'Color'], ['p', 'Fac', 'e', 'Strength'], ['d', 'BSDF', 'a', 'Closure1'],
                   ['e', 'Emission', 'a', 'Closure2'], ['a', 'Closure', 'output', 'Surface']])


def uv_material(handle):
    """The Texture Coordinate node's UV (a request for the standard UV map with no name) colouring a checker."""
    return shader(handle, [{'handle': 't', 'type': 'texture_coordinate', 'set': {}},
                           {'handle': 'k', 'type': 'checker_texture', 'set': {'scale': 4.0, 'color1': [0.9, 0.7, 0.1], 'color2': [0.1, 0.3, 0.7]}},
                           {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.0}}],
                  [['t', 'UV', 'k', 'Vector'], ['k', 'Color', 'd', 'Color'], ['d', 'BSDF', 'output', 'Surface']])


def strong_bump_material(handle, color):
    """A Bump node whose height is a high-frequency noise of the position: the height differences are differences of
    noise values a pixel footprint apart, so the normal follows the last bits of the offset positions."""
    return shader(handle, [{'handle': 'geo', 'type': 'geometry', 'set': {}},
                           {'handle': 'n', 'type': 'noise_texture', 'set': {'dimensions': '3D', 'scale': 9.0, 'detail': 2.0}},
                           {'handle': 'bump', 'type': 'bump', 'set': {'strength': 1.0, 'distance': 0.6}},
                           {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': color, 'roughness': 0.0}}],
                  [['geo', 'Position', 'n', 'Vector'], ['n', 'Fac', 'bump', 'Height'], ['bump', 'Normal', 'd', 'Normal'], ['d', 'BSDF', 'output', 'Surface']])


def strong_bump_attribute_material(handle, color):
    """The strong noise Bump with the noise coordinates read from the vertex attribute "coords" by an Attribute node: the
    offset positions of the Bump are the attribute's value plus its derivative times the filter width."""
    return shader(handle, [{'handle': 'a', 'type': 'attribute', 'set': {'attribute': 'coords'}},
                           {'handle': 'n', 'type': 'noise_texture', 'set': {'dimensions': '3D', 'scale': 9.0, 'detail': 2.0}},
                           {'handle': 'bump', 'type': 'bump', 'set': {'strength': 1.0, 'distance': 0.6}},
                           {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': color, 'roughness': 0.0}}],
                  [['a', 'Vector', 'n', 'Vector'], ['n', 'Fac', 'bump', 'Height'], ['bump', 'Normal', 'd', 'Normal'], ['d', 'BSDF', 'output', 'Surface']])


def coords_attribute(positions):
    """A vertex attribute "coords": an affine map of the vertex positions with no axis-aligned row."""
    data = []
    for k in range(0, len(positions), 3):
        x, y, z = positions[k:k+3]
        data += [f32(0.731*x+0.213*y-0.377*z+0.137), f32(-0.191*x+0.877*y+0.419*z-0.291), f32(0.283*x-0.337*y+0.691*z+0.613)]
    return [{'name': 'coords', 'value': 'point', 'domain': 'vertex', 'data': data}]


def with_attributes(nodes, attributes):
    nodes[0]['mesh']['attributes'] = attributes
    return nodes


# --- Integer Math, Boolean Math and Get Vector Component with linked inputs ------------------------------------------
# The WebGPU device refuses these nodes when an input is linked (the SVM compiler folds all-constant ones). Table scenes:
# one quad of CELL x CELL pixels per case, facing an orthographic camera that maps one world unit to one pixel, with a box
# pixel filter of width 1, so every sample of a pixel sees one cell and its AOVs are the cell's values exactly. Each operand
# is read from face attributes by Attribute nodes: an int32 is the Integer Math sum of two floats that binary32 holds
# exactly (the float-to-int convert of the links, NODE_CONVERT_FI). Integer results leave through four base-256 digits
# (D0..D3, each exact as a float), with R (the result as a float) and CELL (the cell number, from 1) beside them.
INT_MIN, INT_MAX = -2**31, 2**31-1
CELL = 4


class Undefined(ArithmeticError):
    """The pinned evaluation of a case has undefined behaviour in C++ and MSL (a signed overflow, INT_MIN / -1, a
    division by zero)."""


def pinned_integer_math(op, a, b=0, c=0, strict=True):
    """kernel/svm/integer_math.h, operation by operation. strict raises Undefined at the first undefined step; otherwise
    each step wraps (two's complement) and a division by zero gives 0, as AArch64 sdiv does: a model of one machine, not a
    definition, for reporting the undefined cases."""
    def w(v):
        if INT_MIN <= v <= INT_MAX:
            return v
        if strict:
            raise Undefined(op)
        return (v+2**31) % 2**32-2**31

    def div(x, y):
        if y == 0:
            if strict:
                raise Undefined(op)
            return 0
        q = abs(x)//abs(y)
        return w(q if (x < 0) == (y < 0) else -q)

    def mod(x, y):
        if y == 0:
            if strict:
                raise Undefined(op)
            return x
        if x == INT_MIN and y == -1:
            if strict:
                raise Undefined(op)
            return 0
        q = abs(x)//abs(y)
        return x-(q if (x < 0) == (y < 0) else -q)*y

    def floor_div(x, y):
        d, r = div(x, y), mod(x, y)
        return w(d-int((x < 0) != (y < 0))) if r != 0 else d

    def gcd(x, y):
        x, y = w(abs(x)), w(abs(y))
        while y != 0:
            x, y = y, mod(x, y)
        return x

    def power(base, exponent):
        if exponent < 0:
            return (-1 if base < 0 and exponent & 1 else 1) if base in (1, -1) else 0
        result = 1
        while exponent != 0:
            if exponent & 1:
                result = w(result*base)
            exponent >>= 1
            base = w(base*base)
        return result

    sign = lambda x: int(0 < x)-int(x < 0)
    if op == 'add': return w(a+b)
    if op == 'subtract': return w(a-b)
    if op == 'multiply': return w(a*b)
    if op == 'divide': return div(a, b) if b != 0 else 0
    if op == 'multiply_add': return w(w(a*b)+c)
    if op == 'power': return power(a, b)
    if op == 'floored_modulo': return mod(w(mod(a, b)+b), b) if b != 0 else 0
    if op == 'absolute': return w(abs(a))
    if op == 'minimum': return min(a, b)
    if op == 'maximum': return max(a, b)
    if op == 'gcd': return gcd(a, b)
    if op == 'lcm':
        g = gcd(a, b)
        return w(abs(w(div(a, g)*b))) if g != 0 else 0
    if op == 'negate': return w(-a)
    if op == 'sign': return sign(a)
    if op == 'divide_floor': return floor_div(a, b) if b != 0 else 0
    if op == 'divide_ceil': return w(-floor_div(a, w(-b))) if b != 0 else 0
    if op == 'divide_round':
        abs_b = w(abs(b))
        if a >= 0:
            return w((div(w(w(2*a)+abs_b), w(2*abs_b)) if abs_b != 0 else 0)*sign(b))
        return w(w(-(div(w(w(2*w(-a))+abs_b), w(2*abs_b)) if abs_b != 0 else 0))*sign(b))
    if op == 'modulo': return mod(a, b) if b != 0 else 0
    raise ValueError(op)


def pinned_boolean_math(op, a, b):
    """kernel/svm/boolean_math.h: an input is true when it is above 0."""
    a, b = a > 0, b > 0
    return int({'and': a and b, 'or': a or b, 'not': not a, 'nand': not (a and b), 'nor': not (a or b), 'xnor': a == b,
                'xor': a != b, 'imply': (not a) or b, 'nimply': a and not b}[op])


def operand(value):
    """An int32 as two floats that binary32 holds exactly, hi + lo = value; their Integer Math sum never overflows."""
    if -(1 << 24) <= value <= 1 << 24 or value == INT_MIN:
        return float(value), 0.0
    hi = abs(value)//256*256*(1 if value > 0 else -1)
    return float(hi), float(value-hi)


def attribute_node(handle, name):
    return {'handle': handle, 'type': 'attribute', 'set': {'attribute': name}}


def integer_operand(name):
    """The operand `name`: Attribute nodes <name>_hi and <name>_lo summed by an Integer Math add (handle `name`)."""
    return ([attribute_node(name+'-hi', name+'_hi'), attribute_node(name+'-lo', name+'_lo'),
             {'handle': name, 'type': 'integer_math', 'set': {'math_type': 'add'}}],
            [[name+'-hi', 'Fac', name, 'Value1'], [name+'-lo', 'Fac', name, 'Value2']])


def table_outputs(result, output, aovs):
    """An emission of strength result.output, the AOV value outputs (name, node, socket) and CELL."""
    nodes = [attribute_node('cell', 'cell'), {'handle': 'e', 'type': 'emission', 'set': {'color': [1.0, 1.0, 1.0]}}]
    connect = [[result, output, 'e', 'Strength'], ['e', 'Emission', 'output', 'Surface']]
    for name, node, socket in aovs+[('CELL', 'cell', 'Fac')]:
        nodes.append({'handle': 'aov-'+name, 'type': 'aov_output', 'set': {'name': name}})
        connect.append([node, socket, 'aov-'+name, 'Value'])
    return nodes, connect


def integer_material(op):
    nodes, connect = [], []
    for name in ('a', 'b', 'c'):
        more, links = integer_operand(name)
        nodes += more
        connect += links
    nodes.append({'handle': 'op', 'type': 'integer_math', 'set': {'math_type': op}})
    connect += [['a', 'Value', 'op', 'Value1'], ['b', 'Value', 'op', 'Value2'], ['c', 'Value', 'op', 'Value3']]
    # Base-256 digits of the result: D0 and D1, D2 floored modulo 256 of the floored quotients, D3 = floor(r / 2^24).
    for handle, kind, source, divisor in [('q1', 'divide_floor', 'op', 256), ('q2', 'divide_floor', 'op', 65536), ('d0', 'floored_modulo', 'op', 256),
                                          ('d1', 'floored_modulo', 'q1', 256), ('d2', 'floored_modulo', 'q2', 256), ('d3', 'divide_floor', 'op', 16777216)]:
        nodes.append({'handle': handle, 'type': 'integer_math', 'set': {'math_type': kind, 'value2': divisor}})
        connect.append([source, 'Value', handle, 'Value1'])
    more, links = table_outputs('op', 'Value', [('D0', 'd0', 'Value'), ('D1', 'd1', 'Value'), ('D2', 'd2', 'Value'), ('D3', 'd3', 'Value'), ('R', 'op', 'Value')])
    return shader('im-'+op, nodes+more, connect+links)


def boolean_material(op):
    nodes, connect = [], []
    for name in ('a', 'b'):
        more, links = integer_operand(name)
        nodes += more
        connect += links
    nodes.append({'handle': 'op', 'type': 'boolean_math', 'set': {'math_type': op}})
    connect += [['a', 'Value', 'op', 'Boolean1'], ['b', 'Value', 'op', 'Boolean2']]
    more, links = table_outputs('op', 'Boolean', [('B', 'op', 'Boolean')])
    return shader('bm-'+op, nodes+more, connect+links)


def component_material():
    nodes, connect = integer_operand('a')
    nodes = nodes+[attribute_node('vec', 'vec'), {'handle': 'op', 'type': 'get_vector_component'}]
    connect = connect+[['vec', 'Vector', 'op', 'Vector'], ['a', 'Value', 'op', 'Index']]
    more, links = table_outputs('op', 'Value', [('V', 'op', 'Value')])
    return shader('gvc', nodes+more, connect+links)


def table_scene(scene_id, description, materials, rows, aovs, vectors=()):
    """rows: top row first, a list of cells each, a cell {attribute: float or [x, y, z]}; materials: one per row, or one for
    every row; absent cells show the black world. One mesh holds every cell (two triangles, face attributes, the material
    by the face socket shader)."""
    assert len(materials) in (1, len(rows)), 'one material for every row, or one per row'
    columns = max(len(cells) for cells in rows)
    width, height = columns*CELL, len(rows)*CELL
    scale = min(width, height)/2
    names = sorted({name for cells in rows for cell in cells for name in cell})
    positions, indices, shaders, data = [], [], [], {name: [] for name in names+['cell']}
    number = 0
    for r, cells in enumerate(rows):
        for c, cell in enumerate(cells):
            number += 1
            x0, y1 = c*CELL-width/2, height/2-r*CELL
            base = len(positions)//3
            positions += [x0, y1-CELL, 0.0, x0+CELL, y1-CELL, 0.0, x0+CELL, y1, 0.0, x0, y1, 0.0]
            indices += [base, base+1, base+2, base, base+2, base+3]
            shaders += [r if len(materials) > 1 else 0]*2
            for name in names:
                value = cell.get(name, [0.0, 0.0, 0.0] if name in vectors else 0.0)
                data[name] += (list(value) if name in vectors else [value])*2
            data['cell'] += [float(number)]*2
    attributes = [{'name': name, 'value': 'vector' if name in vectors else 'float', 'domain': 'face', 'data': data[name]} for name in names+['cell']]
    nodes = [{'handle': 'integrator', 'scene': 'integrator', 'set': {
                 'use_guiding': False, 'use_light_tree': False, 'use_adaptive_sampling': False, 'use_denoise': False, 'min_bounce': 0,
                 'max_bounce': 0, 'sampling_pattern': 'tabulated_sobol', 'seed': 0}},
             {'handle': 'film', 'scene': 'film', 'set': {'filter_type': 'box', 'filter_width': 1.0, 'use_sample_count': False}},
             {'handle': 'camera', 'scene': 'camera', 'set': {'camera_type': 'orthograph',
                                                              'matrix': [scale, 0.0, 0.0, 0.0, 0.0, scale, 0.0, 0.0, 0.0, 0.0, scale, -7.0]}},
             shader('world', [{'handle': 'b', 'type': 'background_shader', 'set': {'color': [0.0, 0.0, 0.0], 'strength': 0.0}}],
                    [['b', 'Background', 'output', 'Surface']]),
             {'handle': 'bg', 'scene': 'background', 'set': {'shader': 'world'}},
             *materials,
             {'handle': 'table', 'create': 'mesh', 'mesh': {'positions': positions, 'indices': indices, 'attributes': attributes},
              'set': {'used_shaders': [m['handle'] for m in materials], 'shader': shaders}},
             {'handle': 'table-object', 'create': 'object', 'set': {'geometry': 'table'}},
             *[{'handle': 'pass-'+name, 'create': 'pass', 'set': {'type': 'aov_value', 'name': name}} for name in aovs+['CELL']]]
    return {'cbcOracleScene': 1, 'id': scene_id, 'description': description,
            'sourceFixture': 'native/oracle/scenes/author_surface_nodes.py (table scenes: one cell per case)',
            'render': {'width': width, 'height': height, 'samples': 4, 'repeats': 2}, 'colorConfig': None, 'nodes': nodes, 'images': []}


INTEGER_OPERATIONS = ['add', 'subtract', 'multiply', 'divide', 'multiply_add', 'power', 'floored_modulo', 'absolute', 'minimum', 'maximum',
                      'gcd', 'lcm', 'negate', 'sign', 'divide_floor', 'divide_ceil', 'divide_round', 'modulo']
# Cases whose pinned evaluation is defined (integer_math_cases checks it). The undefined ones (signed overflow, INT_MIN / -1,
# divide_round by INT_MIN) are not committed scenes: their films would depend on the compiler and the machine.
INTEGER_CASES = {
    'add': [(1, 2), (-5, 3), (0, 0), (INT_MAX, 0), (INT_MIN, 0), (INT_MAX, INT_MIN), (2**30, 2**30-1), (-2**30, -2**30)],
    'subtract': [(5, 7), (0, 0), (INT_MIN, 0), (-1, INT_MAX), (0, INT_MAX), (INT_MAX, INT_MAX)],
    'multiply': [(6, -7), (0, INT_MIN), (46340, 46340), (-65536, 32768), (INT_MAX, 1), (INT_MAX, -1), (-1, -INT_MAX)],
    'divide': [(7, 2), (-7, 2), (7, -2), (-7, -2), (7, 0), (0, 0), (INT_MIN, 1), (INT_MIN, INT_MAX), (INT_MAX, INT_MIN), (INT_MIN, 2)],
    'multiply_add': [(2, 3, 4), (1000, 1000, -1000000), (0, 0, INT_MIN), (65536, 32767, 65535), (-65536, 32768, 0), (INT_MAX, -1, -1)],
    'power': [(2, 10), (2, 15), (-2, 15), (7, 7), (-3, 3), (46340, 1), (5, 0), (0, 0), (0, -1), (1, -5), (-1, -5), (-1, -4), (2, -1),
              (1, INT_MAX), (-1, INT_MAX), (0, INT_MAX), (-1, INT_MIN)],
    'floored_modulo': [(7, 3), (-7, 3), (7, -3), (-7, -3), (7, 0), (0, 0), (INT_MIN, 3), (INT_MAX, 7), (5, INT_MIN)],
    'absolute': [(-5,), (0,), (INT_MAX,), (INT_MIN+1,), (7,)],
    'minimum': [(INT_MIN, INT_MAX), (-1, 1), (3, 3), (INT_MAX, INT_MIN)],
    'maximum': [(INT_MIN, INT_MAX), (-1, 1), (3, 3), (INT_MAX, INT_MIN)],
    'gcd': [(12, 18), (-12, 18), (0, 5), (0, 0), (17, 5), (INT_MAX, INT_MAX), (2**30, 2**29), (-36, -48)],
    'lcm': [(4, 6), (-4, 6), (21, 6), (0, 5), (0, 0), (46340, 46341), (-6, 4), (2**30, 2)],
    'negate': [(5,), (0,), (INT_MAX,), (-INT_MAX,)],
    'sign': [(-7,), (0,), (INT_MAX,), (INT_MIN,), (1,)],
    'divide_floor': [(7, 2), (-7, 2), (7, -2), (-7, -2), (6, 3), (7, 0), (INT_MIN, 2), (INT_MIN, INT_MAX), (INT_MAX, INT_MIN)],
    'divide_ceil': [(7, 2), (-7, 2), (7, -2), (-7, -2), (6, 3), (7, 0), (INT_MIN, 2), (INT_MAX, 2)],
    'divide_round': [(7, 2), (5, 2), (-5, 2), (5, -2), (7, 0), (1000000, 3), (-1000001, 2), (2**29, 3), (-2**29, -3), (2**30-1, 1)],
    'modulo': [(7, 3), (-7, 3), (7, -3), (7, 0), (0, 0), (INT_MIN, INT_MAX), (INT_MAX, INT_MIN), (INT_MIN, 2)],
}


def integer_rows(cases, strict=True):
    """Table rows for {operation: cases}; with strict, every case must be defined (pinned_integer_math raises otherwise)."""
    rows = []
    for op in [o for o in INTEGER_OPERATIONS if o in cases]:
        cells = []
        for case in cases[op]:
            a, b, c = (list(case)+[0, 0])[:3]
            pinned_integer_math(op, a, b, c, strict=strict)
            cell = {}
            for name, value in zip('abc', (a, b, c)):
                cell[name+'_hi'], cell[name+'_lo'] = operand(value)
            cells.append(cell)
        rows.append(cells)
    return rows


def integer_table(scene_id, description, cases, strict=True):
    return table_scene(scene_id, description, [integer_material(op) for op in INTEGER_OPERATIONS if op in cases],
                       integer_rows(cases, strict), ['D0', 'D1', 'D2', 'D3', 'R'])


BOOLEAN_OPERATIONS = ['and', 'or', 'not', 'nand', 'nor', 'xnor', 'xor', 'imply', 'nimply']
BOOLEAN_CASES = [(0, 0), (0, 1), (1, 0), (1, 1), (2, -3), (-1, 5), (INT_MIN, INT_MAX)]


def boolean_table():
    rows = []
    for op in BOOLEAN_OPERATIONS:
        cells = []
        for a, b in BOOLEAN_CASES:
            cell = {}
            for name, value in zip('ab', (a, b)):
                cell[name+'_hi'], cell[name+'_lo'] = operand(value)
            cells.append(cell)
        rows.append(cells)
    return table_scene('surface-nodes/boolean-math', 'Boolean Math node with linked inputs (from face attributes): every operation on '
                       '(0,0), (0,1), (1,0), (1,1), (2,-3), (-1,5) and (INT_MIN,INT_MAX), the result in the AOV B; one cell per case',
                       [boolean_material(op) for op in BOOLEAN_OPERATIONS], rows, ['B'])


COMPONENT_VECTORS = [[1.5, -2.25, 3.125], [-0.0, 7.0, -0.001], [65504.0, 1e-30, -3.0]]


def binary32(value):
    """The binary32 value nearest to value, unrounded (f32 rounds to seven decimals, which would make 1e-30 zero)."""
    return struct.unpack('<f', struct.pack('<f', value))[0]
COMPONENT_INDICES = [0, 1, 2, -1, 3, INT_MIN, INT_MAX]


def component_table():
    rows = []
    for vector in COMPONENT_VECTORS:
        cells = []
        for index in COMPONENT_INDICES:
            hi, lo = operand(index)
            cells.append({'vec': [binary32(v) for v in vector], 'a_hi': hi, 'a_lo': lo})
        rows.append(cells)
    return table_scene('surface-nodes/get-vector-component', 'Get Vector Component node with linked inputs: three vectors from a face '
                       'attribute and the indices 0, 1, 2, -1, 3, INT_MIN and INT_MAX from face attributes, the component in the AOV V; '
                       'one cell per case', [component_material()], rows, ['V'], vectors=('vec',))


def component_bump_material(handle):
    """Get Vector Component of the position (index from a face attribute) as the height of a Bump node: the compiler picks
    NODE_GET_VECTOR_COMPONENT_DERIVATIVE for a node that feeds a Bump."""
    return shader(handle, [{'handle': 'geo', 'type': 'geometry', 'set': {}}, attribute_node('index', 'index'),
                           {'handle': 'gvc', 'type': 'get_vector_component', 'set': {}},
                           {'handle': 'm', 'type': 'math', 'set': {'math_type': 'multiply', 'value2': 8.0}},
                           {'handle': 'bump', 'type': 'bump', 'set': {'strength': 0.8, 'distance': 0.3}},
                           {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': [0.7, 0.55, 0.4], 'roughness': 0.2}}],
                  [['geo', 'Position', 'gvc', 'Vector'], ['index', 'Fac', 'gvc', 'Index'], ['gvc', 'Value', 'm', 'Value1'],
                   ['m', 'Value', 'bump', 'Height'], ['bump', 'Normal', 'd', 'Normal'], ['d', 'BSDF', 'output', 'Surface']])


def strips(handle, material, indices, x0=-3.6, x1=3.6, y0=-2.0, y1=1.5, z0=1.0, z1=0.4):
    """A tilted panel cut into vertical strips, one face attribute value "index" per strip."""
    positions, faces, data = [], [], []
    step = (x1-x0)/len(indices)
    for k, value in enumerate(indices):
        a, b = x0+k*step, x0+(k+1)*step
        base = len(positions)//3
        positions += [f32(a), y0, z0, f32(b), y0, z0, f32(b), y1, z1, f32(a), y1, z1]
        faces += [base, base+1, base+2, base, base+2, base+3]
        data += [float(value)]*2
    return [{'handle': handle, 'create': 'mesh', 'mesh': {'positions': positions, 'indices': faces,
                                                        'attributes': [{'name': 'index', 'value': 'float', 'domain': 'face', 'data': data}]},
             'set': {'used_shaders': [material]}},
            {'handle': handle+'-object', 'create': 'object', 'set': {'geometry': handle}}]


def missing_constant_material(handle):
    """An Image Texture node with no image: its Missing colour and Missing Alpha (constants) as the colour and the mix."""
    return shader(handle, [{'handle': 'img', 'type': 'image_texture', 'set': {'missing': [0.2, 0.6, 0.9], 'missing_alpha': 0.25}},
                           {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.2}},
                           {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'roughness': 0.3, 'color': [0.8, 0.8, 0.8]}},
                           {'handle': 'm', 'type': 'mix_closure', 'set': {}}],
                  [['img', 'Color', 'd', 'Color'], ['img', 'Alpha', 'm', 'Fac'], ['d', 'BSDF', 'm', 'Closure1'], ['g', 'BSDF', 'm', 'Closure2'],
                   ['m', 'Closure', 'output', 'Surface']])


def missing_linked_material(handle):
    """The same with Missing linked from the Geometry node's Position and Missing Alpha from a vertex attribute "shade"."""
    return shader(handle, [{'handle': 'geo', 'type': 'geometry', 'set': {}}, attribute_node('shade', 'shade'),
                           {'handle': 'img', 'type': 'image_texture', 'set': {}},
                           {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.2}},
                           {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'roughness': 0.3, 'color': [0.8, 0.8, 0.8]}},
                           {'handle': 'm', 'type': 'mix_closure', 'set': {}}],
                  [['geo', 'Position', 'img', 'Missing'], ['shade', 'Fac', 'img', 'Missing Alpha'], ['img', 'Color', 'd', 'Color'],
                   ['img', 'Alpha', 'm', 'Fac'], ['d', 'BSDF', 'm', 'Closure1'], ['g', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']])


def missing_attribute_material(handle):
    """An Attribute node asking for an absent name: its Missing colour and Missing Alpha."""
    return shader(handle, [{'handle': 'attr', 'type': 'attribute', 'set': {'attribute': 'absent', 'missing': [0.9, 0.4, 0.1], 'missing_alpha': 0.6}},
                           {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.2}},
                           {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'roughness': 0.3, 'color': [0.8, 0.8, 0.8]}},
                           {'handle': 'm', 'type': 'mix_closure', 'set': {}}],
                  [['attr', 'Color', 'd', 'Color'], ['attr', 'Alpha', 'm', 'Fac'], ['d', 'BSDF', 'm', 'Closure1'], ['g', 'BSDF', 'm', 'Closure2'],
                   ['m', 'Closure', 'output', 'Surface']])


def image_missing():
    document = scene('surface-nodes/image-missing', 'Image Texture nodes with no image (the Missing and Missing Alpha inputs, constant and '
                     'linked), an Attribute node asking for an absent name (its Missing inputs) and an Environment Texture world with no '
                     'image (its Missing colour)',
                     [missing_constant_material('missing-constant'), missing_linked_material('missing-linked'), missing_attribute_material('missing-attribute')],
                     quad('panel-a', 'missing-constant', [(-3.6, -2, 1), (-1.3, -2, 1), (-1.3, 1.5, 1), (-3.6, 1.5, 1)]) +
                     with_attributes(quad('panel-b', 'missing-linked', [(-1.1, -2, 1), (1.1, -2, 1), (1.1, 1.5, 1), (-1.1, 1.5, 1)]),
                                     [{'name': 'shade', 'value': 'float', 'domain': 'vertex', 'data': [0.0, 0.3, 1.0, 0.6]}]) +
                     quad('panel-c', 'missing-attribute', [(1.3, -2, 1), (3.6, -2, 1), (3.6, 1.5, 1), (1.3, 1.5, 1)]))
    world = shader('world', [{'handle': 'env', 'type': 'environment_texture', 'set': {'missing': [0.3, 0.35, 0.42]}},
                             {'handle': 'b', 'type': 'background_shader', 'set': {'strength': 1.0}}],
                   [['env', 'Color', 'b', 'Color'], ['b', 'Background', 'output', 'Surface']])
    document['nodes'] = [world if node['handle'] == 'world' else node for node in document['nodes']]
    return document


def node_scenes():
    out = {}
    out['integer-math.json'] = integer_table(
        'surface-nodes/integer-math', 'Integer Math node with linked inputs (each operand the sum of two face attributes): every operation '
        'at its edges where the pinned code is defined (division and modulo by zero, power with negative and large exponents, GCD and '
        'LCM with signs and zero, INT_MIN and INT_MAX without overflow); the result in the AOVs D0..D3 (base-256 digits, D3 signed) '
        'and R; one cell per case', INTEGER_CASES)
    out['boolean-math.json'] = boolean_table()
    out['get-vector-component.json'] = component_table()
    out['get-vector-component-bump.json'] = scene(
        'surface-nodes/get-vector-component-bump', 'Get Vector Component of the position (the index 0, 1, 2, -1 or 3 from a face '
        'attribute per strip) times 8 as the height of a Bump node on a tilted panel (the derivative form of the node)',
        [component_bump_material('component-bump')], strips('panel', 'component-bump', [0, 1, 2, -1, 3]))
    out['image-missing.json'] = image_missing()
    return out


def scenes():
    out = {}
    # None of the three attributes is created in standard mode (no "standard" flag): they are plain named attributes.
    out['attribute-standard-name.json'] = scene(
        'surface-nodes/attribute-standard-name',
        'Plain (not standard) attributes whose names are standard attribute names: "color" and "pointiness" read by Attribute nodes, '
        'and a corner float2 "uv" read as the UV map by the Texture Coordinate node',
        [named_like_standard_material('named'), uv_material('mapped')],
        with_attributes(quad('panel-a', 'named', [(-3.6, -2, 1), (-0.2, -2, 1), (-0.2, 1.5, 1), (-3.6, 1.5, 1)]),
                        [{'name': 'color', 'value': 'color', 'domain': 'vertex',
                          'data': [0.9, 0.1, 0.1, 0.1, 0.8, 0.2, 0.2, 0.3, 0.9, 0.8, 0.8, 0.1]},
                         {'name': 'pointiness', 'value': 'float', 'domain': 'vertex', 'data': [0.0, 0.25, 1.0, 0.5]}]) +
        with_attributes(quad('panel-b', 'mapped', [(0.2, -2, 1), (3.6, -2, 1), (3.6, 1.5, 1), (0.2, 1.5, 1)]),
                        [{'name': 'uv', 'value': 'point2', 'domain': 'corner',
                          'data': [0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 1.0]}]))
    out['bevel.json'] = scene('surface-nodes/bevel', 'Bevel node: rounded edges of a box and a smooth sphere, the bevel normal shading a diffuse/glossy mix',
                              [bevel_material('bevel-box', 0.3, 8), bevel_material('bevel-sphere', 0.2, 4)],
                              box('box', 'bevel-box', (-1.4, -1.0, 0.5), (1.6, 1.8, 1.4))+sphere('sphere', 'bevel-sphere', (1.6, -0.9, 0.4), 1.0))
    out['raycast.json'] = scene('surface-nodes/raycast', 'Raycast node: hit distance and hit normal along the shading normal, and an only_local ray into a box',
                                [raycast_material('ray-sphere', False), raycast_material('ray-box', True)],
                                sphere('sphere', 'ray-sphere', (1.6, -0.9, 0.4), 1.0)+box('box', 'ray-box', (-1.4, -1.0, 0.5), (1.6, 1.8, 1.4)))
    out['radial-tiling.json'] = scene('surface-nodes/radial-tiling', 'Radial Tiling node: a regular rounded pentagon (normalized) and an irregular 3.5-gon',
                                      [radial_material('rt-regular', 5.0, 0.3, True), radial_material('rt-irregular', 3.5, 0.6, False)],
                                      quad('panel-a', 'rt-regular', [(-3.6, -2, 1), (-0.2, -2, 1), (-0.2, 1.5, 1), (-3.6, 1.5, 1)])+
                                      quad('panel-b', 'rt-irregular', [(0.2, -2, 1), (3.6, -2, 1), (3.6, 1.5, 1), (0.2, 1.5, 1)]))
    # Every surface carries the Bump: the back wall and the floor (axis-aligned) and a panel tilted about Y, whose dPdu has
    # two non-zero projected components, so the Cramer products of du and dv do not vanish.
    document = scene('surface-nodes/bump-strong-noise', 'Bump node with a strong high-frequency noise height of the position (scale 9, detail 2, '
                     'distance 0.6) on the back wall, the floor and a tilted panel, seen directly and after diffuse bounces with Filter Glossy 0',
                     [strong_bump_material('bumped-a', [0.6, 0.6, 0.6])],
                     quad('panel-a', 'bumped-a', [(-3.6, -2, 1), (-0.2, -2, 0.2), (-0.2, 1.5, 0.2), (-3.6, 1.5, 1)]))
    replaced = {'wall': strong_bump_material('wall', [0.6, 0.62, 0.65]), 'floor-material': strong_bump_material('floor-material', [0.5, 0.45, 0.35])}
    document['nodes'] = [replaced.get(node['handle'], node) for node in document['nodes']]
    document['render'].update(width=48, height=36, samples=16)
    out['bump-strong-noise.json'] = document
    # The same height with its coordinates from an Attribute node (two tilted panels, the wall and the floor): the
    # Attribute node's own first-order bump offset.
    document = scene('surface-nodes/bump-strong-noise-attribute', 'Bump node with a strong high-frequency noise height of a vertex '
                     'attribute read by an Attribute node (scale 9, detail 2, distance 0.6) on the back wall, the floor and two tilted '
                     'panels, seen directly and after diffuse bounces with Filter Glossy 0',
                     [strong_bump_attribute_material('bumped-a', [0.6, 0.6, 0.6])],
                     quad('panel-a', 'bumped-a', [(-3.6, -2, 1), (-0.2, -2, 0.2), (-0.2, 1.5, 0.2), (-3.6, 1.5, 1)]) +
                     quad('panel-b', 'bumped-a', [(0.3, -1.9, 0.4), (3.4, -2, 1.3), (3.1, 1.4, 0.9), (0.2, 1.5, 0.1)]))
    replaced = {'wall': strong_bump_attribute_material('wall', [0.6, 0.62, 0.65]),
                'floor-material': strong_bump_attribute_material('floor-material', [0.5, 0.45, 0.35])}
    document['nodes'] = [replaced.get(node['handle'], node) for node in document['nodes']]
    for node in document['nodes']:
        if node.get('create') == 'mesh':
            node['mesh']['attributes'] = coords_attribute(node['mesh']['positions'])
    document['render'].update(width=48, height=36, samples=16)
    out['bump-strong-noise-attribute.json'] = document
    out.update(node_scenes())
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true', help='Fail when a committed scene differs')
    args = parser.parse_args()
    HERE.mkdir(exist_ok=True)
    for name, document in scenes().items():
        text = json.dumps(document, indent=1)+'\n'
        path = HERE/name
        if args.check:
            if path.read_text() != text:
                raise SystemExit('Scene differs: '+str(path))
        else:
            path.write_text(text)
        print(name, 'ok')


if __name__ == '__main__':
    main()
