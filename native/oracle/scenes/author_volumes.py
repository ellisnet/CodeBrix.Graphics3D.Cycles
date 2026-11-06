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
          world_volume=False, world_light=False, phase=None, graph=None, integrator=None):
    document = room.scene(scene_id, description, dict({'max_volume_bounce': 4}, **(integrator or {})), samples=16)
    document['sourceFixture'] = 'native/oracle/scenes/author_volumes.py (a homogeneous medium box in the sampling family room)'
    nodes = []
    for entry in document['nodes']:
        if entry['handle'] in ('lamp', 'lamp-object') and lamp != 'point':
            continue
        if entry['handle'] == 'world' and (world_volume or world_light):
            graph = entry['graph']
            if world_volume:
                graph['nodes'].append({'handle': 'wv', 'type': 'scatter_volume', 'set': {'density': 0.03, 'color': [0.8, 0.8, 0.8], 'anisotropy': 0.2}})
                graph['connect'].append(['wv', 'Volume', 'output', 'Volume'])
            if world_light:
                entry['set'] = {'emission_sampling_method': 'auto'}
                nodes.append(entry)
                nodes.append({'handle': 'world-light', 'create': 'backgroundlight',
                              'set': {'use_mis': True, 'map_resolution': 64, 'strength': [1.0, 1.0, 1.0], 'used_shaders': ['world']}})
                nodes.append({'handle': 'world-light-object', 'create': 'object', 'set': {'geometry': 'world-light'}})
                continue
        nodes.append(entry)
    if lamp == 'sun':
        nodes += [{'handle': 'sun', 'create': 'sunlight', 'set': {'angle': 0.08, 'strength': [3.0, 2.8, 2.5], 'use_mis': True, 'used_shaders': ['emitter']}},
                  {'handle': 'sun-object', 'create': 'object', 'set': {'geometry': 'sun', 'tfm': [1.0, 0.0, 0.0, 0.0, 0.0, 0.8, 0.6, 0.0, 0.0, -0.6, 0.8, 0.0]}}]
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
