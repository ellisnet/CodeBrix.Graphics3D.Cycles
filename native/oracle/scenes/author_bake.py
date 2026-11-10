#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the texture baking oracle scenes (scenes/bake/) byte for byte.

Bakes in the sampling family's room (a sphere lamp with MIS, a constant world, glossy, glass and diffuse spheres):
the target is a UV-mapped sphere or the floor, baked through the public C API (cbc_host_bake) at the render size and
samples. A bake scene's "render" block names the bake: {"bake": {"object": HANDLE, "type": NAME, "flags": [...],
"uv": NAME, "aov": NAME}}; the runner (native/oracle/runner.cpp) writes each repeat's bake as the film, with the
bake's components per pixel (meta.json "filmComponents"). Every bake type the host exposes, the camera view and
albedo flags, a named UV layout, an instanced target, a texture, the AOVs, adaptive sampling, denoising and the
shadow catcher.
Usage: python3 native/oracle/scenes/author_bake.py [--check]."""
import argparse
import json
import math
from pathlib import Path

import author_sampling as room

HERE = Path(__file__).resolve().parent/'bake'
f32 = room.f32


def uv_sphere(handle, material, center, radius, stacks=8, slices=12, smooth=True):
    """A sphere with a corner UV layout: (longitude, latitude), each face a rectangle of the [0, 1] square. The zero-area
    triangles at the poles are left out (a texel on one is refused on the WebGPU device, native/DEVICE.md item 115)."""
    positions, indices, uv = [], [], []
    for i in range(stacks+1):
        for j in range(slices+1):
            th, ph = math.pi*i/stacks, 2*math.pi*j/slices
            p = (math.sin(th)*math.cos(ph), math.cos(th), math.sin(th)*math.sin(ph))
            positions += [f32(center[k]+radius*p[k]) for k in range(3)]
    for i in range(stacks):
        for j in range(slices):
            a = i*(slices+1)+j
            b = a+slices+1
            corner = lambda r, c: [f32((c+0.02)/(slices+0.04)), f32(1.0-(r+0.02)/(stacks+0.04))]
            if i > 0:
                indices += [a, b, a+1]
                uv += corner(i, j)+corner(i+1, j)+corner(i, j+1)
            if i < stacks-1:
                indices += [a+1, b, b+1]
                uv += corner(i, j+1)+corner(i+1, j)+corner(i+1, j+1)
    faces = len(indices)//3
    return {'handle': handle, 'create': 'mesh',
            'mesh': {'positions': positions, 'indices': indices,
                     'attributes': [{'name': 'uv', 'value': 'point2', 'domain': 'corner', 'flags': ['standard'], 'data': uv}]},
            'set': {'used_shaders': [material], 'smooth': [smooth]*faces}}


def floor_uv():
    """The room's floor with a standard UV layout and a second, rotated one (named, vertex domain)."""
    return {'handle': 'floor', 'create': 'mesh',
            'mesh': {'positions': [c for p in room.FLOOR for c in p], 'indices': [0, 1, 2, 0, 2, 3],
                     'attributes': [{'name': 'uv', 'value': 'point2', 'domain': 'corner', 'flags': ['standard'],
                                     'data': [0.0, 0.0, 1.0, 0.0, 1.0, 1.0, 0.0, 0.0, 1.0, 1.0, 0.0, 1.0]},
                                    {'name': 'turned', 'value': 'point2', 'domain': 'vertex',
                                     'data': [0.1, 0.5, 0.5, 0.1, 0.9, 0.5, 0.5, 0.9]}]},
            'set': {'used_shaders': ['floor-material']}}


def textured():
    return room.shader('textured', [{'handle': 'c', 'type': 'checker_texture', 'set': {'scale': 4.0, 'color1': [0.8, 0.3, 0.2], 'color2': [0.2, 0.5, 0.7]}},
                                    {'handle': 't', 'type': 'texture_coordinate'},
                                    {'handle': 'd', 'type': 'diffuse_bsdf', 'set': {'roughness': 0.2}},
                                    {'handle': 'g', 'type': 'glossy_bsdf', 'set': {'distribution': 'ggx', 'roughness': 0.3, 'color': [0.8, 0.8, 0.7]}},
                                    {'handle': 'r', 'type': 'refraction_bsdf', 'set': {'distribution': 'ggx', 'roughness': 0.2, 'IOR': 1.4, 'color': [0.6, 0.7, 0.9]}},
                                    {'handle': 'm1', 'type': 'mix_closure', 'set': {'fac': 0.35}}, {'handle': 'm2', 'type': 'mix_closure', 'set': {'fac': 0.25}},
                                    {'handle': 'ac', 'type': 'aov_output', 'set': {'name': 'Tint'}}, {'handle': 'av', 'type': 'aov_output', 'set': {'name': 'Mask'}}],
                       [['t', 'UV', 'c', 'Vector'], ['c', 'Color', 'd', 'Color'], ['d', 'BSDF', 'm1', 'Closure1'], ['g', 'BSDF', 'm1', 'Closure2'],
                        ['m1', 'Closure', 'm2', 'Closure1'], ['r', 'BSDF', 'm2', 'Closure2'], ['m2', 'Closure', 'output', 'Surface'],
                        ['c', 'Color', 'ac', 'Color'], ['c', 'Fac', 'av', 'Value']])


def bake_scene(scene_id, description, bake, integrator=None, samples=16, width=32, height=24, target='ball', extra=(), shared=False):
    """The room with the spheres; sphere 1 (glass in the sampling scenes) replaced by the UV-mapped bake target 'ball'."""
    document = room.scene(scene_id, description, integrator or {}, samples=samples, width=width, height=height)
    document['sourceFixture'] = 'native/oracle/scenes/author_bake.py (bakes in the sampling family room)'
    nodes = [n for n in document['nodes'] if not n['handle'].startswith('sphere-1') and n['handle'] not in ('floor', 'floor-object')]
    material = textured()
    material['set']['pass_id'] = 9
    nodes += [material, uv_sphere('ball', 'textured', (0.0, -0.9, 0.4), 1.0), {'handle': 'ball-object', 'create': 'object', 'set': {'geometry': 'ball', 'pass_id': 7}},
              floor_uv(), {'handle': 'floor-object', 'create': 'object', 'set': {'geometry': 'floor'}}, {'handle': 'aov-tint', 'create': 'pass', 'set': {'type': 'aov_color', 'name': 'Tint'}},
              {'handle': 'aov-mask', 'create': 'pass', 'set': {'type': 'aov_value', 'name': 'Mask'}}]
    if shared:
        # A second user of the ball's mesh: the target keeps its transform (not applied to the vertices).
        nodes.append({'handle': 'ball-twin', 'create': 'object', 'set': {'geometry': 'ball', 'tfm': [0.5, 0.0, 0.0, 2.6, 0.0, 0.5, 0.0, 1.2, 0.0, 0.0, 0.5, 1.5]}})
    nodes += list(extra)
    document['nodes'] = nodes
    block = {'object': target+'-object' if target != 'floor' else 'floor-object', 'type': bake['type']}
    for key in ('flags', 'uv', 'aov'):
        if key in bake:
            block[key] = bake[key]
    document['render']['bake'] = block
    return document


def scenes():
    out = {}
    types = ['combined', 'emission', 'background', 'ao', 'diffuse', 'diffuse_direct', 'diffuse_indirect', 'diffuse_color', 'glossy', 'glossy_direct',
             'glossy_indirect', 'glossy_color', 'transmission', 'transmission_direct', 'transmission_indirect', 'transmission_color', 'position', 'normal',
             'uv', 'roughness', 'object_id', 'material_id', 'depth', 'mist']
    for kind in types:
        out[kind.replace('_', '-')+'.json'] = bake_scene('bake/'+kind.replace('_', '-'), 'The '+kind+' bake type of a textured, lit sphere', {'type': kind})
    out['aov-color.json'] = bake_scene('bake/aov-color', 'The colour AOV of the sphere (its checker colour)', {'type': 'aov_color', 'aov': 'Tint'})
    out['aov-value.json'] = bake_scene('bake/aov-value', 'The value AOV of the sphere (its checker factor)', {'type': 'aov_value', 'aov': 'Mask'})
    out['combined-camera.json'] = bake_scene('bake/combined-camera', 'Combined with the view direction towards the scene camera', {'type': 'combined', 'flags': ['use_camera']})
    out['diffuse-albedo.json'] = bake_scene('bake/diffuse-albedo', 'Diffuse light with the surface colour kept (include_albedo)', {'type': 'diffuse', 'flags': ['include_albedo']})
    out['glossy-camera-albedo.json'] = bake_scene('bake/glossy-camera-albedo', 'Glossy light towards the camera, with albedo', {'type': 'glossy', 'flags': ['use_camera', 'include_albedo']})
    out['instanced.json'] = bake_scene('bake/instanced', 'Combined of a target whose mesh has a second user (the kernel applies the object transform)', {'type': 'combined'}, shared=True)
    out['floor-named-uv.json'] = bake_scene('bake/floor-named-uv', 'The floor through a named vertex-domain UV layout', {'type': 'combined', 'uv': 'turned'}, target='floor')
    out['adaptive.json'] = bake_scene('bake/adaptive', 'Combined with adaptive sampling (threshold 0.05, minimum 8 samples): convergence in the bake\'s UV space',
                                      {'type': 'combined'}, integrator=room.adaptive(0.05, 8), samples=64)
    out['denoise.json'] = bake_scene('bake/denoise', 'Combined, denoised (Open Image Denoise on the CPU, as for a render): an image operation in UV space',
                                     {'type': 'combined'}, integrator={'use_denoise': True}, samples=16)
    catcher = [{'handle': 'bg-transparent', 'scene': 'background', 'set': {'transparent': True}}]
    document = bake_scene('bake/shadow-catcher', 'Combined of a shadow catcher target with a transparent world (the split at the first hit)', {'type': 'combined'}, extra=catcher)
    for n in document['nodes']:
        if n['handle'] == 'ball-object':
            n['set']['is_shadow_catcher'] = True
    out['shadow-catcher.json'] = document
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
