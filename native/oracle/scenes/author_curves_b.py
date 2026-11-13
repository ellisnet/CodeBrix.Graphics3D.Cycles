#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the second family of curve oracle scenes (scenes/curves-b/) byte for byte.

The room and tufts of author_curves.py: object and deformation motion blur on every curve shape, radii per motion step,
the motion pass, transparent shadows through baked curve transparency, and scenes that mix curves with caustics,
subsurface scattering, volumes, ambient occlusion, bevel, raycast, displacement and shadow linking.
Usage: python3 native/oracle/scenes/author_curves_b.py [--check]."""
import argparse
import json
import math
from pathlib import Path

import author_curves as curves
import author_sampling as room

HERE = Path(__file__).resolve().parent/'curves-b'
f32 = room.f32
hair = curves.hair
place = curves.place
with_nodes = curves.with_nodes
SHAPES = (('ribbon', 'ribbon'), ('thick', 'thick'), ('linear', 'thick_linear'))


def base(name, description, integrator=None, **kwargs):
    document = curves.base('curves-b/'+name, description, integrator, **kwargs)
    document['sourceFixture'] = 'native/oracle/scenes/author_curves_b.py (curves in the sampling family room)'
    return document


def node(document, handle):
    return next(n for n in document['nodes'] if n['handle'] == handle)


def motion_on(document, shuttertime=0.7):
    node(document, 'integrator')['set']['motion_blur'] = True
    node(document, 'camera')['set']['shuttertime'] = shuttertime


def swung(entry, steps=3, swing=(0.5, 0.15, 0.2), radius=None, wave=0.12):
    """Deformation motion on a hair entry: its keys swing along a direction, and bend with the key index; the centre step
    stays the authored keys. radius scales the radii across the shutter when given."""
    block = entry['hair']
    keys, radii = block['keys'], block['radius']
    centre = (steps-1)//2
    motion_keys, motion_radii = [], []
    for step in range(steps):
        t = 2.0*step/(steps-1)-1.0
        for k in range(len(radii)):
            for axis in range(3):
                value = keys[3*k+axis]
                if step != centre:
                    value = f32(value+t*swing[axis]+wave*t*t*math.sin(1.7*k+axis))
                motion_keys.append(value)
            motion_radii.append(radii[k] if step == centre else f32(radii[k]*(1.0+radius*t)) if radius else radii[k])
    block['motion'] = {'steps': steps, 'keys': motion_keys}
    if radius:
        block['motion']['radius'] = motion_radii
    return entry


def rotation(angle, x=0.0, y=0.0, z=0.0, scale=1.0):
    c, s = f32(math.cos(angle)), f32(math.sin(angle))
    return [f32(c*scale), 0.0, f32(s*scale), x, 0.0, f32(scale), 0.0, y, f32(-s*scale), 0.0, f32(c*scale), z]


def moving(entry, tfm=None, angle=0.12, shift=(0.35, 0.1, 0.12)):
    """Object motion: three transforms around the centre one (a turn and a shift). Moving objects keep their own transform."""
    centre = tfm or rotation(0.0)
    entry['set']['tfm'] = centre
    pre = rotation(-angle, centre[3]-shift[0], centre[7]-shift[1], centre[11]-shift[2])
    post = rotation(angle, centre[3]+shift[0], centre[7]+shift[1], centre[11]+shift[2])
    entry['set']['motion'] = [pre, centre, post]
    return entry


def motion_scenes():
    out = {}
    for name, shape in SHAPES:
        document = base('motion-deform-'+name, 'Deformation motion blur on '+shape.replace('_', ' ')+' curves, three steps')
        motion_on(document)
        out['motion-deform-'+name+'.json'] = with_nodes(document, swung(hair('tuft', 'matte', shape)), place('tuft-object', 'tuft'))
        document = base('motion-radius-'+name, 'Deformation motion blur on '+shape.replace('_', ' ')+' curves, five steps with radii per step')
        motion_on(document)
        out['motion-radius-'+name+'.json'] = with_nodes(document, swung(hair('tuft', 'matte', shape), steps=5, radius=0.5), place('tuft-object', 'tuft'))
        document = base('motion-object-'+name, 'Object motion blur on '+shape.replace('_', ' ')+' curves')
        motion_on(document)
        out['motion-object-'+name+'.json'] = with_nodes(document, hair('tuft', 'matte', shape), moving(place('tuft-object', 'tuft')))
    document = base('motion-mixed-instanced', 'All three shapes: a static tuft, a deforming baked tuft, a moving instance beside a static instance of a deforming tuft')
    motion_on(document)
    out['motion-mixed-instanced.json'] = with_nodes(document,
        hair('ribbons', 'matte', 'ribbon', seed=0.4, curves=6, center=(-2.4, -2, 0.4), spread=0.8), place('ribbons-a', 'ribbons'),
        swung(hair('linear', 'glossy', 'thick_linear', seed=2.3, curves=6, center=(2.4, -2, 0.4), spread=0.8)), place('linear-a', 'linear'),
        swung(hair('thick', 'matte', 'thick', seed=1.1, curves=6, center=(0, -2, 0.4), spread=0.8), steps=5, radius=0.3),
        moving(place('thick-a', 'thick')), place('thick-b', 'thick', [0.9, 0.0, 0.3, 1.2, 0.0, 0.8, 0.0, 0.0, -0.3, 0.0, 0.9, -1.0]))
    document = base('motion-camera-deform', 'Camera motion together with deformation and object motion on curves')
    motion_on(document, 0.5)
    node(document, 'camera')['set']['motion'] = [[1.0, 0.0, 0.0, -0.1, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0], [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0], [1.0, 0.0, 0.0, 0.1, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0]]
    out['motion-camera-deform.json'] = with_nodes(document, swung(hair('tuft', 'matte', 'thick')), place('tuft-object', 'tuft'),
        hair('ribbons', 'glossy', 'ribbon', seed=2.0, center=(-2.2, -2, -0.4), spread=0.7), moving(place('ribbons-object', 'ribbons')))
    # Hair Info's thickness (motion_curve_keys_linear) and the key attributes on deforming curves.
    for name, shape in (('ribbon', 'ribbon'), ('thick', 'thick')):
        base_document = curves.hair_info_scene('motion-info-'+name, shape)
        base_document['id'] = 'curves-b/motion-info-'+name
        base_document['description'] = 'The Hair Info node on deforming '+shape+' curves with radii per step'
        base_document['sourceFixture'] = 'native/oracle/scenes/author_curves_b.py (curves in the sampling family room)'
        motion_on(base_document)
        swung(node(base_document, 'tuft'), radius=0.6)
        out['motion-info-'+name+'.json'] = base_document
    # The motion pass without motion blur: curve_motion_center_location and primitive_motion_position.
    for name, deform, move in (('static', False, False), ('deform', True, False), ('object', False, True), ('both', True, True)):
        document = base('motion-pass-'+name, 'The motion pass without motion blur on thick curves and ribbons ('+name+')')
        node(document, 'integrator')['set']['motion_blur'] = False
        node(document, 'camera')['set'].update({'shuttertime': 0.5, 'motion': [[1.0, 0.0, 0.0, -0.1, 0.0, 1.0, 0.0, 0.05, 0.0, 0.0, 1.0, -7.1], [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0], [1.0, 0.0, 0.0, 0.1, 0.0, 1.0, 0.0, -0.05, 0.0, 0.0, 1.0, -6.9]]})
        tuft = hair('tuft', 'matte', 'thick')
        ribbons = hair('ribbons', 'glossy', 'ribbon', seed=2.0, center=(-2.2, -2, -0.4), spread=0.7)
        if deform:
            swung(tuft)
            swung(ribbons, steps=5)
        tuft_object = place('tuft-object', 'tuft')
        ribbons_object = place('ribbons-object', 'ribbons')
        if move:
            moving(tuft_object)
            moving(ribbons_object, rotation(0.2, 0.1, 0.0, 0.0, 1.1))
        out['motion-pass-'+name+'.json'] = with_nodes(document, tuft, tuft_object, ribbons, ribbons_object,
            {'handle': 'pass-Vector', 'create': 'pass', 'set': {'type': 'motion', 'name': 'Vector'}},
            {'handle': 'pass-Vector Weight', 'create': 'pass', 'set': {'type': 'motion_weight', 'name': 'Vector Weight'}},
            {'handle': 'pass-Depth', 'create': 'pass', 'set': {'type': 'depth', 'name': 'Depth'}})
    return out


def transparent_material(handle='see-through', color=(0.62, 0.35, 0.8), mix=None, checker=False, light_path=False):
    """A Transparent BSDF: a constant colour, a checker over the position (the baked value varies along a strand), the
    Light Path node's depths (the bake kernel's feature mask), with an optional diffuse mix."""
    nodes = [{'handle': 't', 'type': 'transparent_bsdf', 'set': {'color': list(color)}}]
    connect = []
    if checker:
        nodes += [{'handle': 'g', 'type': 'geometry'}, {'handle': 'c', 'type': 'checker_texture', 'set': {'scale': 2.3, 'color1': [0.8, 0.3, 0.6], 'color2': [0.2, 0.9, 0.4]}}]
        connect += [['g', 'Position', 'c', 'Vector'], ['c', 'Color', 't', 'Color']]
    if light_path:
        nodes += [{'handle': 'lp', 'type': 'light_path'}, {'handle': 'rgb', 'type': 'combine_color', 'set': {'color_type': 'rgb'}},
                  {'handle': 'd', 'type': 'math', 'set': {'math_type': 'multiply', 'value2': 0.3}}, {'handle': 'a', 'type': 'math', 'set': {'math_type': 'add', 'value2': 0.25}}]
        connect += [['lp', 'Ray Depth', 'd', 'Value1'], ['lp', 'Transparent Depth', 'a', 'Value1'], ['d', 'Value', 'rgb', 'Red'], ['a', 'Value', 'rgb', 'Green'], ['lp', 'Is Shadow Ray', 'rgb', 'Blue'], ['rgb', 'Color', 't', 'Color']]
    if mix is None:
        connect.append(['t', 'BSDF', 'output', 'Surface'])
    else:
        nodes += [{'handle': 'dd', 'type': 'diffuse_bsdf', 'set': {'color': [0.3, 0.5, 0.7]}}, {'handle': 'm', 'type': 'mix_closure', 'set': {'fac': mix}}]
        connect += [['dd', 'BSDF', 'm', 'Closure1'], ['t', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']]
    return room.shader(handle, nodes, connect)


def shadow_scenes():
    out = {}
    for name, shape in SHAPES:
        out['shadow-'+name+'.json'] = with_nodes(base('shadow-'+name, 'Transparent shadows through '+shape.replace('_', ' ')+' curves: a mix over a checker, baked per key'),
            transparent_material(mix=0.65, checker=True), hair('tuft', 'see-through', shape), place('tuft-object', 'tuft'))
    out['shadow-constant-triangles.json'] = with_nodes(base('shadow-constant-triangles', 'Purely transparent curves and a transparent sphere: baked curve hits beside recorded triangle hits'),
        transparent_material(), hair('tuft', 'see-through', 'thick'), place('tuft-object', 'tuft'),
        hair('ribbons', 'see-through', 'ribbon', seed=2.0, center=(-2.2, -2, -0.4), spread=0.7), place('ribbons-object', 'ribbons'),
        *room.sphere('sphere-t', 'see-through', (1.8, -1.0, 0.4), 0.9))
    out['shadow-light-path.json'] = with_nodes(base('shadow-light-path', 'The Light Path node in baked curve transparency: depths read as the bake kernel reads them'),
        transparent_material(light_path=True), hair('tuft', 'see-through', 'thick'), place('tuft-object', 'tuft'))
    out['shadow-dense.json'] = with_nodes(base('shadow-dense', 'Nearly opaque transparency: the shadow-all opacity cutoff after a few curve hits'),
        transparent_material(color=(0.05, 0.02, 0.03)), hair('tuft', 'see-through', 'thick', curves=24), place('tuft-object', 'tuft'))
    out['shadow-limit.json'] = with_nodes(base('shadow-limit', 'The transparent bounce limit through curves', {'transparent_max_bounce': 2}),
        transparent_material(), hair('tuft', 'see-through', 'thick', curves=24), place('tuft-object', 'tuft'))
    document = base('shadow-motion', 'Transparent shadows through deforming and moving curves')
    motion_on(document)
    out['shadow-motion.json'] = with_nodes(document, transparent_material(mix=0.5), swung(hair('tuft', 'see-through', 'thick')), place('tuft-object', 'tuft'),
        hair('ribbons', 'see-through', 'ribbon', seed=2.0, center=(-2.2, -2, -0.4), spread=0.7), moving(place('ribbons-object', 'ribbons')))
    catcher = with_nodes(base('shadow-catcher', 'Transparent curves over a shadow catcher plane'),
        transparent_material(mix=0.3), hair('tuft', 'see-through', 'thick'), place('tuft-object', 'tuft'),
        {'handle': 'pass-Shadow Catcher', 'create': 'pass', 'set': {'type': 'shadow_catcher', 'name': 'Shadow Catcher'}})
    node(catcher, 'floor-object')['set']['is_shadow_catcher'] = True
    out['shadow-catcher.json'] = catcher
    out['shadow-catcher-on-curves.json'] = with_nodes(base('shadow-catcher-on-curves', 'A hair object as the shadow catcher, under a blocker and a lamp'),
        hair('tuft', 'matte', 'thick'), object_with(place('tuft-object', 'tuft'), is_shadow_catcher=True),
        *room.sphere('blocker', 'glossy', (-0.5, 1.6, -0.5), 0.9),
        {'handle': 'pass-Shadow Catcher', 'create': 'pass', 'set': {'type': 'shadow_catcher', 'name': 'Shadow Catcher'}})
    out['shadow-fast-gi.json'] = with_nodes(base('shadow-fast-gi', 'Fast GI with transparent curves', {'ao_bounces': 1, 'ao_factor': 0.5, 'ao_distance': 2.0}),
        transparent_material(mix=0.5), hair('tuft', 'see-through', 'thick'), place('tuft-object', 'tuft'))
    return out


def derived(name, source, description, *entries, resolution=None):
    """Another family's scene with curves added: its nodes unchanged, its own id."""
    document = json.loads((HERE.parent/(source+'.json')).read_text())
    document['id'] = 'curves-b/'+name
    document['description'] = description
    document['sourceFixture'] = 'native/oracle/scenes/author_curves_b.py (scenes/'+source+'.json with curves)'
    if resolution:
        document['render'].update({'width': resolution[0], 'height': resolution[1]})
    return with_nodes(document, *entries)


def matte():
    return room.single('hair-matte', 'diffuse_bsdf', {'color': [0.75, 0.5, 0.3], 'roughness': 0.3})


def object_with(entry, **values):
    entry['set'].update(values)
    return entry


def mixed_scenes():
    out = {}
    size = (64, 48)
    # Caustics: curves receiving, blocking and refused as casters (mnee.h rejects a caster that is not a triangle mesh).
    out['caustics-receiver.json'] = derived('caustics-receiver', 'mnee/glass-sphere-caustic', 'Caustics on curves: a tuft receiving the glass sphere caustic', matte(),
        hair('tuft', 'hair-matte', 'thick', center=(-1.4, -1.0, -1.2), spread=0.9, height=1.6), object_with(place('tuft-object', 'tuft'), is_caustics_receiver=True), resolution=size)
    out['caustics-blocker.json'] = derived('caustics-blocker', 'mnee/glass-sphere-caustic', 'Curves between the receivers, the glass caster and the lamp: ignored by the probe rays, blocking the solved path', matte(),
        hair('tuft', 'hair-matte', 'ribbon', center=(0.6, 0.2, -0.6), spread=1.2, height=1.4), place('tuft-object', 'tuft'), resolution=size)
    glass = room.single('hair-glass', 'glass_bsdf', {'distribution': 'ggx', 'roughness': 0.0, 'IOR': 1.45, 'color': [0.95, 0.97, 1.0]})
    out['caustics-caster-rejected.json'] = derived('caustics-caster-rejected', 'mnee/glass-sphere-caustic', 'A curve caster: the kernel rejects a caster that is not a triangle mesh', glass,
        hair('tuft', 'hair-glass', 'thick', center=(0.8, -0.6, -1.0), spread=0.9, height=1.8), object_with(place('tuft-object', 'tuft'), is_caustics_caster=True), resolution=size)
    out['caustics-tree.json'] = derived('caustics-tree', 'mnee/light-tree-caustic', 'Caustics with the light tree and curves in the scene', matte(),
        hair('tuft', 'hair-matte', 'thick_linear', center=(-1.4, -1.0, -1.2), spread=0.9, height=1.6), object_with(place('tuft-object', 'tuft'), is_caustics_receiver=True), resolution=size)
    # Subsurface: the walk's local traversal passes curves over; a curve with a BSSRDF walks and finds no triangle.
    out['subsurface-beside.json'] = derived('subsurface-beside', 'subsurface/node-methods', 'Subsurface scattering on spheres beside a tuft: unaligned nodes and curve leaves in the local traversal', matte(),
        hair('tuft', 'hair-matte', 'thick', center=(0.6, -2, -1.6), spread=1.0), place('tuft-object', 'tuft'), resolution=size)
    out['subsurface-on-curves.json'] = derived('subsurface-on-curves', 'subsurface/node-methods', 'A subsurface closure on thick curves: the random walk has no triangle to find',
        hair('tuft', 'sss', 'thick', center=(0.6, -2, -1.6), spread=1.0), place('tuft-object', 'tuft'), hair('ribbons', 'sss-legacy', 'ribbon', seed=1.3, center=(-2.0, -2, -1.6), spread=0.7), place('ribbons-object', 'ribbons'), resolution=size)
    # Volumes: curves inside, through and behind media; the volume stack's traversal passes curve leaves over.
    out['volume-absorption.json'] = derived('volume-absorption', 'volumes/absorption-point', 'An absorbing box with a tuft in and behind it: shadows through the medium and the curves', matte(),
        hair('tuft', 'hair-matte', 'thick'), place('tuft-object', 'tuft'), resolution=size)
    out['volume-scatter.json'] = derived('volume-scatter', 'volumes/scatter-point', 'A scattering box with ribbons in it and light through both', matte(),
        hair('tuft', 'hair-matte', 'ribbon'), place('tuft-object', 'tuft'), resolution=size)
    out['volume-heterogeneous.json'] = derived('volume-heterogeneous', 'volumes/heterogeneous-point', 'A heterogeneous medium with transparent curves: baked curve hits on the shadow path through it', transparent_material(mix=0.5),
        hair('tuft', 'see-through', 'thick'), place('tuft-object', 'tuft'), resolution=size)
    out['volume-ray-marching.json'] = derived('volume-ray-marching', 'volumes/ray-marching-homogeneous-point', 'Ray marching with curves in the medium', matte(),
        hair('tuft', 'hair-matte', 'thick_linear'), place('tuft-object', 'tuft'), resolution=size)
    # The Ambient Occlusion, Bevel and Raycast nodes: their traversals over curves, and the nodes on curves.
    out['ao-beside.json'] = derived('ao-beside', 'ao/ao-node', 'The Ambient Occlusion node on triangles with curves as occluders', matte(),
        hair('tuft', 'hair-matte', 'thick', center=(0.2, -2, -1.2), spread=1.2), place('tuft-object', 'tuft'), resolution=size)
    # The only-local ribbons are instanced (two objects): a local traversal of a transform-applied object from the scene's root
    # in a scene with instances is refused on this device for any primitive (Found in the lane report).
    out['ao-on-curves.json'] = derived('ao-on-curves', 'ao/ao-node', 'The Ambient Occlusion node on curves, global and only local',
        hair('tuft', 'ao-global', 'thick', center=(0.2, -2, -1.2), spread=1.2), place('tuft-object', 'tuft'), hair('ribbons', 'ao-local', 'ribbon', seed=1.3, center=(-2.0, -2, -1.2), spread=0.7),
        place('ribbons-object', 'ribbons'), place('ribbons-object-2', 'ribbons', [1.0, 0.0, 0.0, 0.6, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.8]), resolution=size)
    out['bevel-beside.json'] = derived('bevel-beside', 'surface-nodes/bevel', 'The Bevel node on triangles with curves in the same BVH', matte(),
        hair('tuft', 'hair-matte', 'thick', center=(0.2, -2, -1.2), spread=1.2), place('tuft-object', 'tuft'), resolution=size)
    out['bevel-on-curves.json'] = derived('bevel-on-curves', 'surface-nodes/bevel', 'The Bevel node on curves: a local traversal with no triangles',
        hair('tuft', 'bevel-box', 'thick', center=(0.2, -2, -1.2), spread=1.2), place('tuft-object', 'tuft'), resolution=size)
    out['raycast-beside.json'] = derived('raycast-beside', 'surface-nodes/raycast', 'The Raycast node on triangles, its rays hitting curves: their shader data and attributes', matte(),
        hair('tuft', 'hair-matte', 'thick', center=(0.2, -2, -1.2), spread=1.6, curves=24), place('tuft-object', 'tuft'), hair('ribbons', 'hair-matte', 'ribbon', seed=1.3, center=(-1.5, -2, 0.4), spread=1.2, curves=24), place('ribbons-object', 'ribbons'), resolution=size)
    # True displacement of a mesh beside curves, and a displacement shader on curves (no displacement evaluation for hair).
    out['displacement-beside.json'] = derived('displacement-beside', 'displacement/true-height', 'A truly displaced mesh with a tuft in the scene', matte(),
        hair('tuft', 'hair-matte', 'thick'), place('tuft-object', 'tuft'), resolution=size)
    out['displacement-on-curves.json'] = derived('displacement-on-curves', 'displacement/true-height', 'A true displacement shader on curves: hair is not displaced (Geometry::has_true_displacement only keeps it unbaked)',
        hair('tuft', 'object-height', 'thick'), place('tuft-object', 'tuft'), resolution=size)
    # Shadow linking: curves as blockers of a set, and emissive curves the dedicated light kernels pick.
    out['linking-blockers.json'] = derived('linking-blockers', 'shadow-linking/blockers', 'Shadow linking with curves as blockers of set 1, and shadow rays past them', matte(),
        hair('tuft', 'hair-matte', 'thick', center=(0.6, -1.0, 1.6), spread=1.0, height=2.0), object_with(place('tuft-object', 'tuft'), blocker_shadow_set=1),
        hair('ribbons', 'hair-matte', 'ribbon', seed=1.3, center=(-1.4, -1.0, 2.2), spread=0.8, height=1.8), place('ribbons-object', 'ribbons'))
    out['linking-emissive.json'] = derived('linking-emissive', 'shadow-linking/blockers', 'Emissive curves in a shadow set: the dedicated light kernels pick and shade them, with no forward MIS on a curve',
        hair('tuft', 'panel-emission', 'thick', center=(0.6, -1.0, 1.6), spread=1.0, height=2.0), object_with(place('tuft-object', 'tuft'), shadow_set_membership=5),
        hair('ribbons', 'panel-emission', 'ribbon', seed=1.3, center=(-1.4, -1.0, 2.2), spread=0.8, height=1.8), object_with(place('ribbons-object', 'ribbons'), shadow_set_membership=3))
    # Emission that is not constant on the curves: the dedicated shadow path evaluates the shader at the picked curve hit (its u and v).
    glow = room.shader('curve-glow', [{'handle': 'g', 'type': 'geometry'}, {'handle': 'a', 'type': 'vector_math', 'set': {'math_type': 'absolute'}}, {'handle': 'e', 'type': 'emission', 'set': {'strength': 4.0}}],
        [['g', 'Parametric', 'a', 'Vector1'], ['a', 'Vector', 'e', 'Color'], ['e', 'Emission', 'output', 'Surface']])
    out['linking-emissive-textured.json'] = derived('linking-emissive-textured', 'shadow-linking/blockers', 'Curves in a shadow set whose emission follows the hit\'s parametric coordinates: the dedicated light kernels pick them and the light shader is evaluated at the picked hit', glow,
        hair('tuft', 'curve-glow', 'thick', center=(0.6, -1.0, 1.6), spread=1.0, height=2.0), object_with(place('tuft-object', 'tuft'), shadow_set_membership=5),
        hair('ribbons', 'curve-glow', 'ribbon', seed=1.3, center=(-1.4, -1.0, 2.2), spread=0.8, height=1.8), object_with(place('ribbons-object', 'ribbons'), shadow_set_membership=3))
    out['linking-tree.json'] = derived('linking-tree', 'shadow-linking/light-tree', 'Shadow linking with the light tree and curves as blockers', matte(),
        hair('tuft', 'hair-matte', 'thick', center=(0.6, -1.0, 1.6), spread=1.0, height=2.0), object_with(place('tuft-object', 'tuft'), blocker_shadow_set=1))
    return out


def scenes():
    out = {}
    out.update(motion_scenes())
    out.update(shadow_scenes())
    out.update(mixed_scenes())
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
