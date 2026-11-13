#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the feature-combination coverage scenes (scenes/combinations/cov-*.json) byte for byte.

Pairs of ported features that no other committed scene images together: texture baking with curves, points, object and
deformation motion, a volume, MNEE caustics, subsurface scattering, the light tree, light linking and shadow linking; object and
deformation motion with shadow linking and with the shadow catcher; subsurface scattering (random walk, Christensen-Burley, skin)
with the light tree; light linking with the shadow catcher. Each scene is the sampling family's room (author_sampling.py) or the
bake family's room (author_bake.py) with the two features added, at the corpus's usual size and sample count; the bake target is
wound outwards (author_bake.uv_sphere winds it inwards, and a bake views the side its normal points to).
Then scenes that each hold several of the remaining pairs (second_tier: volume kinds, camera motion, adaptive sampling, panoramic
and stereo cameras, true displacement, light groups, cryptomatte and the AO node with the ported features), subsurface, glass and
caustics on spheres wound outwards (outward_tier), and the surface lamp sampler next to near-delta GGX lobes
(cov-surface-lamp-near-delta).
Usage: python3 native/oracle/scenes/author_coverage.py [--check]."""
import argparse
import json
from pathlib import Path

import author_bake as bake
import author_curves as curves
import author_points as points
import author_sampling as room
import author_subsurface as subsurface
import author_subsurface_combinations as sscomb

HERE = Path(__file__).resolve().parent/'combinations'
SOURCE = 'native/oracle/scenes/author_coverage.py ({})'


def node(document, handle):
    return next(n for n in document['nodes'] if n['handle'] == handle)


def edit(document, handle, values):
    node(document, handle)['set'].update(values)
    return document


def replace_material(document, mesh, material):
    node(document, mesh)['set']['used_shaders'] = [material]
    return document


def add_shaders(document, *shaders):
    """Shader entries placed before the first mesh (entries are applied in order, so a mesh names only shaders already made)."""
    at = next(i for i, n in enumerate(document['nodes']) if n.get('create') == 'mesh')
    document['nodes'][at:at] = list(shaders)
    return document


def lamp_set(document, extra):
    """The second (fill) lamp of the subsurface combinations, with its object sockets."""
    document['nodes'] += sscomb.fill_lamp(extra)
    return document


def tree_lamps(spot_extra=None, area_extra=None):
    """A spot lamp and an area lamp beside the room's sphere lamp (several emitters, so the light tree has a hierarchy)."""
    return [room.shader('spot-shader', [{'handle': 'e', 'type': 'emission', 'set': {'color': [1, 1, 1], 'strength': 1}}], [['e', 'Emission', 'output', 'Surface']]),
            {'handle': 'spot', 'create': 'spotlight', 'set': {'strength': [80, 80, 90], 'use_mis': True, 'cast_shadow': True, 'max_bounces': 1024, 'is_enabled': True,
                                                              'normalize': True, 'used_shaders': ['spot-shader'], 'radius': 0.2, 'is_sphere': True, 'angle': 1.0,
                                                              'smooth': 0.3}},
            {'handle': 'spot-object', 'create': 'object', 'set': {'geometry': 'spot', 'tfm': [1, 0, 0, 0.8, 0, 0, 1, 2.8, 0, -1, 0, 1.0], **(spot_extra or {})}},
            room.shader('area-shader', [{'handle': 'e', 'type': 'emission', 'set': {'color': [1, 1, 1], 'strength': 1}}], [['e', 'Emission', 'output', 'Surface']]),
            {'handle': 'area', 'create': 'arealight', 'set': {'strength': [40, 50, 60], 'use_mis': True, 'cast_shadow': True, 'max_bounces': 1024, 'is_enabled': True,
                                                              'normalize': True, 'used_shaders': ['area-shader'], 'sizeu': 1.0, 'sizev': 1.0, 'ellipse': False,
                                                              'spread': 1.2, 'is_portal': False}},
            {'handle': 'area-object', 'create': 'object', 'set': {'geometry': 'area', 'tfm': [1, 0, 0, -2.5, 0, 0, 1, 2.5, 0, -1, 0, -1.0], **(area_extra or {})}}]


def walk_node(handle='sss-walk', **values):
    return subsurface.node(handle, {'subsurface_anisotropy': 0.3, **values})


def burley_node(handle='sss-burley'):
    return subsurface.node(handle, {'method': 'burley', 'scale': 0.6})


def skin_node(handle='sss-skin'):
    return subsurface.node(handle, {'method': 'random_walk_skin', 'scale': 0.5})


def outward(document, handle):
    """Reverse the winding of every triangle of a corner-UV mesh (corners 1 and 2 swapped, with their UVs): author_bake.uv_sphere winds its
    triangles so that the geometric normal points into the sphere, and a bake views a surface from its normal's side, so an opaque target
    would bake its unlit inside."""
    mesh = node(document, handle)['mesh']; idx = mesh['indices']; uv = mesh['attributes'][0]['data']
    for t in range(len(idx)//3):
        idx[3*t+1], idx[3*t+2] = idx[3*t+2], idx[3*t+1]
        uv[6*t+2:6*t+4], uv[6*t+4:6*t+6] = uv[6*t+4:6*t+6], uv[6*t+2:6*t+4]
    return document


def bake_combined(name, description, what, integrator=None, extra=(), samples=16):
    """A combined bake of the bake family's ball, wound outwards (see outward)."""
    document = bake.bake_scene('combinations/'+name, description, {'type': 'combined'}, integrator=integrator, extra=extra, samples=samples)
    document['sourceFixture'] = SOURCE.format(what)
    return outward(document, 'ball')


def room_scene(name, description, what, integrator=None, samples=16):
    document = room.scene('combinations/'+name, description, integrator or {}, samples=samples)
    document['sourceFixture'] = SOURCE.format(what)
    return document


def catcher_film(document):
    """A transparent world, the exact shadow catcher and its three passes."""
    edit(document, 'bg', {'transparent': True})
    edit(document, 'film', {'use_approximate_shadow_catcher': False})
    document['nodes'] += [{'handle': 'pass-catcher', 'create': 'pass', 'set': {'type': 'shadow_catcher', 'name': 'Shadow Catcher'}},
                          {'handle': 'pass-matte', 'create': 'pass', 'set': {'type': 'shadow_catcher_matte', 'name': 'Shadow Catcher Matte'}},
                          {'handle': 'pass-count', 'create': 'pass', 'set': {'type': 'shadow_catcher_sample_count', 'name': 'Shadow Catcher Sample Count'}}]
    return document


def scenes():
    out = {}
    # Baking with each of the other named features (the bake family's room: the UV-mapped, textured ball in the middle).
    out['cov-bake-curves'] = bake_combined(
        'cov-bake-curves', 'Combined bake of the ball beside a tuft of thick curves (shadows and indirect light from curves in UV space)', 'baking with curves',
        extra=[curves.hair('tuft', 'matte', 'thick', center=(-1.5, -2, -0.2), spread=0.6, height=2.6), curves.place('tuft-object', 'tuft')])
    out['cov-bake-points'] = bake_combined(
        'cov-bake-points', 'Combined bake of the ball beside a ball of points (shadows and indirect light from points in UV space)', 'baking with points',
        extra=[points.cloud('cloud', 'glossy', count=40, center=(1.5, 0.6, -0.6), spread=0.7, radius=0.18), points.place('cloud-object', 'cloud')])
    document = bake_combined('cov-bake-motion', 'Combined bake of the static ball with motion blur on: the left sphere moves (object motion) and the right one '
                             'deforms (motion triangles); bake rays sample both at the bake time', 'baking with object and deformation motion',
                             integrator={'motion_blur': True})
    edit(document, 'sphere-0-object', {'motion': [sscomb.moving(0.92, (-0.25, 0.4, 0.0)), sscomb.moving(1.06, (0.25, -0.1, 0.1))]})
    sscomb.deform(document, 'sphere-2', 0.08)
    out['cov-bake-motion'] = document
    out['cov-bake-volume'] = bake_combined(
        'cov-bake-volume', 'Combined bake of the ball wholly inside an absorbing and scattering box (the bake rays start in the medium)', 'baking in a volume',
        integrator={'max_volume_bounce': 4}, extra=sscomb.medium_box('medium', (-1.4, -1.95, -1.0), (1.4, 1.3, 1.8), 0.15, 0.3))
    document = bake_combined('cov-bake-caustics', 'Combined bake of the ball as a caustic receiver: the left sphere is a glass caustic caster and the lamp '
                             'casts caustics (MNEE from the bake rays)', 'baking with MNEE caustics')
    replace_material(document, 'sphere-0', 'glass')
    edit(edit(edit(document, 'sphere-0-object', {'is_caustics_caster': True}), 'ball-object', {'is_caustics_receiver': True}), 'lamp', {'use_caustics': True})
    out['cov-bake-caustics'] = document
    document = bake_combined('cov-bake-subsurface', 'Combined bake of a random walk subsurface ball beside a Christensen-Burley right sphere (subsurface '
                             'exit points from the bake rays)', 'baking with subsurface scattering')
    add_shaders(document, walk_node(), burley_node())
    replace_material(replace_material(document, 'ball', 'sss-walk'), 'sphere-2', 'sss-burley')
    out['cov-bake-subsurface'] = document
    out['cov-bake-light-tree'] = bake_combined(
        'cov-bake-light-tree', 'Combined bake of the ball lit by a sphere, a spot and an area lamp through the light tree', 'baking with the light tree',
        integrator={'use_light_tree': True}, extra=tree_lamps())
    document = bake_combined('cov-bake-light-linking', 'Combined bake of the ball in receiver set 1: the warm lamp links sets 0 and 1, the cool fill lamp '
                             'sets 0 and 2, so the ball sees only the warm lamp and the spheres both', 'baking with light linking')
    lamp_set(document, {'light_set_membership': 0b101})
    edit(edit(document, 'lamp-object', {'light_set_membership': 0b011}), 'ball-object', {'receiver_light_set': 1})
    out['cov-bake-light-linking'] = document
    document = bake_combined('cov-bake-shadow-linking', 'Combined bake of the ball: the right sphere (blocker set 1) casts no shadow from the warm lamp, '
                             'which only sets 0 and 2 block, and the left sphere (blocker set 2) none from the cool fill lamp, which sets 0 and 1 block',
                             'baking with shadow linking')
    lamp_set(document, {'shadow_set_membership': 0b011})
    edit(edit(edit(document, 'lamp-object', {'shadow_set_membership': 0b101}), 'sphere-2-object', {'blocker_shadow_set': 1}), 'sphere-0-object',
         {'blocker_shadow_set': 2})
    out['cov-bake-shadow-linking'] = document
    # Motion with shadow linking and with the shadow catcher.
    document = room_scene('cov-motion-shadow-linking', 'Shadow linking with motion blur: the moving left sphere (blocker set 1) casts no shadow from the warm '
                          'lamp, the deforming right sphere (blocker set 2) none from the cool fill lamp', 'shadow linking with object and deformation motion',
                          integrator={'motion_blur': True})
    lamp_set(document, {'shadow_set_membership': 0b011})
    edit(edit(edit(document, 'lamp-object', {'shadow_set_membership': 0b101}), 'sphere-0-object', {'blocker_shadow_set': 1}), 'sphere-2-object',
         {'blocker_shadow_set': 2})
    edit(document, 'sphere-0-object', {'motion': [sscomb.moving(0.92, (-0.25, 0.3, 0.0)), sscomb.moving(1.06, (0.25, -0.1, 0.1))]})
    sscomb.deform(document, 'sphere-2', 0.08)
    out['cov-motion-shadow-linking'] = document
    document = catcher_film(room_scene('cov-motion-shadow-catcher', 'A shadow catcher floor under a moving left sphere (object motion) and a deforming right '
                                       'sphere (motion triangles), with a transparent world and the three shadow catcher passes',
                                       'the shadow catcher with object and deformation motion', integrator={'motion_blur': True}))
    edit(document, 'floor-object', {'is_shadow_catcher': True})
    edit(document, 'sphere-0-object', {'motion': [sscomb.moving(0.92, (-0.25, 0.3, 0.0)), sscomb.moving(1.06, (0.25, -0.1, 0.1))]})
    sscomb.deform(document, 'sphere-2', 0.08)
    out['cov-motion-shadow-catcher'] = document
    # Subsurface scattering (three methods) with the light tree.
    document = room_scene('cov-subsurface-light-tree', 'Skin (left), Christensen-Burley (middle) and random walk (right) subsurface spheres lit by a sphere, '
                          'a spot and an area lamp through the light tree (the exit points select lights from the tree)', 'subsurface scattering with the light tree',
                          integrator={'use_light_tree': True})
    add_shaders(document, skin_node(), burley_node(), walk_node())
    document['nodes'] += tree_lamps()
    replace_material(replace_material(replace_material(document, 'sphere-0', 'sss-skin'), 'sphere-1', 'sss-burley'), 'sphere-2', 'sss-walk')
    out['cov-subsurface-light-tree'] = document
    # Light linking with the shadow catcher.
    document = catcher_film(room_scene('cov-light-linking-shadow-catcher', 'A shadow catcher floor in receiver set 1: the warm lamp links sets 0 and 1, the '
                                       'cool fill lamp sets 0 and 2, so the floor catches only the warm lamp\'s shadows; the spheres see both lamps',
                                       'light linking with the shadow catcher'))
    lamp_set(document, {'light_set_membership': 0b101})
    edit(edit(edit(document, 'lamp-object', {'light_set_membership': 0b011}), 'floor-object', {'is_shadow_catcher': True, 'receiver_light_set': 1}),
         'sphere-0-object', {'receiver_light_set': 2})
    out['cov-light-linking-shadow-catcher'] = document
    # The surface lamp sampler near a delta lobe (the surface counterpart of the volumes' g = 0.999 class): near-mirror GGX
    # surfaces (roughness 0.03 and 0.05: alpha about 1e-3, a lobe as narrow as the 0.999 phase function's) beside a sphere lamp and a
    # spherical spot lamp a quarter of a unit from them, so the sampled direction's last bits move the BSDF value at the lobe's edge.
    document = room_scene('cov-surface-lamp-near-delta', 'Near-mirror GGX floor and middle sphere (roughness 0.03) and right sphere (0.05) with a small sphere '
                          'lamp a quarter of a unit above the floor and beside the middle sphere, and a spherical spot lamp just above the right sphere: '
                          'the surface lamp sampler near a delta lobe', 'the surface lamp sampler close to near-delta lobes', samples=16)
    document['render'].update({'width': 64, 'height': 48})
    add_shaders(document, room.single('mirror', 'glossy_bsdf', {'distribution': 'ggx', 'roughness': 0.03, 'color': [0.85, 0.85, 0.8]}),
                room.single('mirror-soft', 'glossy_bsdf', {'distribution': 'ggx', 'roughness': 0.05, 'color': [0.8, 0.7, 0.5]}))
    document['nodes'] += [room.shader('near-emitter', [{'handle': 'e', 'type': 'emission', 'set': {'color': [1, 1, 1], 'strength': 1}}], [['e', 'Emission', 'output', 'Surface']]),
                          {'handle': 'near', 'create': 'pointlight', 'set': {'radius': 0.2, 'is_sphere': True, 'use_mis': True, 'strength': [30, 28, 24],
                                                                             'used_shaders': ['near-emitter']}},
                          {'handle': 'near-object', 'create': 'object', 'set': {'geometry': 'near', 'tfm': [1.0, 0.0, 0.0, 1.15, 0.0, 1.0, 0.0, -1.55, 0.0, 0.0, 1.0, -0.2]}},
                          room.shader('cap-shader', [{'handle': 'e', 'type': 'emission', 'set': {'color': [1, 1, 1], 'strength': 1}}], [['e', 'Emission', 'output', 'Surface']]),
                          {'handle': 'cap', 'create': 'spotlight', 'set': {'strength': [25, 30, 35], 'use_mis': True, 'cast_shadow': True, 'max_bounces': 1024,
                                                                           'is_enabled': True, 'normalize': True, 'used_shaders': ['cap-shader'], 'radius': 0.15,
                                                                           'is_sphere': True, 'angle': 1.2, 'smooth': 0.2}},
                          {'handle': 'cap-object', 'create': 'object', 'set': {'geometry': 'cap', 'tfm': [1, 0, 0, 2.3, 0, 0, 1, 0.45, 0, -1, 0, 0.4]}}]
    replace_material(replace_material(replace_material(document, 'floor', 'mirror'), 'sphere-1', 'mirror'), 'sphere-2', 'mirror-soft')
    out['cov-surface-lamp-near-delta'] = document
    # The same at the narrowest lobes the pinned GGX still evaluates (alpha_x * alpha_y above 2e-10, roughness_is_almost_specular):
    # roughness 0.006 and 0.01, alpha 3.6e-5 and 1e-4.
    narrow = json.loads(json.dumps(document))
    narrow['id'] = 'combinations/cov-surface-lamp-near-delta-narrow'
    narrow['description'] = document['description'].replace('(roughness 0.03)', '(roughness 0.006)').replace('(0.05)', '(0.01)')+', at the narrowest non-singular lobes'
    node(narrow, 'mirror')['graph']['nodes'][0]['set']['roughness'] = 0.006
    node(narrow, 'mirror-soft')['graph']['nodes'][0]['set']['roughness'] = 0.01
    out['cov-surface-lamp-near-delta-narrow'] = narrow
    out.update(second_tier())
    return {name+'.json': document for name, document in out.items()}


def noise_box(handle, lo, hi, scale=1.6, density=0.8):
    """sscomb.medium_box's box with the heterogeneous medium of author_volumes.noise_medium (scatter density from a Noise Texture)."""
    entries = sscomb.medium_box(handle, lo, hi, 0.05, 0.3)
    entries[0]['graph'] = {'nodes': [{'handle': 'n', 'type': 'noise_texture', 'set': {'scale': scale, 'detail': 2.0}},
                                     {'handle': 'm', 'type': 'math', 'set': {'math_type': 'multiply', 'value2': density}},
                                     {'handle': 'a', 'type': 'absorption_volume', 'set': {'density': 0.05, 'color': [0.8, 0.65, 0.4]}},
                                     {'handle': 's', 'type': 'scatter_volume', 'set': {'color': [0.75, 0.8, 0.9], 'anisotropy': 0.3}},
                                     {'handle': 'add', 'type': 'add_closure'}],
                           'connect': [['n', 'Fac', 'm', 'Value1'], ['m', 'Value', 's', 'Density'], ['a', 'Volume', 'add', 'Closure1'],
                                       ['s', 'Volume', 'add', 'Closure2'], ['add', 'Closure', 'output', 'Volume']]}
    return entries


def world_volume(document, density=0.03):
    """A scattering world volume in the room's constant world (author_volumes' world-volume scenes)."""
    world = node(document, 'world')
    world['graph']['nodes'].append({'handle': 'wv', 'type': 'scatter_volume', 'set': {'density': density, 'color': [0.8, 0.8, 0.8], 'anisotropy': 0.2}})
    world['graph']['connect'].append(['wv', 'Volume', 'output', 'Volume'])
    edit(document, 'integrator', {'max_volume_bounce': 4})
    return document


def caustics(document, caster='sphere-0', receivers=('floor-object', 'sphere-1-object', 'sphere-2-object')):
    replace_material(document, caster, 'glass')
    edit(document, caster+'-object', {'is_caustics_caster': True})
    for r in receivers:
        edit(document, r, {'is_caustics_receiver': True})
    return edit(document, 'lamp', {'use_caustics': True})


def subsurface_pair(document, middle=None, right=None):
    """Christensen-Burley middle sphere and random walk right sphere (or the given shaders)."""
    middle = middle or burley_node(); right = right or walk_node()
    add_shaders(document, middle, right)
    return replace_material(replace_material(document, 'sphere-1', middle['handle']), 'sphere-2', right['handle'])


def object_motion(document, handle='sphere-0'):
    edit(document, 'integrator', {'motion_blur': True})
    return edit(document, handle+'-object', {'motion': [sscomb.moving(0.92, (-0.25, 0.3, 0.0)), sscomb.moving(1.06, (0.25, -0.1, 0.1))]})


def camera_motion(document):
    """A camera dolly and pan through the shutter (three transforms, the centre one the camera's own)."""
    edit(document, 'integrator', {'motion_blur': True})
    edit(document, 'camera', {'shuttertime': 0.6, 'motion': [[1.0, 0.0, 0.0, -0.25, 0.0, 1.0, 0.0, 0.08, 0.0, 0.0, 1.0, -7.2],
                                                          [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0],
                                                          [1.0, 0.0, 0.0, 0.25, 0.0, 1.0, 0.0, -0.05, 0.0, 0.0, 1.0, -6.8]]})
    return document


def linking(document, light=True, shadow=True):
    """The fill lamp and the two-lamp light and shadow sets of the subsurface combinations (light: middle sphere in receiver set 1, right in set 2;
    shadow: left sphere in blocker set 1, right in set 2)."""
    extra = {}
    if light: extra['light_set_membership'] = 0b101
    if shadow: extra['shadow_set_membership'] = 0b011
    lamp_set(document, extra)
    if light:
        edit(document, 'lamp-object', {'light_set_membership': 0b011})
        edit(edit(document, 'sphere-1-object', {'receiver_light_set': 1}), 'sphere-2-object', {'receiver_light_set': 2})
    if shadow:
        edit(document, 'lamp-object', {'shadow_set_membership': 0b101})
        edit(edit(document, 'sphere-0-object', {'blocker_shadow_set': 1}), 'sphere-2-object', {'blocker_shadow_set': 2})
    return document


def pass_entries(kinds):
    return [{'handle': 'pass-'+name, 'create': 'pass', 'set': {'type': kind, 'name': name, **extra}} for kind, name, extra in kinds]


def second_tier():
    """Combinations beyond the named pairs, each scene several of the remaining pairs a consumer plausibly authors together: volume kinds with
    subsurface, caustics and linking; camera motion with subsurface, caustics, volumes and linking; adaptive sampling, panoramic and stereo cameras,
    true displacement, light groups, cryptomatte and the AO node with the ported features."""
    out = {}
    d = room_scene('cov-heterogeneous-subsurface-caustics', 'A heterogeneous (noise) medium box around the Christensen-Burley (middle) and random walk '
                   '(right) spheres, with the left sphere a glass caustic caster and the floor and spheres caustic receivers',
                   'a heterogeneous volume with subsurface scattering and MNEE caustics', integrator={'max_volume_bounce': 4})
    subsurface_pair(d); caustics(d); d['nodes'] += noise_box('medium', (-1.2, -1.95, -0.8), (3.5, 0.3, 1.6))
    out['cov-heterogeneous-subsurface-caustics'] = d
    d = room_scene('cov-world-volume-linking', 'A scattering world volume with light linking (middle and right spheres in receiver sets 1 and 2) and shadow '
                   'linking (left and right spheres in blocker sets 1 and 2) between the warm lamp and the cool fill lamp', 'a world volume with light and shadow linking')
    world_volume(d); linking(d)
    out['cov-world-volume-linking'] = d
    d = room_scene('cov-world-volume-subsurface-caustics', 'A scattering world volume with Christensen-Burley (middle) and random walk (right) spheres that '
                   'receive caustics from the glass left sphere', 'a world volume with subsurface scattering and MNEE caustics')
    world_volume(d); subsurface_pair(d); caustics(d)
    out['cov-world-volume-subsurface-caustics'] = d
    d = room_scene('cov-camera-motion-subsurface-caustics', 'Camera motion blur (a dolly and pan) over Christensen-Burley (middle) and random walk (right) '
                   'spheres receiving caustics from the glass left sphere', 'camera motion with subsurface scattering and MNEE caustics')
    camera_motion(d); subsurface_pair(d); caustics(d)
    out['cov-camera-motion-subsurface-caustics'] = d
    d = catcher_film(room_scene('cov-camera-motion-heterogeneous-linking-catcher', 'Camera motion blur with a heterogeneous medium box on the right, light '
                                'and shadow linking between two lamps and a shadow catcher floor', 'camera motion with a heterogeneous volume, linking and the shadow catcher',
                                integrator={'max_volume_bounce': 4}))
    camera_motion(d); linking(d); edit(d, 'floor-object', {'is_shadow_catcher': True}); d['nodes'] += noise_box('medium', (0.9, -1.95, -0.8), (3.5, 0.3, 1.6))
    out['cov-camera-motion-heterogeneous-linking-catcher'] = d
    d = room_scene('cov-adaptive-volumes-subsurface', 'Adaptive sampling (threshold 0.05, at least 8 samples) with a homogeneous box around the left sphere, '
                   'a heterogeneous box around the right one and Christensen-Burley (middle) and random walk (right) spheres, with the Sample Count pass',
                   'adaptive sampling with volumes and subsurface scattering', integrator={**room.adaptive(0.05, 8), 'max_volume_bounce': 4}, samples=64)
    # Film use_sample_count is Cycles' session state (the host refuses a changing set); the Sample Count pass reads the count.
    d['nodes'].append({'handle': 'pass-Sample Count', 'create': 'pass', 'set': {'type': 'sample_count', 'name': 'Sample Count'}})
    subsurface_pair(d); d['nodes'] += sscomb.medium_box('fog', (-3.5, -1.95, -0.8), (-1.1, 0.3, 1.6), 0.1, 0.25)
    d['nodes'] += noise_box('medium', (1.1, -1.95, -0.8), (3.5, 0.3, 1.6))
    out['cov-adaptive-volumes-subsurface'] = d
    d = room_scene('cov-panoramic-world-volume-curves-points', 'An equirectangular panoramic camera inside the room with a scattering world volume, a '
                   'tuft of thick curves, a ball of points and a Christensen-Burley sphere', 'a panoramic camera with a world volume, curves, points and subsurface')
    edit(d, 'camera', {'camera_type': 'panorama', 'panorama_type': 'equirectangular', 'matrix': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, -0.5, 0.0, 0.0, 1.0, -2.5],
                       'latitude_min': -1.5707963267948966, 'latitude_max': 1.5707963267948966, 'longitude_min': -3.141592653589793, 'longitude_max': 3.141592653589793})
    world_volume(d); subsurface_pair(d, right=room.single('right-glossy', 'glossy_bsdf', {'distribution': 'ggx', 'roughness': 0.25, 'color': [0.85, 0.7, 0.4]}))
    d['nodes'] += [curves.hair('tuft', 'matte', 'thick', center=(-1.2, -2, -0.9), spread=0.5, height=2.2), curves.place('tuft-object', 'tuft'),
                   points.cloud('cloud', 'glossy', count=40, center=(1.3, 0.8, -0.8), spread=0.6, radius=0.16), points.place('cloud-object', 'cloud')]
    out['cov-panoramic-world-volume-curves-points'] = d
    d = room_scene('cov-stereo-motion-subsurface', 'The left eye of a spherical stereo perspective camera (interocular 0.3, convergence 6) with object motion on '
                   'the left sphere, deformation motion on the right, random walk subsurface right and Christensen-Burley middle spheres',
                   'a stereo camera with object and deformation motion and subsurface scattering')
    edit(d, 'camera', {'stereo_eye': 'left', 'interocular_distance': 0.3, 'convergence_distance': 6.0})
    subsurface_pair(d); object_motion(d); sscomb.deform(d, 'sphere-2', 0.06)
    out['cov-stereo-motion-subsurface'] = d
    d = room_scene('cov-displacement-motion-tree', 'True displacement (a noise height through a Displacement node) on the moving left sphere (object '
                   'motion) and the deforming right one, lit by a sphere, a spot and an area lamp through the light tree',
                   'true displacement with object and deformation motion and the light tree', integrator={'use_light_tree': True})
    add_shaders(d, {'handle': 'displaced', 'create': 'shader', 'graph': {'nodes': [
        {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'color': [0.6, 0.45, 0.3], 'roughness': 0.2}},
        {'handle': 'noise', 'type': 'noise_texture', 'set': {'scale': 2.5, 'detail': 2.0}},
        {'handle': 'disp', 'type': 'displacement', 'set': {'space': 'object', 'scale': 0.15, 'midlevel': 0.5}}],
        'connect': [['d', 'BSDF', 'output', 'Surface'], ['noise', 'Fac', 'disp', 'Height'], ['disp', 'Displacement', 'output', 'Displacement']]},
        'set': {'emission_sampling_method': 'none', 'displacement_method': 'true'}})
    replace_material(replace_material(d, 'sphere-0', 'displaced'), 'sphere-2', 'displaced')
    object_motion(d); sscomb.deform(d, 'sphere-2', 0.06); d['nodes'] += tree_lamps()
    out['cov-displacement-motion-tree'] = d
    d = room_scene('cov-light-groups-tree-linking-denoise', 'Light groups (lamp, fill, spot and world) with their combined passes, the light tree, light '
                   'linking between the warm and the fill lamp and Open Image Denoise on the CPU', 'light groups with the light tree, light linking and denoising',
                   integrator={'use_light_tree': True, 'use_denoise': True, 'denoiser_type': 'openimagedenoise', 'denoiser_prefilter': 'accurate',
                               'denoiser_quality': 'high', 'denoise_start_sample': 0, 'denoise_use_gpu': False})
    linking(d, shadow=False); d['nodes'] += tree_lamps(spot_extra={'lightgroup': 'spot'})
    edit(edit(edit(d, 'lamp-object', {'lightgroup': 'lamp'}), 'fill-object', {'lightgroup': 'fill'}), 'bg', {'lightgroup': 'world'})
    d['nodes'] += pass_entries([('combined', 'LG '+g, {'lightgroup': g}) for g in ('lamp', 'fill', 'spot', 'world')])
    out['cov-light-groups-tree-linking-denoise'] = d
    d = room_scene('cov-cryptomatte-curves-points-motion-volume', 'Cryptomatte object, material and asset passes with a moving tuft of curves, a deforming '
                   'ball of points, the moving left sphere and a homogeneous box on the right', 'cryptomatte with curves, points, motion and a volume',
                   integrator={'max_volume_bounce': 4})
    edit(d, 'film', {'cryptomatte_passes': 7, 'cryptomatte_depth': 2})
    object_motion(d)
    d['nodes'] += [curves.hair('tuft', 'matte', 'thick', center=(-1.2, -2, -0.9), spread=0.5, height=2.2),
                   curves.place('tuft-object', 'tuft', tfm=None),
                   points.cloud('cloud', 'glossy', count=40, center=(0.9, 0.8, -0.9), spread=0.6, radius=0.16, steps=3), points.place('cloud-object', 'cloud')]
    edit(d, 'tuft-object', {'motion': [sscomb.moving(1.0, (-0.15, 0.0, 0.0)), sscomb.moving(1.0, (0.15, 0.05, 0.0))]})
    d['nodes'] += sscomb.medium_box('fog', (1.1, -1.95, -0.8), (3.5, 0.3, 1.6), 0.1, 0.25)
    d['nodes'] += pass_entries([('cryptomatte', name, {}) for name in ('CryptoObject00', 'CryptoObject01', 'CryptoMaterial00', 'CryptoMaterial01',
                                                                      'CryptoAsset00', 'CryptoAsset01')])
    out['cov-cryptomatte-curves-points-motion-volume'] = d
    d = catcher_film(room_scene('cov-ao-node-volume-motion-catcher', 'An Ambient Occlusion node material on the moving left sphere and the middle sphere, a '
                                'homogeneous box on the right and a shadow catcher floor', 'the AO node with a volume, object motion and the shadow catcher',
                                integrator={'max_volume_bounce': 4}))
    add_shaders(d, room.shader('ao-material', [{'handle': 'ao', 'type': 'ambient_occlusion', 'set': {'color': [0.9, 0.7, 0.4], 'distance': 1.4, 'samples': 4,
                                                                                                    'only_local': False, 'inside': False}},
                                                {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.2}}],
                                               [['ao', 'Color', 'd', 'Color'], ['d', 'BSDF', 'output', 'Surface']]))
    replace_material(replace_material(d, 'sphere-0', 'ao-material'), 'sphere-1', 'ao-material')
    object_motion(d); edit(d, 'floor-object', {'is_shadow_catcher': True})
    d['nodes'] += sscomb.medium_box('fog', (1.1, -1.95, -0.8), (3.5, 0.3, 1.6), 0.1, 0.25)
    out['cov-ao-node-volume-motion-catcher'] = d
    d = catcher_film(room_scene('cov-subsurface-skin-legacy-linking-catcher', 'Skin (middle) and legacy random walk (right) subsurface spheres with light and '
                                'shadow linking between two lamps, a shadow catcher floor and Open Image Denoise on the CPU',
                                'the skin and legacy random walk methods with linking, the shadow catcher and denoising',
                                integrator={'use_denoise': True, 'denoiser_type': 'openimagedenoise', 'denoiser_prefilter': 'accurate', 'denoiser_quality': 'high',
                                            'denoise_start_sample': 0, 'denoise_use_gpu': False}))
    subsurface_pair(d, middle=skin_node(), right=subsurface.node('sss-legacy', {'method': 'random_walk_legacy'}))
    linking(d); edit(d, 'floor-object', {'is_shadow_catcher': True})
    out['cov-subsurface-skin-legacy-linking-catcher'] = d
    out.update(outward_tier())
    out.update(state_tier())
    return out


def state_tier():
    """The Light Path node's path-state outputs in shadow, volume, lamp and world evaluations, a Principled Volume attribute on the
    mesh, and the bake of a scene with an approximate shadow catcher and an opaque world."""
    out = {}
    d = room_scene('cov-shadow-light-path-depth', 'The left sphere mixes a diffuse and a transparent BSDF by Ray Depth > 0.5 (opaque to camera rays, '
                   'transparent to deeper rays and to the shadow rays, whose depth counts one more): the Light Path depths in shadow evaluations',
                   'the Light Path node in shadow evaluations', samples=16)
    add_shaders(d, room.shader('seethrough', [{'handle': 'lp', 'type': 'light_path'}, {'handle': 'c', 'type': 'math', 'set': {'math_type': 'greater_than', 'value2': 0.5}},
                                              {'handle': 't', 'type': 'transparent_bsdf', 'set': {'color': [0.9, 0.9, 0.9]}},
                                              {'handle': 'df', 'type': 'diffuse_bsdf', 'set': {'color': [0.4, 0.55, 0.7]}}, {'handle': 'm', 'type': 'mix_closure'}],
                               [['lp', 'Ray Depth', 'c', 'Value1'], ['c', 'Value', 'm', 'Fac'], ['df', 'BSDF', 'm', 'Closure1'], ['t', 'BSDF', 'm', 'Closure2'],
                                ['m', 'Closure', 'output', 'Surface']]))
    out['cov-shadow-light-path-depth'] = replace_material(d, 'sphere-0', 'seethrough')
    d = room_scene('cov-volume-light-path-depth', 'A scattering box whose density is 0.2 + 0.1 * Ray Depth + 0.05 * Transparent Depth + 0.3 * Backfacing + '
                   '0.2 * Ray Length (the Geometry node\'s Backfacing and the Light Path node\'s Ray Length read zero in a volume): the path-state outputs in volume evaluations',
                   'the Light Path node in volume evaluations', integrator={'max_volume_bounce': 4})
    box = sscomb.medium_box('medium', (-1.2, -1.95, -0.8), (3.5, 0.3, 1.6), 0.1, 0.3)
    g = box[0]['graph']; terms = (('Ray Depth', 0.1), ('Transparent Depth', 0.05), ('Backfacing', 0.3), ('Ray Length', 0.2))
    g['nodes'] += [{'handle': 'lp', 'type': 'light_path'}, {'handle': 'geo', 'type': 'geometry'}] + [{'handle': f't{k}', 'type': 'math', 'set': {'math_type': 'multiply_add', 'value2': w, 'value3': 0.2 if k == 0 else 0.0}}
                                                             for k, (_, w) in enumerate(terms)] + [{'handle': f'a{k}', 'type': 'math', 'set': {'math_type': 'add'}} for k in range(1, len(terms))]
    g['connect'] += [['geo' if name == 'Backfacing' else 'lp', name, f't{k}', 'Value1'] for k, (name, _) in enumerate(terms)]
    g['connect'] += [['t0', 'Value', 'a1', 'Value1']] + [[f'a{k-1}' if k > 1 else 't0', 'Value', f'a{k}', 'Value1'] for k in range(2, len(terms))] + [[f't{k}', 'Value', f'a{k}', 'Value2'] for k in range(1, len(terms))]
    g['connect'] = [c for c in g['connect'] if c != ['t0', 'Value', 'a1', 'Value1']] + [['t0', 'Value', 'a1', 'Value1']]
    g['connect'] += [[f'a{len(terms)-1}', 'Value', 's', 'Density']]
    d['nodes'] += box
    out['cov-volume-light-path-depth'] = d
    d = room_scene('cov-principled-volume-mesh-attribute', 'A Principled Volume box whose density and colour attributes name the box mesh\'s mesh-element '
                   'attributes "mydens" (0.35) and "mytint" (0.9, 0.7, 0.5): their one value scales the density and the colour, as for any volume',
                   'a Principled Volume attribute on the mesh', integrator={'max_volume_bounce': 4})
    box = sscomb.medium_box('medium', (-1.2, -1.95, -0.8), (3.5, 0.3, 1.6), 0.1, 0.3)
    box[0]['graph'] = {'nodes': [{'handle': 'pv', 'type': 'principled_volume', 'set': {'density': 1.0, 'color': [0.7, 0.8, 0.9], 'anisotropy': 0.2,
                                                                                      'density_attribute': 'mydens', 'color_attribute': 'mytint'}}],
                       'connect': [['pv', 'Volume', 'output', 'Volume']]}
    box[1]['mesh']['attributes'] = [{'name': 'mydens', 'value': 'float', 'domain': 'mesh', 'data': [0.35]},
                                    {'name': 'mytint', 'value': 'color', 'domain': 'mesh', 'data': [0.9, 0.7, 0.5]}]
    d['nodes'] += box
    out['cov-principled-volume-mesh-attribute'] = d
    d = bake_combined('cov-bake-approximate-catcher-world', 'Combined bake of a shadow catcher ball with the approximate shadow catcher and an opaque world: '
                      'the film\'s background pass makes the bake kernel bake the world along the direction to each surface point',
                      'baking with an approximate shadow catcher and an opaque world')
    edit(edit(d, 'ball-object', {'is_shadow_catcher': True}), 'film', {'use_approximate_shadow_catcher': True})
    out['cov-bake-approximate-catcher-world'] = d
    d = room_scene('cov-lamp-light-path-depth', 'The point lamp and a sun share an emission shader whose strength is 0.4 + 0.1 * Ray Depth + 0.07 * '
                   'Transparent Depth + 0.03 * Diffuse Depth + 0.05 * Glossy Depth + 0.02 * Transmission Depth: the Light Path depths in lamp emission '
                   'evaluations (the shadow path\'s counters in light sampling, the main path\'s at a lamp hit or a sun)', 'the Light Path node in lamp emission evaluations')
    terms = (('Ray Depth', 0.1), ('Transparent Depth', 0.07), ('Diffuse Depth', 0.03), ('Glossy Depth', 0.05), ('Transmission Depth', 0.02))
    g = {'nodes': [{'handle': 'e', 'type': 'emission', 'set': {'color': [1, 1, 1], 'strength': 1}}, {'handle': 'lp', 'type': 'light_path'}]
                  + [{'handle': f't{k}', 'type': 'math', 'set': {'math_type': 'multiply_add', 'value2': w, 'value3': 0.4 if k == 0 else 0.0}} for k, (_, w) in enumerate(terms)]
                  + [{'handle': f'a{k}', 'type': 'math', 'set': {'math_type': 'add'}} for k in range(1, len(terms))],
         'connect': [['lp', name, f't{k}', 'Value1'] for k, (name, _) in enumerate(terms)]
                    + [[f'a{k-1}' if k > 1 else 't0', 'Value', f'a{k}', 'Value1'] for k in range(1, len(terms))] + [[f't{k}', 'Value', f'a{k}', 'Value2'] for k in range(1, len(terms))]
                    + [[f'a{len(terms)-1}', 'Value', 'e', 'Strength'], ['e', 'Emission', 'output', 'Surface']]}
    next(n for n in d['nodes'] if n.get('handle') == 'emitter')['graph'] = g
    d['nodes'] += [{'handle': 'sun', 'create': 'sunlight', 'set': {'angle': 0.08, 'strength': [1.5, 1.4, 1.2], 'use_mis': True, 'used_shaders': ['emitter']}},
                   {'handle': 'sun-object', 'create': 'object', 'set': {'geometry': 'sun', 'tfm': [1.0, 0.0, 0.0, 0.0, 0.0, 0.8, 0.6, 0.0, 0.0, -0.6, 0.8, 0.0]}}]
    out['cov-lamp-light-path-depth'] = d
    d = room_scene('cov-world-light-path-depth', 'A noise world whose strength is 0.6 + 0.2 * Ray Depth + 0.1 * Diffuse Depth + 0.05 * Glossy Depth + '
                   '0.03 * Transparent Depth + 0.04 * Transmission Depth, sampled through a background light with MIS: the Light Path depths in world '
                   'evaluations (the importance map reads every depth as zero and Ray Depth as one; the background hits and light sampling read the path\'s counters)',
                   'the Light Path node in world evaluations')
    terms = (('Ray Depth', 0.2), ('Diffuse Depth', 0.1), ('Glossy Depth', 0.05), ('Transparent Depth', 0.03), ('Transmission Depth', 0.04))
    g = {'nodes': [{'handle': 'b', 'type': 'background_shader', 'set': {'color': [0.3, 0.35, 0.42], 'strength': 1.0}}, {'handle': 'tc', 'type': 'texture_coordinate'},
                   {'handle': 'nz', 'type': 'noise_texture', 'set': {'scale': 2.5, 'detail': 2.0}}, {'handle': 'lp', 'type': 'light_path'}]
                  + [{'handle': f't{k}', 'type': 'math', 'set': {'math_type': 'multiply_add', 'value2': w, 'value3': 0.6 if k == 0 else 0.0}} for k, (_, w) in enumerate(terms)]
                  + [{'handle': f'a{k}', 'type': 'math', 'set': {'math_type': 'add'}} for k in range(1, len(terms))],
         'connect': [['tc', 'Generated', 'nz', 'Vector'], ['nz', 'Color', 'b', 'Color']] + [['lp', name, f't{k}', 'Value1'] for k, (name, _) in enumerate(terms)]
                    + [[f'a{k-1}' if k > 1 else 't0', 'Value', f'a{k}', 'Value1'] for k in range(1, len(terms))] + [[f't{k}', 'Value', f'a{k}', 'Value2'] for k in range(1, len(terms))]
                    + [[f'a{len(terms)-1}', 'Value', 'b', 'Strength'], ['b', 'Background', 'output', 'Surface']]}
    next(n for n in d['nodes'] if n.get('handle') == 'world')['graph'] = g
    d['nodes'] += [{'handle': 'world-light', 'create': 'backgroundlight', 'set': {'use_mis': True, 'map_resolution': 64, 'strength': [1.0, 1.0, 1.0], 'used_shaders': ['world']}},
                   {'handle': 'world-light-object', 'create': 'object', 'set': {'geometry': 'world-light'}}]
    out['cov-world-light-path-depth'] = d
    return out


def outward_spheres(document, handles=('sphere-0', 'sphere-1', 'sphere-2')):
    """author_sampling.sphere winds its triangles with the geometric normal pointing into the sphere (as the bake ball, see outward); a
    consumer's closed mesh points outwards. Reverse the winding of the named room spheres."""
    for h in handles:
        idx = node(document, h)['mesh']['indices']
        for t in range(len(idx)//3):
            idx[3*t+1], idx[3*t+2] = idx[3*t+2], idx[3*t+1]
    return document


def outward_tier():
    """Subsurface scattering, glass and caustics on spheres wound outwards (the subsurface family and every room-based scene wind them inwards)."""
    out = {}
    d = room_scene('cov-outward-subsurface-methods', 'Skin (left), Christensen-Burley (middle) and random walk (right) Subsurface Scattering nodes on '
                   'spheres wound outwards', 'subsurface methods on outward-wound spheres')
    add_shaders(d, skin_node(), burley_node(), walk_node())
    replace_material(replace_material(replace_material(d, 'sphere-0', 'sss-skin'), 'sphere-1', 'sss-burley'), 'sphere-2', 'sss-walk')
    out['cov-outward-subsurface-methods'] = outward_spheres(d)
    d = room_scene('cov-outward-subsurface-principled', 'Legacy random walk Subsurface Scattering node (left) and Principled BSDF subsurface with the random '
                   'walk (middle) and Christensen-Burley (right) methods on spheres wound outwards', 'Principled and legacy subsurface on outward-wound spheres')
    add_shaders(d, subsurface.node('sss-legacy', {'method': 'random_walk_legacy'}), subsurface.principled('sss-p-walk', {}),
                subsurface.principled('sss-p-burley', {'subsurface_method': 'burley'}))
    replace_material(replace_material(replace_material(d, 'sphere-0', 'sss-legacy'), 'sphere-1', 'sss-p-walk'), 'sphere-2', 'sss-p-burley')
    out['cov-outward-subsurface-principled'] = outward_spheres(d)
    d = room_scene('cov-outward-glass-caustics-subsurface', 'An outward-wound glass caustic caster (left) casting caustics from the point lamp onto '
                   'outward-wound Christensen-Burley (middle) and random walk (right) receivers and the floor', 'glass, caustics and subsurface on outward-wound spheres')
    subsurface_pair(d); caustics(d)
    out['cov-outward-glass-caustics-subsurface'] = outward_spheres(d)
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true', help='Fail when a committed scene differs')
    args = parser.parse_args()
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
