#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the render-setting oracle scenes (scenes/render-settings/) byte for byte.

Tiled rendering (provisional, native/host/ABI.md "Tiled rendering"; render.tileSize, cbc_host_tile_size): each tiled
scene renders in Cycles' tiles, whose finished tiles the library keeps in memory and returns as one film; its twin is
the same document without the key, rendered in one tile. A broken tile (a seam, a tile at the wrong place, a pass read
from the wrong tile) would show in the comparison of the two; on the CPU reference the twins' films and passes are
bit for bit the same. Through the public C API.
Usage: python3 native/oracle/scenes/author_render_settings.py [--check]."""
import argparse
import json
import math
from pathlib import Path

import author_starters as starters

HERE = Path(__file__).resolve().parent/'render-settings'


def passes(entries):
    return [dict(handle='pass-'+name, create='pass', set=dict(type=kind, name=name, **extra)) for kind, name, extra in entries]


def glass_passes():
    """The starter glass slab over a diffuse floor, a glossy sphere beside it and a rectangular area light in a light
    group, 64 x 48 at 32 samples with adaptive sampling (each tile renders a 4-pixel overscan), and named passes."""
    floor = [-4, -1, -2, 4, -1, -2, 4, -1, 6, -4, -1, 6]
    slab, slab_indices = starters.box(1.6, 0.4, 1.0)
    sphere, sphere_indices = starters.sphere(0.45, 24, 12)
    nodes = starters.common(64, 48, 0.9, starters.translate(0, 0.5, -4))+starters.world([0.03, 0.04, 0.05], 1.0)
    nodes[0]['set'].update(use_adaptive_sampling=True, adaptive_threshold=0.05, adaptive_min_samples=8)
    nodes += [starters.material('floor-material', 'diffuse_bsdf', dict(color=[0.6, 0.6, 0.6], roughness=0.0)),
              starters.material('glass', 'glass_bsdf', dict(distribution='ggx', roughness=0.1, IOR=1.45, color=[1, 1, 1])),
              starters.material('metal', 'glossy_bsdf', dict(distribution='ggx', roughness=0.25, color=[0.9, 0.7, 0.4]))]
    nodes += starters.mesh('floor', floor, [0, 2, 1, 0, 3, 2], 'floor-material')
    nodes += starters.mesh('slab', slab, slab_indices, 'glass', starters.translate(0, -0.6, 1))
    nodes += starters.mesh('ball', sphere, sphere_indices, 'metal', starters.translate(-1.4, -0.55, 0.6))
    # Area lights emit along their object's -Z; this rotation points -Z down.
    nodes += starters.light('area', 'arealight', dict(sizeu=2.0, sizev=2.0, ellipse=False, spread=math.pi, is_portal=False),
                            [1, 0, 0, 0, 0, 0, 1, 3, 0, -1, 0, 1], [300, 300, 300])
    nodes[-1]['set']['lightgroup'] = 'key'
    nodes += passes([('depth', 'Depth', {}), ('normal', 'Normal', {}), ('diffuse_color', 'DiffCol', {}),
                     ('transmission_direct', 'TransDir', {}), ('glossy_indirect', 'GlossInd', {}),
                     ('sample_count', 'Sample Count', {}), ('combined', 'LG key', dict(lightgroup='key'))])
    return nodes


def border_denoise():
    """The starter glossy sphere under a point light and a diffuse floor, 64 x 48 at 16 samples, a camera border
    (left 0.2, right 0.85, bottom 0.25, top 0.75: 41 x 24 pixels) and Open Image Denoise over the whole region."""
    positions, indices = starters.sphere(1.0, 32, 16)
    nodes = starters.common(64, 48, 0.8, starters.translate(0, 0, -4))+starters.world([0.05, 0.05, 0.05], 1.0)
    nodes[0]['set'].update(use_denoise=True, denoiser_type='openimagedenoise', denoiser_prefilter='accurate', denoiser_quality='high',
                           denoise_start_sample=0, denoise_use_gpu=False)
    nodes[1]['set'].update({'border.left': 0.2, 'border.right': 0.85, 'border.bottom': 0.25, 'border.top': 0.75})
    nodes += [starters.material('glossy', 'glossy_bsdf', dict(distribution='ggx', roughness=0.3, color=[0.8, 0.8, 0.8])),
              starters.material('floor-material', 'diffuse_bsdf', dict(color=[0.5, 0.55, 0.6], roughness=0.0))]
    nodes += starters.mesh('sphere', positions, indices, 'glossy')
    nodes += starters.mesh('floor', [-4, -1, -3, 4, -1, -3, 4, -1, 5, -4, -1, 5], [0, 2, 1, 0, 3, 2], 'floor-material')
    nodes += starters.light('point', 'pointlight', dict(radius=0.25, is_sphere=True), starters.translate(2.5, 3, -3), [400, 400, 400])
    nodes += passes([('combined', 'Noisy Image', dict(mode='noisy')), ('denoising_albedo', 'Denoising Albedo', {}),
                     ('denoising_normal', 'Denoising Normal', {})])
    return nodes


def document(name, description, width, height, samples, nodes, tile_size):
    data = starters.scene('render-settings/'+name, description, width, height, samples, nodes)
    data['sourceFixture'] = 'native/oracle/scenes/author_render_settings.py'
    if tile_size:
        data['render']['tileSize'] = tile_size
    return data


def scenes():
    glass = ('Glass slab, glossy sphere and a diffuse floor under an area light in the light group "key", adaptive sampling, with '
             'depth, normal, diffuse colour, transmission direct, glossy indirect, sample count and light group passes')
    border = ('Glossy sphere and diffuse floor under a point light, a camera border (left 0.2, right 0.85, bottom 0.25, top 0.75) '
              'and Open Image Denoise, with the noisy image and the denoising guide passes')
    return {
        'tiles-glass-passes.json': document('tiles-glass-passes', glass+'; rendered in 12 tiles of 16 pixels (render.tileSize 16), '
                                            'the twin of tiles-glass-passes-untiled', 64, 48, 32, glass_passes(), 16),
        'tiles-glass-passes-untiled.json': document('tiles-glass-passes-untiled', glass+'; one tile, the twin of tiles-glass-passes',
                                                    64, 48, 32, glass_passes(), 0),
        'tiles-border-denoise.json': document('tiles-border-denoise', border+'; the region rendered in 18 tiles of 8 pixels '
                                              '(render.tileSize 8) and denoised once, the twin of tiles-border-denoise-untiled',
                                              64, 48, 16, border_denoise(), 8),
        'tiles-border-denoise-untiled.json': document('tiles-border-denoise-untiled', border+'; one tile, the twin of '
                                                      'tiles-border-denoise', 64, 48, 16, border_denoise(), 0),
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true', help='Fail when a committed scene differs')
    args = parser.parse_args()
    HERE.mkdir(exist_ok=True)
    for name, data in scenes().items():
        path, text = HERE/name, json.dumps(data, indent=1)+'\n'
        if args.check:
            if not path.is_file() or path.read_text() != text:
                raise SystemExit('Scene file differs: '+str(path))
        else:
            path.write_text(text)
        print(name, 'ok')


if __name__ == '__main__':
    main()
