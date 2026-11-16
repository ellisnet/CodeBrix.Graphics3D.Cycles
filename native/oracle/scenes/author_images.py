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
files. Through the public C API.
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


def scenes():
    return {'udim-three-tiles.json': three_tiles(), 'udim-checker-linear.json': checker_linear(),
            'udim-missing-tile.json': missing_tile(), 'texture-limit-blocks.json': texture_limit_blocks(),
            'texture-limit-blocks-halved.json': texture_limit_blocks_halved(), 'texture-limit-checker.json': texture_limit_checker()}


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
