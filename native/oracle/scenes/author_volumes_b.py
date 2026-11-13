#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the second set of volume oracle scenes (scenes/volumes/) byte for byte.

Committed volume scenes rendered again with the sampling patterns other than tabulated Sobol (every random number of
the volume integrators, the heterogeneous null-scattering and ray-marching shadows), with Open Image Denoise (the
volume integrators' denoising features, film_write_denoising_features_volume), with light linking (the receivers of the
volume direct light and of a medium's emission), with a Light Path node in heterogeneous and overlapping homogeneous media
(the extrema estimate of the rays that are not camera rays, volume_estimate_extrema), and a BSDF node in a volume shader's
Volume socket (the volume evaluation steps over it, svm/closure.h svm_node_closure_bsdf_skip), and the heterogeneous box lit
only by the world.
Usage: python3 native/oracle/scenes/author_volumes_b.py [--check]."""
import argparse
import json
from pathlib import Path

import author_volumes as volumes
from author_denoise import DENOISE

HERE = Path(__file__).resolve().parent/'volumes'

# (source scene, sampling pattern): homogeneous and heterogeneous media, distance, equiangular and MIS sampling,
# ray marching, the light tree.
PATTERN_SOURCES = [('volumes/heterogeneous-point', 'sobol_burley'), ('volumes/scatter-point-equiangular', 'blue_noise_first'),
                   ('volumes/ray-marching-heterogeneous-point', 'blue_noise_pure'), ('volumes/tree-lamps', 'sobol_burley')]


def variant(source, pattern):
    document = json.loads((HERE.parent/(source+'.json')).read_text())
    name = source.split('/', 1)[1]
    document['id'] = 'volumes/'+pattern.replace('_', '-')+'-'+name
    document['description'] = pattern+' variant of '+source+': '+document['description']
    document['sourceFixture'] = 'native/oracle/scenes/author_volumes_b.py (from scenes/'+source+'.json)'
    for node in document['nodes']:
        if node.get('scene') == 'integrator':
            node['set']['sampling_pattern'] = pattern
    return document


# Homogeneous, heterogeneous and ray-marched media with Open Image Denoise (as scenes/denoise/: the WebGPU film denoised
# by CPU Open Image Denoise, judged against the CPU film denoised the same way).
DENOISE_SOURCES = ['volumes/scatter-point', 'volumes/heterogeneous-point', 'volumes/ray-marching-heterogeneous-point']


def denoised(source):
    document = json.loads((HERE.parent/(source+'.json')).read_text())
    document['id'] = 'volumes/denoise-'+source.split('/', 1)[1]
    document['description'] = source+' with Open Image Denoise (prefilter accurate, quality high, start sample 0, CPU device): '+document['description']
    document['sourceFixture'] = 'native/oracle/scenes/author_volumes_b.py (from scenes/'+source+'.json)'
    for node in document['nodes']:
        if node.get('scene') == 'integrator':
            node['set'].update(DENOISE)
    return document


# Light linking: the medium box receives light set 1 and the floor set 2 (everything else set 0); the point light and the
# spot light light sets 0 and 1, the sun and the area light sets 0 and 2, so neither of those lights the box; the box's own
# emission (volume-emission) lights set 0 only, so rays the medium scattered do not see it. (source, id, light tree)
LINK_SOURCES = [('volumes/tree-lamps', 'linking-tree-lamps', True), ('volumes/tree-lamps', 'linking-lamps', False),
                ('volumes/volume-emission', 'linking-emission', False)]
LINK_RECEIVERS = {'box-object': 1, 'floor-object': 2}
LINK_MEMBERSHIP = {'lamp-object': 0b011, 'shaped-spot-object': 0b011, 'sun-object': 0b101, 'shaped-area-ellipse-object': 0b101, 'box-object': 0b001}


def linked(source, name, tree):
    document = json.loads((HERE.parent/(source+'.json')).read_text())
    document['id'] = 'volumes/'+name
    document['description'] = source+' with light linking'+('' if tree else ' and the flat light distribution')+': '+document['description']
    document['sourceFixture'] = 'native/oracle/scenes/author_volumes_b.py (from scenes/'+source+'.json)'
    for node in document['nodes']:
        if node.get('scene') == 'integrator':
            node['set']['use_light_tree'] = tree
        if node['handle'] in LINK_RECEIVERS:
            node['set']['receiver_light_set'] = LINK_RECEIVERS[node['handle']]
        if node['handle'] in LINK_MEMBERSHIP:
            node['set']['light_set_membership'] = LINK_MEMBERSHIP[node['handle']]
    return document


def camera_ray_scale(nodes, connect, target, low, high, prefix):
    """Multiply the link into target (handle, socket) by low + (high - low) * Is Camera Ray (a Light Path node)."""
    source = next(c for c in connect if (c[2], c[3]) == target)
    connect.remove(source)
    nodes += [{'handle': prefix+'lp', 'type': 'light_path'},
              {'handle': prefix+'cm', 'type': 'math', 'set': {'math_type': 'multiply', 'value2': high-low}},
              {'handle': prefix+'ca', 'type': 'math', 'set': {'math_type': 'add', 'value2': low}},
              {'handle': prefix+'cd', 'type': 'math', 'set': {'math_type': 'multiply'}}]
    connect += [[prefix+'lp', 'Is Camera Ray', prefix+'cm', 'Value1'], [prefix+'cm', 'Value', prefix+'ca', 'Value1'],
                [source[0], source[1], prefix+'cd', 'Value1'], [prefix+'ca', 'Value', prefix+'cd', 'Value2'], [prefix+'cd', 'Value', target[0], target[1]]]


def light_path(name, description, method='multiple_importance', overlap=False):
    nodes, connect = volumes.noise_medium()
    camera_ray_scale(nodes, connect, ('s', 'Density'), 0.4, 1.3, '')
    document = volumes.scene('volumes/'+name, description, method=method, graph=(nodes, connect))
    if overlap:
        # A second, homogeneous medium box over part of the first, its scatter density 0.35 * (0.4 + 0.9 * Is Camera Ray): both
        # entries are in the paths' stacks there and the octree set-up estimates both. (A world medium with a heterogeneous
        # object crashes the pinned CPU kernel, so the overlap is an object's.)
        second = volumes.medium('medium-2', 0.04, 0.35, 0.2, method)
        second_nodes, second_connect = second['graph']['nodes'], second['graph']['connect']
        second_nodes.append({'handle': 'hv', 'type': 'math', 'set': {'math_type': 'add', 'value1': 0.35, 'value2': 0.0}})
        second_connect.append(['hv', 'Value', 's', 'Density'])
        camera_ray_scale(second_nodes, second_connect, ('s', 'Density'), 0.4, 1.3, 'h')
        corners = [(-0.8, -1.2, -0.6), (2.4, -1.2, -0.6), (-0.8, 0.9, -0.6), (2.4, 0.9, -0.6), (-0.8, -1.2, 1.4), (2.4, -1.2, 1.4), (-0.8, 0.9, 1.4), (2.4, 0.9, 1.4)]
        document['nodes'] += [second, {'handle': 'box-2', 'create': 'mesh', 'mesh': {'positions': [c for p in corners for c in p], 'indices': volumes.BOX_INDICES},
                                       'set': {'used_shaders': ['medium-2']}},
                              {'handle': 'box-2-object', 'create': 'object', 'set': {'geometry': 'box-2'}}]
    return document


def bsdf_in_volume():
    """A Mix Shader of a Principled BSDF and the medium's scatter in the box's Volume socket: the volume shader steps
    over the BSDF node (no closure), so only the scatter half, at the mix weight, makes the medium."""
    nodes = [{'handle': 'a', 'type': 'absorption_volume', 'set': {'density': 0.12, 'color': [0.8, 0.65, 0.4]}},
             {'handle': 's', 'type': 'scatter_volume', 'set': {'density': 0.35, 'color': [0.75, 0.8, 0.9], 'anisotropy': 0.3}},
             {'handle': 'b', 'type': 'principled_bsdf', 'set': {'base_color': [0.9, 0.2, 0.1], 'roughness': 0.4}},
             {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'color': [0.2, 0.9, 0.3], 'roughness': 0.2}},
             {'handle': 'mix', 'type': 'mix_closure', 'set': {'fac': 0.35}},
             {'handle': 'mix2', 'type': 'mix_closure', 'set': {'fac': 0.6}},
             {'handle': 'add', 'type': 'add_closure'}]
    connect = [['b', 'BSDF', 'mix', 'Closure1'], ['s', 'Volume', 'mix', 'Closure2'], ['mix', 'Closure', 'mix2', 'Closure1'],
               ['g', 'BSDF', 'mix2', 'Closure2'], ['a', 'Volume', 'add', 'Closure1'], ['mix2', 'Closure', 'add', 'Closure2'],
               ['add', 'Closure', 'output', 'Volume']]
    return volumes.scene('volumes/bsdf-in-volume', 'BSDF nodes in the medium\'s Volume socket (Principled and Glossy BSDFs mixed with the scatter): '
                         'the volume shader steps over them as the pinned kernel does', graph=(nodes, connect))


# The denoising depth feature through a medium's bounds: every volume-only surface a path crosses writes its depth
# segment (film_write_denoising_features_surface_volume), the first one with the near clip distance, and the
# background behind the box writes what remains of the far clip depth. Lambert surfaces and an absorbing medium, so
# every path is the same on every backend whatever the glossy closures and the scattering do.
def depth_bounds():
    document = json.loads((HERE.parent/'volumes/absorption-point.json').read_text())
    document['id'] = 'volumes/denoising-depth-bounds'
    document['description'] = ('volumes/absorption-point with Lambert surfaces and the denoising depth pass: the depth segments that end at '
                               'the medium\'s bounds, at the surfaces behind them and at the background')
    document['sourceFixture'] = 'native/oracle/scenes/author_volumes_b.py (from scenes/volumes/absorption-point.json)'
    for node in document['nodes']:
        if node.get('create') == 'shader' and node['handle'] in ('wall', 'floor-material', 'glossy', 'glass', 'matte'):
            color = node['graph']['nodes'][0]['set']['color']
            node['graph'] = {'nodes': [{'handle': 'n', 'type': 'diffuse_bsdf', 'set': {'color': color, 'roughness': 0.0}}],
                             'connect': [['n', 'BSDF', 'output', 'Surface']]}
        if node.get('scene') == 'camera':
            node['set'].update({'nearclip': 0.25, 'farclip': 60.0})
    document['nodes'].append({'handle': 'pass-Denoising Depth', 'create': 'pass', 'set': {'type': 'denoising_depth', 'name': 'Denoising Depth'}})
    return document


def scenes():
    documents = {pattern.replace('_', '-')+'-'+source.split('/', 1)[1]+'.json': variant(source, pattern) for source, pattern in PATTERN_SOURCES}
    documents['bsdf-in-volume.json'] = bsdf_in_volume()
    documents['denoising-depth-bounds.json'] = depth_bounds()
    documents.update({'denoise-'+source.split('/', 1)[1]+'.json': denoised(source) for source in DENOISE_SOURCES})
    documents.update({name+'.json': linked(source, name, tree) for source, name, tree in LINK_SOURCES})
    # A heterogeneous medium lit only by the world (no medium emission, so no VOLUME_EMISSION region).
    documents['heterogeneous-world.json'] = volumes.scene('volumes/heterogeneous-world', 'The heterogeneous box lit only by the world through a background light with MIS', lamp='none', world_light=True, graph=volumes.noise_medium())
    documents['light-path-heterogeneous-point.json'] = light_path('light-path-heterogeneous-point', 'The heterogeneous box whose density is 0.4 + 0.9 * Is Camera Ray times the noise density (a Light Path node), point light with MIS: camera rays read the baked octree extrema, scattered and shadow rays estimate them')
    documents['light-path-heterogeneous-equiangular.json'] = light_path('light-path-heterogeneous-equiangular', 'The Light Path heterogeneous box with equiangular sampling only', method='equiangular')
    documents['light-path-overlap.json'] = light_path('light-path-overlap', 'The Light Path heterogeneous box overlapped by a homogeneous medium box whose density also follows Is Camera Ray: the octree set-up estimates both stack entries', overlap=True)
    return documents


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
