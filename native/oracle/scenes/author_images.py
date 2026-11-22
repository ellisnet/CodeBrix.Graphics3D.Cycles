#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Write the image binding oracle scenes (scenes/images/) and their raw pixel files byte for byte.

UDIM tiles (provisional, native/host/ABI.md "Images"): several images bound to one Image Texture node through the `image`
pseudo-socket's object form, {"1001": HANDLE, ...} (cbc_host_image_bind_tiles), on the diffuse plane of
starter/textured-plane under its constant world. The tile covering UV [u, u + 1) x [v, v + 1) is 1001 + 10 * v + u; a UV
outside every bound tile reads the node's Missing inputs. The texture limit (provisional, render.textureLimit,
cbc_host_texture_limit): Cycles halves an image whose larger side exceeds the limit when it loads it, on the same plane.
Every pixel is built from a fixed formula (bytes, or floats rounded through binary32), so every platform gets the same
files. Tiled images (provisional, native/host/ABI.md "Tiled images"): an images entry with `tiled` is read in tiles
and mip levels through the tile callback (cbc_host_image_create_tiled); the levels are raw files this script writes, 2 x 2
box reductions in integers. Through the public C API.
Usage: python3 native/oracle/scenes/author_images.py [--check]."""
import argparse
import json
import struct
from pathlib import Path

import author_starters as starters

HERE = Path(__file__).resolve().parent/'images'
PROVENANCE = 'native/oracle/scenes/author_images.py (formula); rows bottom-up'


def f32(value):
    return struct.unpack('<f', struct.pack('<f', value))[0]


def solid_float4(color, size=2):
    """A size x size float4 image of one colour, alpha 1."""
    return b''.join(struct.pack('<4f', *[f32(c) for c in color], 1.0) for _ in range(size*size))


def checker_byte4(size, cell, first, second):
    """A size x size byte4 checker of cell x cell squares, the lower-left square of colour first; alpha 255."""
    pixels = bytearray()
    for y in range(size):
        for x in range(size):
            pixels += bytes(first if ((x//cell)+(y//cell)) % 2 == 0 else second)+b'\xff'
    return bytes(pixels)


def image(scene_name, handle, width, height, fmt, data):
    """An images entry and its raw file (name beside the scene, content)."""
    name = '%s.%s.%s' % (scene_name, handle, fmt)
    return dict(handle=handle, width=width, height=height, format=fmt, file=name, provenance=PROVENANCE), (name, data)


def plane(handle, shader, corners, uvs):
    """A quad (corners x0, y0, x1, y1 at z 0) with standard corner UVs: uvs gives the UV of its lower-left and
    upper-right corners."""
    (x0, y0, x1, y1), (u0, v0, u1, v1) = corners, uvs
    uv = dict(name='uv', value='point2', domain='corner', flags=['standard'],
              data=[u0, v0, u1, v1, u1, v0, u0, v0, u0, v1, u1, v1])
    return starters.mesh(handle, [x0, y0, 0, x1, y0, 0, x1, y1, 0, x0, y1, 0], [0, 2, 1, 0, 3, 2], shader, attributes=[uv])


def textured(tiles, interpolation, extension='periodic', missing=None, colorspace='scene_linear'):
    """The starter textured plane's diffuse material, its image texture bound to tiles ({number: image handle}) or, for a
    plain binding, to the image handle tiles names."""
    settings = dict(image=tiles if isinstance(tiles, str) else {str(number): handle for number, handle in tiles}, colorspace=colorspace,
                    alpha_type='unassociated',
                    interpolation=interpolation, extension=extension)
    if missing:
        settings.update(missing=missing[:3], missing_alpha=missing[3])
    return dict(handle='textured', create='shader', graph=dict(nodes=[
        dict(handle='d', type='diffuse_bsdf', set=dict(roughness=0.0)),
        dict(handle='t', type='image_texture', set=settings),
        dict(handle='uv', type='uvmap')],
        connect=[['uv', 'UV', 't', 'Vector'], ['t', 'Color', 'd', 'Color'], ['d', 'BSDF', 'output', 'Surface']]),
        set=dict(emission_sampling_method='none'))


def document(scene_id, description, nodes, images, fixture='UDIM tiles on the starter textured plane', texture_limit=None):
    nodes = starters.common(48, 32, 0.9, starters.translate(0, 0, -3))+starters.world([0.2, 0.2, 0.2], 1.0)+nodes
    data = starters.scene(scene_id, description, 48, 32, 8, nodes, images)
    data['sourceFixture'] = 'native/oracle/scenes/author_images.py (%s)' % fixture
    if texture_limit is not None:
        data['render']['textureLimit'] = texture_limit
    return data


def three_tiles():
    name = 'udim-three-tiles'
    colors = [(0.8, 0.15, 0.1), (0.15, 0.7, 0.2), (0.15, 0.25, 0.85)]
    entries = [image(name, 'tile-%d' % (1001+k), 2, 2, 'float4', solid_float4(c)) for k, c in enumerate(colors)]
    nodes = [textured([(1001+k, 'tile-%d' % (1001+k)) for k in range(3)], 'closest')]
    nodes += plane('plane', 'textured', (-1.5, -1, 1.5, 1), (0, 0, 3, 1))
    return document('images/'+name, 'Three UDIM tiles of one solid float4 colour each (1001, 1002, 1003) bound to one Image '
                    'Texture node with Closest interpolation, on a diffuse plane whose UVs run from 0 to 3 across',
                    nodes, [e for e, _ in entries]), [f for _, f in entries]


def checker_linear():
    name = 'udim-checker-linear'
    tiles = [1001, 1002, 1003, 1011, 1012, 1013]
    entries = []
    for k, number in enumerate(tiles):
        first = (220-30*k, 40+35*k, 60+25*k)
        second = (30+20*k, 200-25*k, 230-35*k)
        entries.append(image(name, 'tile-%d' % number, 16, 16, 'byte4', checker_byte4(16, 2+k % 3, first, second)))
    nodes = [textured([(number, 'tile-%d' % number) for number in tiles], 'linear', colorspace='__builtin_srgb')]
    nodes += plane('plane', 'textured', (-1.5, -1, 1.5, 1), (0, 0, 3, 2))
    return document('images/'+name, 'Six UDIM tiles (1001 to 1003 and 1011 to 1013), each a 16x16 sRGB byte4 checker of its own '
                    'colours and cell size, bound to one Image Texture node with Linear interpolation and Periodic extension (each '
                    'tile wraps on itself), on a diffuse plane whose UVs run from 0 to 3 across and 0 to 2 up',
                    nodes, [e for e, _ in entries]), [f for _, f in entries]


def missing_tile():
    name = 'udim-missing-tile'
    entries = [image(name, 'a', 4, 4, 'byte4', checker_byte4(4, 1, (210, 60, 40), (240, 200, 60))),
               image(name, 'b', 4, 4, 'byte4', checker_byte4(4, 2, (40, 90, 220), (90, 220, 230))),
               image(name, 'c', 4, 4, 'byte4', checker_byte4(4, 1, (60, 170, 70), (20, 60, 30)))]
    # Image a at tiles 1003 and 1001 (two device images of one image), 1002 unbound; b and c at 1019 and 1020 (u 8 and 9 of
    # v 1); the lower quad's UVs reach u 11.5, where u >= 10 has no tile.
    nodes = [textured([(1003, 'a'), (1001, 'a'), (1019, 'b'), (1020, 'c')], 'closest', missing=(0.9, 0.2, 0.7, 1.0))]
    nodes += plane('upper', 'textured', (-1.5, 0.05, 1.5, 1), (0, 0, 3, 1))
    nodes += plane('lower', 'textured', (-1.5, -1, 1.5, -0.05), (8.5, 1, 11.5, 2))
    return document('images/'+name, 'UDIM tiles with a gap and an image used twice: image a at tiles 1001 and 1003 and none at 1002 '
                    '(the upper quad, UVs 0 to 3), images b and c at 1019 and 1020 (the lower quad, UVs 8.5 to 11.5 at v 1 to 2, '
                    'beyond u 10 no tile exists); the UVs without an image read the texture\'s Missing colour',
                    nodes, [e for e, _ in entries]), [f for _, f in entries]


def blocks_byte4(size, block):
    """A size x size byte4 image of uniform block x block squares, each square's colour from its position; alpha 255."""
    pixels = bytearray()
    for y in range(size):
        for x in range(size):
            bx, by = x//block, y//block
            pixels += bytes(((17*bx+3*by) % 256, (255-9*by-2*bx) % 256, (37*bx+53*by) % 256, 255))
    return bytes(pixels)


def fine_on_coarse_byte4(size, coarse, first, second):
    """A size x size byte4 checker of coarse x coarse squares of colours first and second, each square itself a checker of
    single pixels 24 levels lighter and darker than its colour; alpha 255."""
    pixels = bytearray()
    for y in range(size):
        for x in range(size):
            base = first if ((x//coarse)+(y//coarse)) % 2 == 0 else second
            shift = 24 if (x+y) % 2 == 0 else -24
            pixels += bytes(min(255, max(0, c+shift)) for c in base)+b'\xff'
    return bytes(pixels)


def texture_limit(name, description, entry, limit):
    nodes = [textured('image', 'linear')]
    nodes += plane('plane', 'textured', (-1.5, -1, 1.5, 1), (0, 0, 1, 1))
    return document('images/'+name, description, nodes, [entry[0]], 'the texture limit on the starter textured plane',
                    limit), [entry[1]]


def texture_limit_blocks():
    name = 'texture-limit-blocks'
    return texture_limit(name, 'A 64x64 byte4 image of uniform 2x2 blocks with Linear interpolation on a diffuse plane, under a texture '
                         'limit of 32: Cycles halves it when it loads it, to the image of the block colours, so the CPU film equals '
                         'that of texture-limit-blocks-halved bit for bit', image(name, 'image', 64, 64, 'byte4', blocks_byte4(64, 2)), 32)


def texture_limit_blocks_halved():
    name = 'texture-limit-blocks-halved'
    return texture_limit(name, 'The twin of texture-limit-blocks without a texture limit: the 32x32 byte4 image of its block colours, '
                         'Linear interpolation, on the same diffuse plane', image(name, 'image', 32, 32, 'byte4', blocks_byte4(32, 1)), None)


def texture_limit_checker():
    name = 'texture-limit-checker'
    return texture_limit(name, 'A 64x64 byte4 checker of 16x16 squares, each square a checker of single pixels 24 levels lighter and '
                         'darker, with Linear interpolation on a diffuse plane, under a texture limit of 16: Cycles quarters it when it '
                         'loads it, so the single-pixel pattern averages away and a flat 16x16 checker of 4x4 squares renders',
                         image(name, 'image', 64, 64, 'byte4', fine_on_coarse_byte4(64, 16, (200, 70, 50), (60, 120, 210))), 16)


# The tiled scenes render on one thread (render.threads): a Linear or Cubic lookup of a tiled image depends in its last
# bits on the pool slot Cycles' texture cache gave the tile, which follows the order of the loads; one CPU thread fixes it.
TILED_THREADS = 1


def box_levels(width, height, level0, tile=64):
    """The mip levels of a byte4 image for a tiled image entry: level 0 is the image, each further level the 2 x 2 box
    reduction of the one before (integer mean, half up; a side of 1 repeats its texel), down to the first level whose sides
    are both at most the tile size. Returns [(width, height, bytes)]."""
    levels = [(width, height, level0)]
    while levels[-1][0] > tile or levels[-1][1] > tile:
        sw, sh, source = levels[-1]
        dw, dh = max(1, sw >> 1), max(1, sh >> 1)
        pixels = bytearray()
        for y in range(dh):
            for x in range(dw):
                taps = [(min(2*x+i, sw-1), min(2*y+j, sh-1)) for j in (0, 1) for i in (0, 1)]
                for c in range(4):
                    pixels.append((sum(source[(ty*sw+tx)*4+c] for tx, ty in taps)+2)//4)
        levels.append((dw, dh, bytes(pixels)))
    return levels


def tiled_image(scene_name, handle, width, height, level0, tile=64):
    """A tiled byte4 images entry (provisional, native/host/ABI.md "Tiled images") and its raw level files."""
    levels = box_levels(width, height, level0, tile)
    names = ['%s.%s.level-%d.byte4' % (scene_name, handle, k) for k in range(len(levels))]
    entry = dict(handle=handle, width=width, height=height, format='byte4', file=names[0],
                 tiled=dict(tileSize=tile, levels=names[1:]), provenance=PROVENANCE+'; levels: 2 x 2 box reductions')
    return entry, [(name, data) for name, (_, _, data) in zip(names, levels)]


def detail_byte4(width, height):
    """A width x height byte4 image whose 16 x 16 blocks take their colour from their position, with a single-pixel
    checker 24 levels lighter and darker inside (so every mip level differs from the one above) and a darker line on
    every 64th row and column (the tile borders of the default tile size); alpha 255."""
    pixels = bytearray()
    for y in range(height):
        for x in range(width):
            bx, by = x//16, y//16
            base = ((40+17*bx+5*by) % 200+28, (228-13*by-3*bx) % 200+28, (60+29*bx+41*by) % 200+28)
            shift = 24 if (x+y) % 2 == 0 else -24
            if x % 64 in (0, 63) or y % 64 in (0, 63):
                base = tuple(c//3 for c in base)
            pixels += bytes(min(255, max(0, c+shift)) for c in base)+b'\xff'
    return bytes(pixels)


def image_shader(handle, image_handle, interpolation, extension='periodic', colorspace='scene_linear', emission=None):
    """A diffuse material (or, with emission, an emission material of that strength) coloured by an image texture."""
    settings = dict(image=image_handle, colorspace=colorspace, alpha_type='unassociated', interpolation=interpolation, extension=extension)
    closure = (dict(handle='d', type='emission', set=dict(strength=emission)), 'Emission') if emission is not None else \
        (dict(handle='d', type='diffuse_bsdf', set=dict(roughness=0.0)), 'BSDF')
    return dict(handle=handle, create='shader', graph=dict(nodes=[
        closure[0], dict(handle='t', type='image_texture', set=settings), dict(handle='uv', type='uvmap')],
        connect=[['uv', 'UV', 't', 'Vector'], ['t', 'Color', 'd', 'Color'], ['d', closure[1], 'output', 'Surface']]),
        set=dict(emission_sampling_method='none' if emission is None else 'forward'))


def tiled_mip_planes(name='tiled-mip-planes', limit=None):
    entry, files = tiled_image(name, 'image', 192, 128, detail_byte4(192, 128))
    nodes = [image_shader('linear', 'image', 'linear'), image_shader('cubic', 'image', 'cubic'), image_shader('closest', 'image', 'closest')]

    def quad(handle, shader, column, row, centre, span):
        x, y = -1.0+column, -0.5+row
        (cu, cv), h = centre, span/2
        return plane(handle, shader, (x-0.46, y-0.46, x+0.46, y+0.46), (cu-h, cv-h, cu+h, cv+h))
    corner = (64/192, 0.5)
    nodes += quad('whole', 'linear', 0, 1, (0.5, 0.5), 1.0)
    nodes += quad('half', 'linear', 1, 1, (0.5, 0.5), 0.464)
    nodes += quad('quarter', 'linear', 2, 1, corner, 0.232)
    nodes += quad('cubic-corner', 'cubic', 0, 0, corner, 0.12)
    nodes += quad('closest-corner', 'closest', 1, 0, corner, 0.12)
    nodes += quad('repeat', 'linear', 2, 0, (0.5, 0.5), 2.0)
    nodes = starters.common(96, 64, 0.9, starters.translate(0, 0, -3))+starters.world([0.2, 0.2, 0.2], 1.0)+nodes
    data = starters.scene('images/'+name, 'A tiled image (192x128 byte4 in tiles of 64 with three mip levels, read through the '
                          'tile callback) on six diffuse quads whose UV spans select different levels from the ray footprint: the '
                          'whole image (the smallest level), 0.464 of it (levels 1 and 2 mixed stochastically), 0.232 around a tile '
                          'corner (levels 0 and 1), 0.12 around the corner with Cubic and with Closest interpolation (level 0, '
                          'filtered across the tile borders), and UVs from -0.5 to 1.5 (Periodic extension)', 96, 64, 16, nodes, [entry])
    data['sourceFixture'] = 'native/oracle/scenes/author_images.py (tiled images)'
    data['render']['threads'] = TILED_THREADS
    if limit is not None:
        data['description'] = ('The scene tiled-mip-planes under a texture limit of %d: Cycles skips the mip levels of a tiled image '
                               'that are larger than the limit instead of reducing pixels, so every quad reads the smallest level' % limit)
        data['render']['textureLimit'] = limit
    return data, files


def grid(cells):
    """A square of side 2 in the xy plane at z 0, cells x cells quads: positions and triangle indices."""
    positions = [c for j in range(cells+1) for i in range(cells+1) for c in (-1+2*i/cells, -1+2*j/cells, 0.0)]
    indices = []
    for j in range(cells):
        for i in range(cells):
            a = j*(cells+1)+i
            indices += [a, a+cells+2, a+1, a, a+cells+1, a+cells+2]
    return positions, indices


def tiled_displacement():
    name = 'tiled-displacement'
    entry, files = tiled_image(name, 'image', 192, 128, detail_byte4(192, 128))
    relief = dict(handle='relief', create='shader', graph=dict(nodes=[
        dict(handle='d', type='diffuse_bsdf', set=dict(color=[0.7, 0.7, 0.7], roughness=0.0)), dict(handle='tc', type='texture_coordinate'),
        dict(handle='t', type='image_texture', set=dict(image='image', colorspace='scene_linear', alpha_type='unassociated',
                                                         interpolation='linear', extension='periodic')),
        dict(handle='disp', type='displacement', set=dict(space='object', scale=0.35, midlevel=0.5))],
        connect=[['tc', 'Generated', 't', 'Vector'], ['t', 'Color', 'disp', 'Height'], ['d', 'BSDF', 'output', 'Surface'],
                 ['disp', 'Displacement', 'output', 'Displacement']]), set=dict(emission_sampling_method='none', displacement_method='true'))
    positions, indices = grid(24)
    nodes = [relief]+starters.mesh('relief-mesh', positions, indices, 'relief')
    nodes += starters.light('lamp', 'pointlight', dict(radius=0.05, is_sphere=True), starters.translate(1.6, 1.2, -2.2), [60, 60, 60])
    return tiled_document(name, 'A grid displaced (true displacement, object space) by the height of a tiled image (192x128 byte4, tiles of '
                          '64) on its generated coordinates, under a point light: the image is read by the displacement evaluation '
                          'during scene preparation, without a path and without the stochastic mip level', nodes, [entry], files, 0.2)


def quad(handle, shader, corners, uvs):
    """A quad of four corners (x, y, z), counter-clockwise from the first, with one standard UV pair per corner."""
    uv = dict(name='uv', value='point2', domain='corner', flags=['standard'],
              data=[c for k in (0, 2, 1, 0, 3, 2) for c in uvs[k]])
    return starters.mesh(handle, [c for corner in corners for c in corner], [0, 2, 1, 0, 3, 2], shader, attributes=[uv])


def cutout_byte4(size):
    """A size x size byte4 image of 16 x 16 blocks: every other block transparent (alpha 0), the others opaque in a colour
    from their position, with a single-pixel checker 20 levels lighter and darker."""
    pixels = bytearray()
    for y in range(size):
        for x in range(size):
            bx, by = x//16, y//16
            shift = 20 if (x+y) % 2 == 0 else -20
            base = ((70+31*bx) % 180+40+shift, (200-23*by) % 180+40+shift, (90+17*bx+29*by) % 180+40+shift)
            pixels += bytes(base)+(b'\x00' if (bx+by) % 2 == 0 else b'\xff')
    return bytes(pixels)


def tiled_document(name, description, nodes, entries, files, world_strength, size=(64, 48), samples=16, camera=None, **integrator):
    head = starters.common(size[0], size[1], 0.9, camera or starters.translate(0, 0, -3))
    head[0]['set'].update(integrator)
    data = starters.scene('images/'+name, description, size[0], size[1], samples, head+starters.world([0.2, 0.2, 0.2], world_strength)+nodes, entries)
    data['sourceFixture'] = 'native/oracle/scenes/author_images.py (tiled images)'
    data['render']['threads'] = TILED_THREADS
    return data, files


WALL = ((-1.6, -1.1, 0), (1.6, -1.1, 0), (1.6, 1.1, 0), (-1.6, 1.1, 0))
UNIT_UV = ((0, 0), (1, 0), (1, 1), (0, 1))


def tiled_emission_nee():
    name = 'tiled-emission-nee'
    entry, files = tiled_image(name, 'image', 192, 128, detail_byte4(192, 128))
    nodes = [starters.material('white', 'diffuse_bsdf', dict(color=[0.8, 0.8, 0.8], roughness=0.0)),
             image_shader('glow', 'image', 'linear', emission=1.5)]
    nodes[1]['set']['emission_sampling_method'] = 'front_back'
    nodes += quad('wall', 'white', WALL, UNIT_UV)
    nodes += quad('panel', 'glow', ((1.9, -0.6, -0.8), (2.9, -0.6, -1.2), (2.9, 0.6, -1.2), (1.9, 0.6, -0.8)), ((0.2, 0.2), (0.6, 0.2), (0.6, 0.8), (0.2, 0.8)))
    return tiled_document(name, 'An emissive quad whose emission colour is a tiled image (192x128 byte4, tiles of 64, three mip levels), '
                          'sampled as a light (both faces) beside the view, in front of a diffuse wall: the camera never sees it, so the '
                          'image is read only by the shader evaluations of the light samples that light the wall (the shadow path), '
                          'and its tiles are first asked for there', nodes, [entry], files, 0.05)


def tiled_alpha_shadow():
    name = 'tiled-alpha-shadow'
    entry, files = tiled_image(name, 'image', 128, 128, cutout_byte4(128))
    cutout = dict(handle='cutout', create='shader', graph=dict(nodes=[
        dict(handle='d', type='diffuse_bsdf', set=dict(roughness=0.0)), dict(handle='tr', type='transparent_bsdf', set=dict(color=[1, 1, 1])),
        dict(handle='mix', type='mix_closure'),
        dict(handle='t', type='image_texture', set=dict(image='image', colorspace='scene_linear', alpha_type='unassociated',
                                                         interpolation='linear', extension='periodic')),
        dict(handle='uv', type='uvmap')],
        connect=[['uv', 'UV', 't', 'Vector'], ['t', 'Color', 'd', 'Color'], ['t', 'Alpha', 'mix', 'Fac'], ['tr', 'BSDF', 'mix', 'Closure1'],
                 ['d', 'BSDF', 'mix', 'Closure2'], ['mix', 'Closure', 'output', 'Surface']]), set=dict(emission_sampling_method='none'))
    nodes = [starters.material('white', 'diffuse_bsdf', dict(color=[0.8, 0.8, 0.8], roughness=0.0)), cutout]
    nodes += quad('wall', 'white', WALL, UNIT_UV)
    nodes += quad('screen', 'cutout', ((1.9, -0.6, -0.8), (2.9, -0.6, -0.8), (2.9, 0.6, -0.8), (1.9, 0.6, -0.8)), ((0.1, 0.1), (0.9, 0.1), (0.9, 0.9), (0.1, 0.9)))
    nodes += starters.light('lamp', 'pointlight', dict(radius=0.05, is_sphere=True), starters.translate(4.0, 0.3, -1.6), [60, 60, 60])
    return tiled_document(name, 'A quad whose transparency is the alpha of a tiled image (128x128 byte4 with transparent blocks, tiles of '
                          '64, two mip levels: a Mix of a Transparent and a Diffuse BSDF) beside the view, between a point light and a '
                          'diffuse wall: the camera never sees the quad, so the image is read only by the transparent shadow evaluations '
                          '(the shadow path), and its tiles are first asked for there',
                          nodes, [entry], files, 0.1)


def tiled_lamp_shader():
    name = 'tiled-lamp-shader'
    entry, files = tiled_image(name, 'image', 192, 128, detail_byte4(192, 128))
    lamp_shader = dict(handle='lamp-shader', create='shader', graph=dict(nodes=[
        dict(handle='tc', type='texture_coordinate'),
        dict(handle='t', type='image_texture', set=dict(image='image', colorspace='scene_linear', alpha_type='unassociated',
                                                         interpolation='linear', extension='periodic')),
        dict(handle='e', type='emission', set=dict(strength=1.0))],
        connect=[['tc', 'UV', 't', 'Vector'], ['t', 'Color', 'e', 'Color'], ['e', 'Emission', 'output', 'Surface']]))
    nodes = [starters.material('white', 'diffuse_bsdf', dict(color=[0.8, 0.8, 0.8], roughness=0.0)), lamp_shader]
    nodes += quad('floor', 'white', ((-2.2, -1.0, 1.6), (2.2, -1.0, 1.6), (2.2, -1.0, -2.0), (-2.2, -1.0, -2.0)), UNIT_UV)
    nodes += [dict(handle='lamp', create='arealight', set=dict(strength=[12, 12, 12], use_mis=True, cast_shadow=True, max_bounces=1024,
                                                             is_enabled=True, normalize=True, used_shaders=['lamp-shader'], sizeu=2.0, sizev=1.4,
                                                             ellipse=False, spread=3.14159265)),
              dict(handle='lamp-object', create='object', set=dict(geometry='lamp', tfm=[1, 0, 0, 0.0, 0, 0, 1, 1.0, 0, -1, 0, 0.2]))]
    return tiled_document(name, 'A rectangular area light whose emission colour is a tiled image (192x128 byte4, tiles of 64, three mip '
                          'levels) on its UV (the lamp\'s parametric coordinates), above a diffuse floor: the lamp\'s shader is evaluated '
                          'for light samples (the shadow path) and where a path hits the lamp (the main path)', nodes, [entry], files, 0.05)


def tiled_box_projection():
    name = 'tiled-box-projection'
    entry, files = tiled_image(name, 'image', 192, 128, detail_byte4(192, 128))
    boxed = dict(handle='boxed', create='shader', graph=dict(nodes=[
        dict(handle='d', type='diffuse_bsdf', set=dict(roughness=0.0)), dict(handle='tc', type='texture_coordinate'),
        dict(handle='t', type='image_texture', set=dict(image='image', colorspace='__builtin_srgb', alpha_type='unassociated',
                                                         interpolation='linear', extension='periodic', projection='box', projection_blend=0.25))],
        connect=[['tc', 'Object', 't', 'Vector'], ['t', 'Color', 'd', 'Color'], ['d', 'BSDF', 'output', 'Surface']]),
        set=dict(emission_sampling_method='none'))
    positions, indices = starters.sphere(1.0, 24, 12)
    nodes = [boxed]+starters.mesh('ball', positions, indices, 'boxed', tfm=[0.8, 0, 0.6, 0, 0, 1, 0, 0, -0.6, 0, 0.8, 0])
    nodes[1]['set']['smooth'] = [True]*(len(indices)//3)
    return tiled_document(name, 'A smooth sphere textured by a tiled sRGB image (192x128 byte4, tiles of 64, three mip levels) through the '
                          'box projection with a blend of 0.25 on object coordinates: up to three lookups per shading point, each with '
                          'its own derivatives', nodes, [entry], files, 1.0)


def tiled_ground_extensions():
    name = 'tiled-ground-extensions'
    entry, files = tiled_image(name, 'image', 200, 136, detail_byte4(200, 136))
    nodes = [image_shader('repeat', 'image', 'linear', 'periodic'), image_shader('extend', 'image', 'linear', 'clamp'),
             image_shader('clip', 'image', 'cubic', 'black'), image_shader('mirror', 'image', 'closest', 'mirror')]
    nodes += quad('ground', 'repeat', ((-3.0, -1.0, -2.0), (3.0, -1.0, -2.0), (3.0, -1.0, 14.0), (-3.0, -1.0, 14.0)), ((0, 0), (3, 0), (3, 8), (0, 8)))
    outside = ((-0.5, -0.5), (1.5, -0.5), (1.5, 1.5), (-0.5, 1.5))
    for k, shader in enumerate(('extend', 'clip', 'mirror')):
        x = -1.1+1.1*k
        nodes += quad('card-'+shader, shader, ((x-0.45, 0.1, 0.0), (x+0.45, 0.1, 0.0), (x+0.45, 1.0, 0.0), (x-0.45, 1.0, 0.0)), outside)
    return tiled_document(name, 'A tiled image whose sides are not multiples of its tile (200x136 byte4, tiles of 64, three mip levels) '
                          'on a ground plane seen in perspective with Periodic extension (the level follows the distance) and on three '
                          'cards whose UVs run from -0.5 to 1.5: Extend with Linear, Clip with Cubic and Mirror with Closest interpolation',
                          nodes, [entry], files, 1.0)


def tiled_environment(name, projection, description, importance):
    """A world whose Environment Texture node reads a tiled image (256x128 byte4, tiles of 64, three mip levels) through the
    given projection, behind a diffuse and a glossy sphere it lights; with importance, also sampled as a background light."""
    entry, files = tiled_image(name, 'environment', 256, 128, detail_byte4(256, 128))
    world = dict(handle='world', create='shader', graph=dict(nodes=[
        dict(handle='environment', type='environment_texture', set=dict(image='environment', colorspace='scene_linear', interpolation='linear',
                                                                        projection=projection, alpha_type='ignore')),
        dict(handle='background', type='background_shader', set=dict(strength=1.0))],
        connect=[['environment', 'Color', 'background', 'Color'], ['background', 'Background', 'output', 'Surface']]),
        set=dict(emission_sampling_method='auto' if importance else 'none'))
    nodes = [world, dict(handle='bg', scene='background', set=dict(transparent=False, shader='world')),
             starters.material('matte', 'diffuse_bsdf', dict(color=[0.8, 0.8, 0.8], roughness=0.0)),
             starters.material('shiny', 'glossy_bsdf', dict(color=[0.9, 0.9, 0.9], roughness=0.15))]
    if importance:
        nodes += [dict(handle='world-light', create='backgroundlight', set=dict(use_mis=True, map_resolution=64, strength=[1.0, 1.0, 1.0],
                                                                                used_shaders=['world'])),
                  dict(handle='world-light-object', create='object', set=dict(geometry='world-light'))]
    positions, indices = starters.sphere(0.7, 24, 12)
    nodes += starters.mesh('matte-ball', positions, indices, 'matte', starters.translate(-0.85, 0.0, -0.2))
    nodes += starters.mesh('shiny-ball', positions, indices, 'shiny', starters.translate(0.85, 0.0, -0.2))
    for node in nodes:
        if node.get('create') == 'mesh':
            node['set']['smooth'] = [True]*(len(indices)//3)
    # The camera looks along -y with +z up (the world's horizon and, above it, towards the pole of the equirectangular map).
    head = starters.common(96, 64, 1.1, [1, 0, 0, 0, 0, 0, -1, 3.2, 0, 1, 0, 0.6])
    data = starters.scene('images/'+name, description, 96, 64, 16, head+nodes, [entry])
    data['sourceFixture'] = 'native/oracle/scenes/author_images.py (tiled images)'
    data['render']['threads'] = TILED_THREADS
    return data, files


def tiled_sphere_tube_projection():
    name = 'tiled-sphere-tube-projection'
    entry, files = tiled_image(name, 'image', 192, 128, detail_byte4(192, 128))

    def projected(handle, projection):
        return dict(handle=handle, create='shader', graph=dict(nodes=[
            dict(handle='d', type='diffuse_bsdf', set=dict(roughness=0.0)), dict(handle='tc', type='texture_coordinate'),
            dict(handle='t', type='image_texture', set=dict(image='image', colorspace='__builtin_srgb', alpha_type='unassociated',
                                                             interpolation='linear', extension='periodic', projection=projection))],
            connect=[['tc', 'Generated', 't', 'Vector'], ['t', 'Color', 'd', 'Color'], ['d', 'BSDF', 'output', 'Surface']]),
            set=dict(emission_sampling_method='none'))
    nodes = [projected('sphere-mapped', 'sphere'), projected('tube-mapped', 'tube')]
    positions, indices = starters.sphere(0.75, 24, 12)
    nodes += starters.mesh('ball', positions, indices, 'sphere-mapped', [0.8, 0, 0.6, -0.9, 0, 1, 0, 0, -0.6, 0, 0.8, 0])
    nodes += starters.mesh('can', positions, indices, 'tube-mapped', [0.7, 0, 0, 0.9, 0, 1.3, 0, 0, 0, 0, 0.7, 0])
    for node in nodes:
        if node.get('create') == 'mesh':
            node['set']['smooth'] = [True]*(len(indices)//3)
    nodes += starters.light('lamp', 'pointlight', dict(radius=0.1, is_sphere=True), starters.translate(1.5, 1.8, -2.5), [80, 80, 80])
    return tiled_document(name, 'Two smooth spheres textured by a tiled sRGB image (192x128 byte4, tiles of 64, three mip levels) on their '
                          'generated coordinates, one through the Sphere and one, stretched, through the Tube projection of the Image '
                          'Texture node: Cycles maps those coordinates and their derivatives with dual numbers', nodes, [entry], files, 0.3)


def tiled_volume(name, ray_marching):
    """A box of heterogeneous medium whose scattering density is a tiled image (192x128 byte4, tiles of 64, three mip levels)
    on its generated coordinates, lit by a point light through a quad whose transparency is the alpha of a second tiled
    image: the volume's own evaluations (the main path's, the shadow paths', the density estimate's) and the transparent
    shadow evaluations in a scene with volumes read tiled images."""
    density, density_files = tiled_image(name, 'density', 192, 128, detail_byte4(192, 128))
    cutout, cutout_files = tiled_image(name, 'cutout', 128, 128, cutout_byte4(128))
    medium = dict(handle='medium', create='shader', graph=dict(nodes=[
        dict(handle='tc', type='texture_coordinate'),
        dict(handle='t', type='image_texture', set=dict(image='density', colorspace='scene_linear', alpha_type='unassociated',
                                                         interpolation='linear', extension='periodic')),
        dict(handle='m', type='math', set=dict(math_type='multiply', value2=1.5)),
        dict(handle='a', type='absorption_volume', set=dict(density=0.05, color=[0.8, 0.65, 0.4])),
        dict(handle='s', type='scatter_volume', set=dict(color=[0.75, 0.8, 0.9], anisotropy=0.3)), dict(handle='add', type='add_closure')],
        connect=[['tc', 'Generated', 't', 'Vector'], ['t', 'Alpha', 'm', 'Value1'], ['t', 'Color', 'a', 'Color'], ['m', 'Value', 's', 'Density'],
                 ['a', 'Volume', 'add', 'Closure1'], ['s', 'Volume', 'add', 'Closure2'], ['add', 'Closure', 'output', 'Volume']]),
        set=dict(emission_sampling_method='none', volume_sampling_method='multiple_importance'))
    screen = dict(handle='screen-shader', create='shader', graph=dict(nodes=[
        dict(handle='d', type='diffuse_bsdf', set=dict(roughness=0.0)), dict(handle='tr', type='transparent_bsdf', set=dict(color=[1, 1, 1])),
        dict(handle='mix', type='mix_closure'),
        dict(handle='t', type='image_texture', set=dict(image='cutout', colorspace='scene_linear', alpha_type='unassociated',
                                                         interpolation='linear', extension='periodic')),
        dict(handle='uv', type='uvmap')],
        connect=[['uv', 'UV', 't', 'Vector'], ['t', 'Color', 'd', 'Color'], ['t', 'Alpha', 'mix', 'Fac'], ['tr', 'BSDF', 'mix', 'Closure1'],
                 ['d', 'BSDF', 'mix', 'Closure2'], ['mix', 'Closure', 'output', 'Surface']]), set=dict(emission_sampling_method='none'))
    nodes = [medium, screen, starters.material('white', 'diffuse_bsdf', dict(color=[0.8, 0.8, 0.8], roughness=0.0))]
    nodes += quad('floor', 'white', ((-3.0, -1.2, 3.0), (3.0, -1.2, 3.0), (3.0, -1.2, -2.0), (-3.0, -1.2, -2.0)), UNIT_UV)
    nodes += quad('wall', 'white', ((-3.0, -1.2, 2.5), (3.0, -1.2, 2.5), (3.0, 2.5, 2.5), (-3.0, 2.5, 2.5)), UNIT_UV)
    nodes += quad('screen', 'screen-shader', ((1.4, 1.2, -0.6), (2.6, 1.2, -0.6), (2.6, 1.2, 0.6), (1.4, 1.2, 0.6)), ((0.1, 0.1), (0.9, 0.1), (0.9, 0.9), (0.1, 0.9)))
    box = [-1.3, -1.1, -0.9, 0.9, -1.1, -0.9, -1.3, 0.9, -0.9, 0.9, 0.9, -0.9, -1.3, -1.1, 1.3, 0.9, -1.1, 1.3, -1.3, 0.9, 1.3, 0.9, 0.9, 1.3]
    nodes += starters.mesh('box', box, [0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7], 'medium')
    nodes += starters.light('lamp', 'pointlight', dict(radius=0.1, is_sphere=True), starters.translate(2.0, 3.0, 0.0), [90, 85, 80])
    description = ('A box of heterogeneous medium (absorption and scattering) whose scattering density is the alpha and whose absorption colour '
                   'is the colour of a tiled image (192x128 byte4, tiles of 64, three mip levels) on its generated coordinates, '
                   'lit by a point light through a quad whose transparency is the alpha of a second tiled image (128x128 byte4): '
                   'the volume evaluations of the main path, of the shadow paths and of the density estimate read a tiled image, '
                   'and so do the transparent shadow evaluations of a scene with volumes')
    integrator = dict(max_volume_bounce=4)
    if ray_marching:
        integrator['volume_ray_marching'] = True
        description += '; ray marching'
    return tiled_document(name, description, nodes, [density, cutout], density_files+cutout_files, 0.15, size=(64, 48),
                          camera=[1, 0, 0, 0, 0, 1, 0, 0.3, 0, 0, 1, -5.5], **integrator)


def scenes():
    return {'tiled-mip-planes.json': tiled_mip_planes(), 'tiled-emission-nee.json': tiled_emission_nee(),
            'tiled-alpha-shadow.json': tiled_alpha_shadow(), 'tiled-lamp-shader.json': tiled_lamp_shader(),
            'tiled-box-projection.json': tiled_box_projection(), 'tiled-ground-extensions.json': tiled_ground_extensions(),
            'tiled-texture-limit.json': tiled_mip_planes('tiled-texture-limit', 64), 'tiled-displacement.json': tiled_displacement(), 'udim-three-tiles.json': three_tiles(), 'udim-checker-linear.json': checker_linear(),
            'udim-missing-tile.json': missing_tile(), 'texture-limit-blocks.json': texture_limit_blocks(),
            'texture-limit-blocks-halved.json': texture_limit_blocks_halved(), 'texture-limit-checker.json': texture_limit_checker(),
            'tiled-environment.json': tiled_environment('tiled-environment', 'equirectangular', 'A world whose Environment Texture node reads a '
                                                        'tiled image (256x128 byte4, tiles of 64, three mip levels) through the equirectangular '
                                                        'projection, seen behind a diffuse and a glossy sphere and sampled as a background light '
                                                        '(an importance map of 64): Cycles maps the direction and its derivatives with dual numbers', True),
            'tiled-environment-mirror-ball.json': tiled_environment('tiled-environment-mirror-ball', 'mirror_ball', 'A world whose Environment '
                                                                    'Texture node reads a tiled image (256x128 byte4, tiles of 64, three mip levels) '
                                                                    'through the mirror ball projection, seen behind a diffuse and a glossy sphere '
                                                                    'that it lights', False),
            'tiled-sphere-tube-projection.json': tiled_sphere_tube_projection(),
            'tiled-volume.json': tiled_volume('tiled-volume', False), 'tiled-volume-ray-marching.json': tiled_volume('tiled-volume-ray-marching', True)}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--check', action='store_true', help='Fail when a committed scene or pixel file differs')
    args = parser.parse_args()
    HERE.mkdir(exist_ok=True)
    for name, (data, files) in scenes().items():
        text = json.dumps(data, indent=1)+'\n'
        outputs = [(HERE/name, text.encode())]+[(HERE/file, content) for file, content in files]
        for path, content in outputs:
            if args.check:
                if not path.is_file() or path.read_bytes() != content:
                    raise SystemExit('Scene file differs: '+str(path))
            else:
                path.write_bytes(content)
        print(name, 'ok')


if __name__ == '__main__':
    main()
