#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the sampling oracle scenes (scenes/sampling/) byte for byte.

Adaptive sampling (the convergence check and the box filter at the pinned
filter samples) and the sampling patterns other than tabulated Sobol
(Sobol-Burley, the blue-noise patterns, scrambling distance): a diffuse room
with glossy, glass and diffuse spheres, lit by a sphere lamp with MIS and a
constant world, through the public C API.
Usage: python3 native/oracle/scenes/author_sampling.py [--check]."""
import argparse
import json
import math
import struct
from pathlib import Path

HERE = Path(__file__).resolve().parent/'sampling'


def f32(v):
    return round(struct.unpack('<f', struct.pack('<f', v))[0], 7)


def shader(handle, nodes, connect):
    return {'handle': handle, 'create': 'shader', 'graph': {'nodes': nodes, 'connect': connect}, 'set': {'emission_sampling_method': 'none'}}


def single(handle, node_type, values, output='BSDF'):
    return shader(handle, [{'handle': 'n', 'type': node_type, 'set': values}], [['n', output, 'output', 'Surface']])


def sphere(handle, material, center, radius, stacks=10, slices=14):
    positions, indices = [], []
    for i in range(stacks+1):
        for j in range(slices+1):
            th, ph = math.pi*i/stacks, 2*math.pi*j/slices
            p = (math.sin(th)*math.cos(ph), math.cos(th), math.sin(th)*math.sin(ph))
            positions += [f32(center[k]+radius*p[k]) for k in range(3)]
    for i in range(stacks):
        for j in range(slices):
            a = i*(slices+1)+j
            b = a+slices+1
            indices += [a, b, a+1, a+1, b, b+1]
    faces = len(indices)//3
    return [{'handle': handle, 'create': 'mesh', 'mesh': {'positions': positions, 'indices': indices},
             'set': {'used_shaders': [material], 'smooth': [True]*faces}},
            {'handle': handle+'-object', 'create': 'object', 'set': {'geometry': handle}}]


def quad(handle, material, corners):
    return [{'handle': handle, 'create': 'mesh', 'mesh': {'positions': [c for p in corners for c in p], 'indices': [0, 1, 2, 0, 2, 3]},
             'set': {'used_shaders': [material]}},
            {'handle': handle+'-object', 'create': 'object', 'set': {'geometry': handle}}]


BACK = [(-4, -3, 3), (4, -3, 3), (4, 3, 3), (-4, 3, 3)]
FLOOR = [(-4, -2, -2), (4, -2, -2), (4, -2, 3), (-4, -2, 3)]


def scene(scene_id, description, integrator, samples=8, width=32, height=24, sample_count=False):
    """Glossy, glass and diffuse spheres in a diffuse room; integrator settings added to the fixed ones."""
    settings = {'use_guiding': False, 'use_light_tree': False, 'use_adaptive_sampling': False, 'use_denoise': False,
                'min_bounce': 2, 'max_bounce': 4, 'filter_glossy': 0.0, 'caustics_reflective': True, 'caustics_refractive': True,
                'sampling_pattern': 'tabulated_sobol', 'scrambling_distance': 1.0}
    settings.update(integrator)
    nodes = [{'handle': 'integrator', 'scene': 'integrator', 'set': settings},
             # Film use_sample_count is Cycles' session state (the host refuses a changing set): only the default is written.
             {'handle': 'film', 'scene': 'film', 'set': {} if sample_count else {'use_sample_count': False}},
             {'handle': 'camera', 'scene': 'camera', 'set': {'fov': 0.9, 'matrix': [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, -7.0]}},
             single('wall', 'diffuse_bsdf', {'color': [0.6, 0.62, 0.65], 'roughness': 0.25}),
             single('floor-material', 'diffuse_bsdf', {'color': [0.5, 0.45, 0.35], 'roughness': 0.0}),
             single('glossy', 'glossy_bsdf', {'distribution': 'ggx', 'roughness': 0.25, 'color': [0.85, 0.7, 0.4]}),
             single('glass', 'glass_bsdf', {'distribution': 'ggx', 'roughness': 0.1, 'IOR': 1.45, 'color': [0.9, 0.95, 0.95]}),
             single('matte', 'diffuse_bsdf', {'color': [0.4, 0.55, 0.7], 'roughness': 0.4}),
             *quad('back', 'wall', BACK), *quad('floor', 'floor-material', FLOOR)]
    if sample_count:
        # The per-pixel sample count: equal counts mean equal convergence decisions.
        nodes.append({'handle': 'pass-Sample Count', 'create': 'pass', 'set': {'type': 'sample_count', 'name': 'Sample Count'}})
    for k, m in enumerate(('glossy', 'glass', 'matte')):
        nodes += sphere('sphere-'+str(k), m, (-2.3+2.3*k, -0.9, 0.4), 1.0)
    nodes += [shader('world', [{'handle': 'b', 'type': 'background_shader', 'set': {'color': [0.3, 0.35, 0.42], 'strength': 1.0}}],
                     [['b', 'Background', 'output', 'Surface']]),
              {'handle': 'bg', 'scene': 'background', 'set': {'shader': 'world'}},
              shader('emitter', [{'handle': 'e', 'type': 'emission', 'set': {'color': [1, 1, 1], 'strength': 1}}], [['e', 'Emission', 'output', 'Surface']]),
              {'handle': 'lamp', 'create': 'pointlight', 'set': {'radius': 0.45, 'is_sphere': True, 'use_mis': True, 'strength': [110, 90, 70], 'used_shaders': ['emitter']}},
              {'handle': 'lamp-object', 'create': 'object', 'set': {'geometry': 'lamp', 'tfm': [1.0, 0.0, 0.0, 2.2, 0.0, 1.0, 0.0, 2.0, 0.0, 0.0, 1.0, -2.0]}}]
    return {'cbcOracleScene': 1, 'id': scene_id, 'description': description,
            'sourceFixture': 'native/oracle/scenes/author_sampling.py (glossy, glass and diffuse spheres in a lit room)',
            'render': {'width': width, 'height': height, 'samples': samples, 'repeats': 2}, 'colorConfig': None, 'nodes': nodes, 'images': []}


def adaptive(threshold, min_samples):
    return {'use_adaptive_sampling': True, 'adaptive_threshold': threshold, 'adaptive_min_samples': min_samples}


def scenes():
    out = {}
    out['adaptive-threshold.json'] = scene(
        'sampling/adaptive-threshold', 'Adaptive sampling, threshold 0.05, minimum 8 samples: convergence checks and filtering at samples 16, 32, 48 and 64',
        adaptive(0.05, 8), samples=64, sample_count=True)
    out['adaptive-auto.json'] = scene(
        'sampling/adaptive-auto', 'Adaptive sampling with the automatic threshold (1/samples) and automatic minimum samples',
        adaptive(0.0, 0), samples=96, sample_count=True)
    out['adaptive-coarse.json'] = scene(
        'sampling/adaptive-coarse', 'Adaptive sampling, threshold 0.2, minimum 4 samples: every pixel converges at the first check (sample 16), which ends the render',
        adaptive(0.2, 4), samples=48, sample_count=True)
    out['sobol-burley.json'] = scene('sampling/sobol-burley', 'Sobol-Burley sampling pattern (one Owen-scrambled sequence per pixel, length mask)',
                                     {'sampling_pattern': 'sobol_burley'}, samples=16)
    out['blue-noise-pure.json'] = scene('sampling/blue-noise-pure', 'Blue-noise pure pattern (one sequence, a section per pixel along a scrambled Morton curve)',
                                        {'sampling_pattern': 'blue_noise_pure'}, samples=16)
    out['blue-noise-first.json'] = scene('sampling/blue-noise-first', 'Blue-noise first pattern (a 1-sample blue-noise sequence for sample 0, then the rest)',
                                         {'sampling_pattern': 'blue_noise_first'}, samples=16)
    out['blue-noise-round.json'] = scene('sampling/blue-noise-round', 'Blue-noise round pattern with 12 samples (sequence length rounded up to 16)',
                                         {'sampling_pattern': 'blue_noise_round'}, samples=12)
    out['scrambling-distance.json'] = scene('sampling/scrambling-distance', 'Tabulated Sobol with scrambling distance 0.25 (shared sequence, limited rotation, sample-major work order)',
                                            {'scrambling_distance': 0.25}, samples=16)
    out['adaptive-sobol-burley.json'] = scene('sampling/adaptive-sobol-burley', 'Adaptive sampling (threshold 0.05, minimum 8 samples) with the Sobol-Burley pattern',
                                              {**adaptive(0.05, 8), 'sampling_pattern': 'sobol_burley'}, samples=64, sample_count=True)
    out['adaptive-blue-noise.json'] = scene('sampling/adaptive-blue-noise', 'Adaptive sampling (threshold 0.05, minimum 8 samples) with the blue-noise pure pattern',
                                            {**adaptive(0.05, 8), 'sampling_pattern': 'blue_noise_pure'}, samples=64, sample_count=True)
    out['tabulated-sobol-room.json'] = scene('sampling/tabulated-sobol-room', 'The room with the fixed integrator settings only (tabulated Sobol, 8 samples) at 64 x 48: '
                                             'a near-peak highlight of the sphere lamp on the rough (0.1) glass sphere, where one last bit of the '
                                             'light direction moves the GGX distribution by about 9e-4 (native/DEVICE.md, item 152)', {}, samples=8, width=64, height=48)
    return out


# Committed scenes rendered again with another sampling pattern: every random-number site of the device
# (light sampling and termination, the BSDF pick, ambient occlusion and the AO and Bevel nodes, MNEE,
# camera motion, the shadow catcher, light linking, textures and environments).
VARIANT_SOURCES = [('mnee/glass-sphere-caustic', 'sobol_burley'), ('ao/fast-gi-additive', 'blue_noise_pure'),
                   ('surface-nodes/bevel', 'sobol_burley'), ('ao/ao-node', 'blue_noise_first'),
                   ('light-tree/lamps-and-mesh-lights', 'blue_noise_pure'), ('combinations/camera-motion-fast-gi-additive', 'sobol_burley'),
                   ('shadow-catcher/catcher-point-light', 'blue_noise_first'), ('light-motion/clamps-filters-exclude', 'sobol_burley'),
                   ('light-motion/linking-hdri', 'blue_noise_pure'), ('combinations/tree-textured-plane-normal-map', 'sobol_burley')]


def variant(source, pattern):
    family = source.split('/')[0]
    document = json.loads((HERE.parent/(source+'.json')).read_text())
    document['id'] = 'sampling/'+pattern.replace('_', '-')+'-'+source.replace('/', '-')
    document['description'] = pattern+' variant of '+source+': '+document['description']
    document['sourceFixture'] = 'native/oracle/scenes/author_sampling.py (from scenes/'+source+'.json)'
    # Raw files stay beside the source scene.
    for image in document.get('images', []):
        image['file'] = '../'+family+'/'+image['file']
    for node in document['nodes']:
        if node.get('scene') == 'integrator':
            node['set']['sampling_pattern'] = pattern
    return document


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true', help='Fail when a committed scene differs')
    args = parser.parse_args()
    HERE.mkdir(exist_ok=True)
    documents = scenes()
    documents.update({pattern.replace('_', '-')+'-'+source.replace('/', '-')+'.json': variant(source, pattern) for source, pattern in VARIANT_SOURCES})
    for name, document in documents.items():
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
