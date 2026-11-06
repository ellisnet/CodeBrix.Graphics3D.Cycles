#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the volume oracle scenes (scenes/volumes/) byte for byte.

Shader-defined homogeneous volumes lit by lamps: a closed box of medium (absorption and
Henyey-Greenstein scattering) in the sampling family's room, with direct light from a point,
sun or background light (the volume's distance, equiangular or MIS scatter sampling), shadows
through the medium, a world volume, the Volume Coefficients and Principled Volume nodes and an emissive medium,
through the public C API.
Usage: python3 native/oracle/scenes/author_volumes.py [--check]."""
import argparse
import json
from pathlib import Path

import author_sampling as room

HERE = Path(__file__).resolve().parent/'volumes'

# A closed box (outward-facing triangles) around the middle sphere, clear of the floor (a box face in the floor plane
# would make coplanar boundary hits).
BOX = [(-1.6, -1.9, -1.4), (1.6, -1.9, -1.4), (-1.6, 1.4, -1.4), (1.6, 1.4, -1.4),
       (-1.6, -1.9, 2.2), (1.6, -1.9, 2.2), (-1.6, 1.4, 2.2), (1.6, 1.4, 2.2)]
BOX_INDICES = [0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7]


def medium(handle, absorption, scatter, anisotropy, method, phase=None, graph=None):
    """A volume-only shader: absorption plus scatter (Henyey-Greenstein, or the phase function given as
    (name, extra settings)), or the volume graph given (nodes, connect)."""
    if graph is None:
        scatter_set = {'density': scatter, 'color': [0.75, 0.8, 0.9], 'anisotropy': anisotropy}
        if phase:
            scatter_set['phase'] = phase[0]
            scatter_set.update(phase[1])
        nodes = [{'handle': 'a', 'type': 'absorption_volume', 'set': {'density': absorption, 'color': [0.8, 0.65, 0.4]}},
                 {'handle': 's', 'type': 'scatter_volume', 'set': scatter_set},
                 {'handle': 'add', 'type': 'add_closure'}]
        connect = [['a', 'Volume', 'add', 'Closure1'], ['s', 'Volume', 'add', 'Closure2'], ['add', 'Closure', 'output', 'Volume']]
    else:
        nodes, connect = graph
    return {'handle': handle, 'create': 'shader', 'graph': {'nodes': nodes, 'connect': connect},
            'set': {'emission_sampling_method': 'none', 'volume_sampling_method': method}}


def scene(scene_id, description, lamp='point', absorption=0.12, scatter=0.35, anisotropy=0.3, method='multiple_importance',
          world_volume=False, world_light=False, phase=None, graph=None, integrator=None, more_lamps=()):
    document = room.scene(scene_id, description, dict({'max_volume_bounce': 4}, **(integrator or {})), samples=16)
    document['sourceFixture'] = 'native/oracle/scenes/author_volumes.py (a homogeneous medium box in the sampling family room)'
    nodes = []
    for entry in document['nodes']:
        if entry['handle'] in ('lamp', 'lamp-object') and lamp != 'point':
            continue
        if entry['handle'] == 'world' and (world_volume or world_light):
            world_graph = entry['graph']
            if world_volume:
                world_graph['nodes'].append({'handle': 'wv', 'type': 'scatter_volume', 'set': {'density': 0.03, 'color': [0.8, 0.8, 0.8], 'anisotropy': 0.2}})
                world_graph['connect'].append(['wv', 'Volume', 'output', 'Volume'])
            if world_light:
                entry['set'] = {'emission_sampling_method': 'auto'}
                nodes.append(entry)
                nodes.append({'handle': 'world-light', 'create': 'backgroundlight',
                              'set': {'use_mis': True, 'map_resolution': 64, 'strength': [1.0, 1.0, 1.0], 'used_shaders': ['world']}})
                nodes.append({'handle': 'world-light-object', 'create': 'object', 'set': {'geometry': 'world-light'}})
                continue
        nodes.append(entry)
    if lamp == 'sun' or 'sun' in more_lamps:
        nodes += [{'handle': 'sun', 'create': 'sunlight', 'set': {'angle': 0.08, 'strength': [3.0, 2.8, 2.5], 'use_mis': True, 'used_shaders': ['emitter']}},
                  {'handle': 'sun-object', 'create': 'object', 'set': {'geometry': 'sun', 'tfm': [1.0, 0.0, 0.0, 0.0, 0.0, 0.8, 0.6, 0.0, 0.0, -0.6, 0.8, 0.0]}}]
    if lamp == 'mesh' or 'mesh' in more_lamps:
        # An emissive quad above the box, facing down (light distribution triangles, front side sampled).
        nodes += [{'handle': 'panel-emission', 'create': 'shader', 'graph': {'nodes': [{'handle': 'e', 'type': 'emission', 'set': {'color': [1.0, 0.9, 0.8], 'strength': 30.0}}],
                                                                             'connect': [['e', 'Emission', 'output', 'Surface']]},
                   'set': {'emission_sampling_method': 'front'}},
                  {'handle': 'panel', 'create': 'mesh', 'mesh': {'positions': [-0.8, 2.6, -0.4, 0.8, 2.6, -0.4, 0.8, 2.6, 0.8, -0.8, 2.6, 0.8], 'indices': [0, 1, 2, 0, 2, 3]},
                   'set': {'used_shaders': ['panel-emission']}},
                  {'handle': 'panel-object', 'create': 'object', 'set': {'geometry': 'panel'}}]
    for shaped in [lamp]+list(more_lamps):
        if shaped in SHAPED_LAMPS:
            kind, settings, tfm = SHAPED_LAMPS[shaped]
            handle = 'shaped' if shaped == lamp else 'shaped-'+shaped
            nodes += [{'handle': handle, 'create': kind, 'set': dict(settings, use_mis=True, used_shaders=['emitter'])},
                      {'handle': handle+'-object', 'create': 'object', 'set': {'geometry': handle, 'tfm': tfm}}]
    nodes.append(medium('medium', absorption, scatter, anisotropy, method, phase, graph))
    nodes += [{'handle': 'box', 'create': 'mesh', 'mesh': {'positions': [c for p in BOX for c in p], 'indices': BOX_INDICES},
               'set': {'used_shaders': ['medium']}},
              {'handle': 'box-object', 'create': 'object', 'set': {'geometry': 'box'}}]
    document['nodes'] = nodes
    return document


def noise_medium(scale=1.6, density=0.8):
    """A heterogeneous medium: the scatter density follows a Noise Texture at the shading position (object space of the
    box), plus a constant absorption."""
    return ([{'handle': 'n', 'type': 'noise_texture', 'set': {'scale': scale, 'detail': 2.0}},
             {'handle': 'm', 'type': 'math', 'set': {'math_type': 'multiply', 'value2': density}},
             {'handle': 'a', 'type': 'absorption_volume', 'set': {'density': 0.05, 'color': [0.8, 0.65, 0.4]}},
             {'handle': 's', 'type': 'scatter_volume', 'set': {'color': [0.75, 0.8, 0.9], 'anisotropy': 0.3}},
             {'handle': 'add', 'type': 'add_closure'}],
            [['n', 'Fac', 'm', 'Value1'], ['m', 'Value', 's', 'Density'], ['a', 'Volume', 'add', 'Closure1'], ['s', 'Volume', 'add', 'Closure2'],
             ['add', 'Closure', 'output', 'Volume']])


# An emissive scattering medium, constant or following a Noise Texture.
EMISSIVE = ([{'handle': 'a', 'type': 'absorption_volume', 'set': {'density': 0.1, 'color': [0.8, 0.65, 0.4]}},
             {'handle': 's', 'type': 'scatter_volume', 'set': {'density': 0.3, 'color': [0.75, 0.8, 0.9], 'anisotropy': 0.2}},
             {'handle': 'e', 'type': 'emission', 'set': {'color': [1.0, 0.6, 0.3], 'strength': 0.08}},
             {'handle': 'add', 'type': 'add_closure'}, {'handle': 'add2', 'type': 'add_closure'}],
            [['a', 'Volume', 'add', 'Closure1'], ['s', 'Volume', 'add', 'Closure2'], ['add', 'Closure', 'add2', 'Closure1'], ['e', 'Emission', 'add2', 'Closure2'],
             ['add2', 'Closure', 'output', 'Volume']])
EMISSIVE_NOISE = ([{'handle': 'n', 'type': 'noise_texture', 'set': {'scale': 1.6, 'detail': 2.0}},
                   {'handle': 'm', 'type': 'math', 'set': {'math_type': 'multiply', 'value2': 0.8}},
                   {'handle': 's', 'type': 'scatter_volume', 'set': {'color': [0.75, 0.8, 0.9], 'anisotropy': 0.3}},
                   {'handle': 'e', 'type': 'emission', 'set': {'color': [1.0, 0.6, 0.3], 'strength': 0.08}},
                   {'handle': 'add', 'type': 'add_closure'}],
                  [['n', 'Fac', 'm', 'Value1'], ['m', 'Value', 's', 'Density'], ['s', 'Volume', 'add', 'Closure1'], ['e', 'Emission', 'add', 'Closure2'],
                   ['add', 'Closure', 'output', 'Volume']])

# Spot and area lights above the medium box, pointing down (volume direct light from shaped lights).
SHAPED_LAMPS = {
    'spot': ('spotlight', {'strength': [160, 150, 140], 'radius': 0.25, 'is_sphere': True, 'angle': 1.0, 'smooth': 0.3}, [1, 0, 0, 0.8, 0, 0, 1, 2.8, 0, -1, 0, 0.2]),
    'area': ('arealight', {'strength': [80, 76, 70], 'sizeu': 1.6, 'sizev': 1.2, 'ellipse': False, 'spread': 3.14159265}, [1, 0, 0, 0.0, 0, 0, 1, 2.8, 0, -1, 0, 0.5]),
    'area-ellipse': ('arealight', {'strength': [80, 76, 70], 'sizeu': 1.4, 'sizev': 1.0, 'ellipse': True, 'spread': 1.1}, [1, 0, 0, -0.3, 0, 0, 1, 2.8, 0, -1, 0, 0.3]),
}

def with_passes(document, kinds, lightgroup=False):
    """Named light passes on a volume scene (written beside the combined film); with lightgroup, the medium box and the
    lamp in light group 'one' and its pass."""
    for kind in kinds:
        document['nodes'].append({'handle': 'pass-'+kind, 'create': 'pass', 'set': {'type': kind, 'name': kind}})
    if lightgroup:
        for entry in document['nodes']:
            if entry.get('handle') in ('box-object', 'lamp-object'):
                entry['set']['lightgroup'] = 'one'
        document['nodes'].append({'handle': 'pass-group', 'create': 'pass', 'set': {'type': 'combined', 'name': 'Combined_one', 'lightgroup': 'one'}})
    return document


def with_catcher(document, objects=('floor-object',), transparent=True):
    """The named objects as shadow catchers (the three catcher passes), a transparent film or an opaque one with the
    background pass (the path shades the background first)."""
    for entry in document['nodes']:
        if entry.get('handle') in objects:
            entry['set']['is_shadow_catcher'] = True
        if entry.get('handle') == 'bg':
            entry['set']['transparent'] = transparent
    for kind, name in (('shadow_catcher', 'Shadow Catcher'), ('shadow_catcher_matte', 'Shadow Catcher Matte'), ('shadow_catcher_sample_count', 'Shadow Catcher Sample Count')):
        document['nodes'].append({'handle': 'pass-'+kind, 'create': 'pass', 'set': {'type': kind, 'name': name}})
    if not transparent:
        document['nodes'].append({'handle': 'pass-background', 'create': 'pass', 'set': {'type': 'background', 'name': 'Background'}})
    return document


VOLUME_PASSES = ('volume_direct', 'volume_indirect', 'diffuse_direct', 'diffuse_indirect', 'glossy_direct', 'glossy_indirect', 'transmission_direct', 'transmission_indirect', 'emission', 'background')

# The light tree with volumes (its volume segments, light_tree_sample<true> and light_tree_pdf<true>).
TREE = {'use_light_tree': True, 'light_sampling_threshold': 0.0}
TREE_LAMPS = ('spot', 'area-ellipse', 'sun')

# Biased volume ray marching (the integrator's volume_ray_marching).
MARCHING = {'volume_ray_marching': True}


def scenes():
    return {
        'heterogeneous-point.json': scene('volumes/heterogeneous-point', 'A heterogeneous medium box (noise density) lit by a point light: null scattering with the majorant octree, MIS of distance and equiangular sampling', graph=noise_medium()),
        'heterogeneous-point-distance.json': scene('volumes/heterogeneous-point-distance', 'The heterogeneous box with distance sampling only', method='distance', graph=noise_medium()),
        'heterogeneous-point-equiangular.json': scene('volumes/heterogeneous-point-equiangular', 'The heterogeneous box with equiangular sampling only', method='equiangular', graph=noise_medium()),
        'heterogeneous-sun.json': scene('volumes/heterogeneous-sun', 'The heterogeneous box lit by a sun light', lamp='sun', graph=noise_medium(scale=2.4, density=1.2)),
        'ray-marching-heterogeneous-point.json': scene('volumes/ray-marching-heterogeneous-point', 'The heterogeneous box integrated by biased ray marching, point light with MIS', graph=noise_medium(), integrator=MARCHING),
        'ray-marching-heterogeneous-distance.json': scene('volumes/ray-marching-heterogeneous-distance', 'Biased ray marching through the heterogeneous box, distance sampling only', method='distance', graph=noise_medium(), integrator=MARCHING),
        'ray-marching-heterogeneous-equiangular.json': scene('volumes/ray-marching-heterogeneous-equiangular', 'Biased ray marching through the heterogeneous box, equiangular sampling only', method='equiangular', graph=noise_medium(), integrator=MARCHING),
        'ray-marching-heterogeneous-sun.json': scene('volumes/ray-marching-heterogeneous-sun', 'Biased ray marching through the denser heterogeneous box lit by a sun light', lamp='sun', graph=noise_medium(scale=2.4, density=1.2), integrator=MARCHING),
        'ray-marching-homogeneous-point.json': scene('volumes/ray-marching-homogeneous-point', 'Biased ray marching through the homogeneous scattering box (one step per segment), point light', integrator=MARCHING),
        'ray-marching-max-steps.json': scene('volumes/ray-marching-max-steps', 'Biased ray marching with at most 6 steps per segment (the step size grows to cover the segment)', graph=noise_medium(), integrator=dict(MARCHING, volume_max_steps=6)),
        'catcher-floor.json': with_catcher(scene('volumes/catcher-floor', 'A catcher floor under the scattering box: the split at the closest hit with an empty stack, point light')),
        'catcher-sphere.json': with_catcher(scene('volumes/catcher-sphere', 'A catcher sphere inside the scattering box: the split after the volume segment and the stack re-init, point light'), objects=('sphere-1-object',)),
        'catcher-world.json': with_catcher(scene('volumes/catcher-world', 'A thin world volume with a catcher floor: every split after a volume segment, the background volume left out of the catcher path', world_volume=True)),
        'catcher-background.json': with_catcher(scene('volumes/catcher-background', 'A catcher sphere inside the box, opaque film with the background pass (background first, then the stack re-init)'), objects=('sphere-1-object',), transparent=False),
        'catcher-heterogeneous.json': with_catcher(scene('volumes/catcher-heterogeneous', 'A catcher sphere inside the heterogeneous box, point light', graph=noise_medium()), objects=('sphere-1-object',)),
        'catcher-emission.json': with_catcher(scene('volumes/catcher-emission', 'A catcher sphere inside an emissive medium', graph=EMISSIVE), objects=('sphere-1-object',)),
        'catcher-tree.json': with_catcher(scene('volumes/catcher-tree', 'A catcher floor, a world volume and the light tree', world_volume=True, integrator=TREE, more_lamps=TREE_LAMPS)),
        'catcher-passes.json': with_catcher(with_passes(scene('volumes/catcher-passes', 'A catcher sphere inside an emissive medium with the volume and light passes and a light group', graph=EMISSIVE), VOLUME_PASSES, lightgroup=True), objects=('sphere-1-object',)),
        'ray-marching-tree-catcher.json': with_catcher(with_passes(scene('volumes/ray-marching-tree-catcher', 'Biased ray marching with the light tree, point, spot, area, sun and mesh lights, the light passes and a catcher sphere', graph=noise_medium(), integrator=dict(MARCHING, **TREE), more_lamps=TREE_LAMPS+('mesh',)), VOLUME_PASSES), objects=('sphere-1-object',)),
        'passes-volume.json': with_passes(scene('volumes/passes-volume', 'The volume direct and indirect passes beside the other light passes, scattering box, point light', ), VOLUME_PASSES),
        'passes-volume-group.json': with_passes(scene('volumes/passes-volume-group', 'The volume passes with a light group on the lamp and the box, world light', world_light=True), VOLUME_PASSES, lightgroup=True),
        'passes-heterogeneous.json': with_passes(scene('volumes/passes-heterogeneous', 'The volume passes through the heterogeneous box, point light', graph=noise_medium()), VOLUME_PASSES),
        'passes-tree.json': with_passes(scene('volumes/passes-tree', 'The volume passes with the light tree, point, spot, area and sun lights', integrator=TREE, more_lamps=TREE_LAMPS), VOLUME_PASSES),
        'passes-emission.json': with_passes(scene('volumes/passes-emission', 'The light passes of an emissive medium (film_write_volume_emission), with a light group', graph=EMISSIVE), VOLUME_PASSES, lightgroup=True),
        'passes-emission-heterogeneous.json': with_passes(scene('volumes/passes-emission-heterogeneous', 'The light passes of an emissive heterogeneous medium', graph=EMISSIVE_NOISE), VOLUME_PASSES),
        'tree-point.json': scene('volumes/tree-point', 'The scattering box lit by one point light through the light tree, MIS', integrator=TREE),
        'tree-lamps.json': scene('volumes/tree-lamps', 'The scattering box with point, spot, area and sun lights through the light tree, MIS', integrator=TREE, more_lamps=TREE_LAMPS),
        'tree-lamps-equiangular.json': scene('volumes/tree-lamps-equiangular', 'The light tree with point, spot, area and sun lights, equiangular sampling', integrator=TREE, more_lamps=TREE_LAMPS, method='equiangular'),
        'tree-mesh.json': scene('volumes/tree-mesh', 'The light tree with an emissive quad beside the point, spot, area and sun lights (forward pdfs of triangles from volumes), MIS', integrator=TREE, more_lamps=TREE_LAMPS+('mesh',)),
        'tree-world.json': scene('volumes/tree-world', 'The light tree with a background light, point light and a thin world volume, MIS', integrator=TREE, world_light=True, world_volume=True),
        'tree-heterogeneous.json': scene('volumes/tree-heterogeneous', 'The heterogeneous box with point, spot, area and sun lights through the light tree', integrator=TREE, more_lamps=TREE_LAMPS, graph=noise_medium()),
        'scatter-spot.json': scene('volumes/scatter-spot', 'The scattering box lit by a spot light above it (the cone as the valid ray segment), MIS', lamp='spot'),
        'scatter-area.json': scene('volumes/scatter-area', 'The scattering box lit by a rectangular area light above it, MIS', lamp='area'),
        'scatter-area-ellipse.json': scene('volumes/scatter-area-ellipse', 'The scattering box lit by an elliptic area light with a 1.1 rad spread, MIS', lamp='area-ellipse'),
        'scatter-mesh-light.json': scene('volumes/scatter-mesh-light', 'The scattering box lit only by an emissive quad above it (triangle lights in the volume direct light), MIS', lamp='mesh'),
        'heterogeneous-spot.json': scene('volumes/heterogeneous-spot', 'The heterogeneous box lit by the spot light, equiangular sampling', lamp='spot', method='equiangular', graph=noise_medium()),
        'absorption-point.json': scene('volumes/absorption-point', 'An absorbing medium box (no scattering): shadows and camera rays through the medium, point light',
                                       scatter=0.0),
        'scatter-point.json': scene('volumes/scatter-point', 'A scattering medium box lit by a sphere point light: direct light in the medium with MIS of distance and equiangular sampling'),
        'scatter-point-distance.json': scene('volumes/scatter-point-distance', 'The scattering box with distance sampling only', method='distance'),
        'scatter-point-equiangular.json': scene('volumes/scatter-point-equiangular', 'The scattering box with equiangular sampling only', method='equiangular'),
        'scatter-sun.json': scene('volumes/scatter-sun', 'The scattering box lit by a sun light (distance sampling of the distant light)', lamp='sun'),
        'scatter-world.json': scene('volumes/scatter-world', 'The scattering box lit only by the world through a background light with MIS', lamp='none', world_light=True),
        'world-volume-point.json': scene('volumes/world-volume-point', 'A thin scattering world volume and the scattering box, point light', world_volume=True),
        'phase-rayleigh.json': scene('volumes/phase-rayleigh', 'The scattering box with the Rayleigh phase function', phase=('Rayleigh', {})),
        'phase-draine.json': scene('volumes/phase-draine', 'The scattering box with the Draine phase function (g 0.4, alpha 0.6)', phase=('Draine', {'anisotropy': 0.4, 'alpha': 0.6})),
        'phase-fournier-forand.json': scene('volumes/phase-fournier-forand', 'The scattering box with the Fournier-Forand phase function (B 0.1, IOR 1.12)',
                                            phase=('Fournier-Forand', {'backscatter': 0.1, 'IOR': 1.12})),
        'phase-mie.json': scene('volumes/phase-mie', 'The scattering box with the Mie phase function (droplet diameter 3 um: Henyey-Greenstein and Draine)',
                                phase=('Mie', {'diameter': 3.0})),
        'phase-mie-small.json': scene('volumes/phase-mie-small', 'The Mie phase function with a 0.6 um diameter (the second fit range)', phase=('Mie', {'diameter': 0.6})),
        'volume-coefficients.json': scene('volumes/volume-coefficients', 'A Volume Coefficients node: scatter, absorption and emission coefficients, Henyey-Greenstein phase', graph=(
            [{'handle': 'v', 'type': 'volume_coefficients', 'set': {'phase': 'Henyey-Greenstein', 'scatter_coeffs': [0.3, 0.32, 0.36], 'absorption_coeffs': [0.05, 0.08, 0.12],
                                                                     'emission_coeffs': [0.02, 0.015, 0.01], 'anisotropy': 0.2}}],
            [['v', 'Volume', 'output', 'Volume']])),
        'principled-volume.json': scene('volumes/principled-volume', 'A Principled Volume node with emission and blackbody (temperature 1800 K)', graph=(
            [{'handle': 'v', 'type': 'principled_volume', 'set': {'color': [0.7, 0.75, 0.8], 'density': 0.4, 'anisotropy': 0.25, 'absorption_color': [0.6, 0.5, 0.4],
                                                                   'emission_strength': 0.05, 'emission_color': [1.0, 0.8, 0.6], 'blackbody_intensity': 0.3, 'temperature': 1800.0}}],
            [['v', 'Volume', 'output', 'Volume']])),
        'volume-emission.json': scene('volumes/volume-emission', 'An emissive scattering medium (Emission closure added to the volume shader), sun light', lamp='sun', graph=(
            [{'handle': 'a', 'type': 'absorption_volume', 'set': {'density': 0.1, 'color': [0.8, 0.65, 0.4]}},
             {'handle': 's', 'type': 'scatter_volume', 'set': {'density': 0.3, 'color': [0.75, 0.8, 0.9], 'anisotropy': 0.3}},
             {'handle': 'e', 'type': 'emission', 'set': {'color': [1.0, 0.6, 0.3], 'strength': 0.08}},
             {'handle': 'add', 'type': 'add_closure'}, {'handle': 'add2', 'type': 'add_closure'}],
            [['a', 'Volume', 'add', 'Closure1'], ['s', 'Volume', 'add', 'Closure2'], ['add', 'Closure', 'add2', 'Closure1'], ['e', 'Emission', 'add2', 'Closure2'],
             ['add2', 'Closure', 'output', 'Volume']])),
    }


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
