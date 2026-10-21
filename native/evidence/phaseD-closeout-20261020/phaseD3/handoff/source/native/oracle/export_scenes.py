#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Export the maintained Linux gate scenes as cbcOracleScene v1 files.

Each scene re-expresses one gate fixture (native/cycles/*_fixture.h,
lighting_gate.cpp, render_gate.cpp) through the public C ABI vocabulary:
registered node types, socket names and enum labels only. The gates render
these fixtures through their own scheduler; the exported scenes render through
cbc_host_render (a Cycles Session), so their films are compared backend against
backend (CPU reference, WebGPU, Metal), never against the gates' films.
Integrator controls a gate leaves at the pinned Cycles defaults stay at those
defaults (identical for every backend of one build); Session-only controls the
gates never reach (denoising) are disabled explicitly.

Rerunning reproduces every file byte for byte, including scenes/linux-gates/
manifest.json (SHA-256 of each scene and pixel file).
"""
import hashlib
import json
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent
OUT = HERE/'scenes'/'linux-gates'
IDENTITY = [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0]


def r(v):
    return round(float(v), 7)


def mul(a, b):
    """3x4 row-major affine product a*b (b applied first), as Cycles."""
    out = []
    for i in range(3):
        row = a[4*i:4*i+4]
        for j in range(4):
            value = sum(row[k]*b[4*k+j] for k in range(3))
            out.append(value + (row[3] if j == 3 else 0))
    return out


def chain(*ts):
    result = IDENTITY
    for t in ts:
        result = mul(result, t)
    return result


def translate(x, y, z):
    return [1, 0, 0, x, 0, 1, 0, y, 0, 0, 1, z]


def scale(x, y, z):
    return [x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0]


def rotate(angle, axis):
    s, c = math.sin(angle), math.cos(angle)
    t = 1 - c
    n = math.sqrt(sum(a*a for a in axis))
    x, y, z = (a/n for a in axis)
    return [x*x*t+c, x*y*t-s*z, x*z*t+s*y, 0,
            y*x*t+s*z, y*y*t+c, y*z*t-s*x, 0,
            z*x*t-s*y, z*y*t+s*x, z*z*t+c, 0]


def tfm(t):
    return [r(v) for v in t]


def normalize(v):
    n = math.sqrt(sum(a*a for a in v))
    return [a/n for a in v]


def shader(handle, nodes, connect, **settings):
    entry = dict(handle=handle, create='shader', graph=dict(nodes=nodes, connect=connect))
    if settings:
        entry['set'] = settings
    return entry


def node(handle, kind, **values):
    entry = dict(handle=handle, type=kind)
    if values:
        entry['set'] = values
    return entry


def mesh(handle, positions, indices, shaders, extra=None, attributes=None, motion=None):
    data = dict(positions=[r(v) for v in positions], indices=list(indices))
    if attributes:
        data['attributes'] = attributes
    if motion:
        data['motion'] = motion
    values = dict(used_shaders=list(shaders))
    values.update(extra or {})
    return dict(handle=handle, create='mesh', mesh=data, set=values)


def obj(handle, geometry, transform=None, motion=None):
    values = dict(geometry=geometry)
    if transform is not None:
        values['tfm'] = tfm(transform)
    if motion is not None:
        values['motion'] = [tfm(m) for m in motion]
    return dict(handle=handle, create='object', set=values)


def scene(identifier, description, source, width, height, samples, nodes, images=None):
    return {'cbcOracleScene': 1, 'id': identifier, 'description': description, 'sourceFixture': source,
            'render': dict(width=width, height=height, samples=samples, repeats=2), 'colorConfig': None,
            'nodes': nodes, 'images': images or []}


def world(color, strength=None):
    values = dict(color=list(color))
    if strength is not None:
        values['strength'] = strength
    return [shader('world', [node('b', 'background_shader', **values)], [['b', 'Background', 'output', 'Surface']]),
            dict(handle='bg', scene='background', set=dict(shader='world'))]


# ---- render_gate.cpp fixture(): enclosure, camera and the prepare() graphs ----

def surface_graph(test, glass):
    """surface_fixture.h prepare() graph `test`; glass_fixture.h when glass."""
    nodes = [node('diffuse', 'diffuse_bsdf', color=[.2, .6, .9], roughness=0 if test == 0 else .43),
             node('trans', 'translucent_bsdf', color=[.6, .2, .1]),
             node('transparent', 'transparent_bsdf', color=[.7, .8, .9]),
             node('emission', 'emission', color=[.1, .4, .7], strength=2.3),
             node('holdout', 'holdout')]
    links, result = [], ('diffuse', 'BSDF')
    if test == 2: result = ('trans', 'BSDF')
    if test == 3: result = ('transparent', 'BSDF')
    if test == 4: result = ('emission', 'Emission')
    if test == 5: result = ('holdout', 'Holdout')
    if 6 <= test <= 9:
        nodes.append(node('mix', 'mix_closure', fac=.37))
        links += [['diffuse', 'BSDF', 'mix', 'Closure1'], ['transparent', 'BSDF', 'mix', 'Closure2']]
        result = ('mix', 'Closure')
        if test >= 7:
            nodes += [node('geo', 'geometry'), node('checker', 'checker_texture', scale=3.7)]
            links += [['geo', 'Position', 'checker', 'Vector'], ['checker', 'Fac', 'mix', 'Fac'], ['checker', 'Color', 'diffuse', 'Color']]
        if test >= 8:
            nodes.append(node('add', 'add_closure'))
            links += [[*result, 'add', 'Closure1'], ['emission' if test == 8 else 'holdout', 'Emission' if test == 8 else 'Holdout', 'add', 'Closure2']]
            result = ('add', 'Closure')
    if 10 <= test < 16:
        names = ['Object', 'Normal', 'Reflection', 'Camera', 'Window', 'Object']
        tc = dict()
        if test == 15:
            tc = dict(use_transform=True, ob_tfm=tfm(chain(translate(.3, .2, .1), scale(.4, .5, .6))))
        nodes += [node('tc', 'texture_coordinate', **tc), node('add', 'add_closure')]
        links += [['tc', names[test-10], 'emission', 'Color'], ['emission', 'Emission', 'add', 'Closure1'], ['diffuse', 'BSDF', 'add', 'Closure2']]
        result = ('add', 'Closure')
    if test >= 16:
        nodes[0]['set']['roughness'] = [1e-5, .0001, .001, .02][test-16]
    if glass:
        rough = [0, .003, .004, .02, .15, .35, .65, 1, -.2, 1.2]
        ior = [1, 1.00001, 1.33, 1.5, .7, 2.4, 1.5, .8, 1.1, 1.8]
        k = test % 10
        if test < 10:
            nodes.append(node('glass', 'refraction_bsdf', distribution='ggx', roughness=rough[k], IOR=ior[k], color=[.2, .6, .9]))
        else:
            nodes.append(node('glass', 'glass_bsdf', distribution='ggx', roughness=rough[k], IOR=ior[k],
                              color=[1, 1, 1] if test % 5 == 0 else [.2, .6, .9]))
        result = ('glass', 'BSDF')
        if k >= 5:
            nodes += [node('gd', 'diffuse_bsdf', roughness=.43, color=[.7, .4, .2]), node('gmix', 'mix_closure', fac=.37)]
            links += [[*result, 'gmix', 'Closure1'], ['gd', 'BSDF', 'gmix', 'Closure2']]
            result = ('gmix', 'Closure')
        if k == 7:
            nodes += [node('gt', 'transparent_bsdf'), node('gadd', 'add_closure')]
            links += [[*result, 'gadd', 'Closure1'], ['gt', 'BSDF', 'gadd', 'Closure2']]
            result = ('gadd', 'Closure')
        if k == 9:
            nodes += [node('ge', 'emission', color=[.1, .2, .3]), node('gadd', 'add_closure')]
            links += [[*result, 'gadd', 'Closure1'], ['ge', 'Emission', 'gadd', 'Closure2']]
            result = ('gadd', 'Closure')
    links.append([*result, 'output', 'Surface'])
    return shader(f'graph-{test}', nodes, links)


def prepared(glass):
    """prepare(): 20 graphs on a 20-triangle mesh, shared by two objects plus a copy."""
    nodes = [surface_graph(t, glass) for t in range(20)]
    positions, indices = [], []
    for t in range(20):
        x, y = (t % 4)*2.3, (t//4)*2.1
        positions += [x, y, .2, x+1.3, y, .4, x, y+1.1, -.1]
        indices += [3*t, 3*t+1, 3*t+2]
    normals = []
    for i in range(60):
        normals += [r(v) for v in normalize([-.25+.03*(i % 7), .3+.02*(i % 5), 1])]
    extra = dict(shader=list(range(20)), smooth=[bool(t % 2) for t in range(20)])
    attributes = [dict(name='N', value='normal', domain='vertex_normal', flags=['standard'], data=normals)]
    graphs = [f'graph-{t}' for t in range(20)]
    nodes += [mesh('shared', positions, indices, graphs, extra, attributes), mesh('copy', positions, indices, graphs, extra, attributes)]
    for i in range(3):
        transform = chain(translate(-4, -4, i*2), rotate(.17*i, [0, 1, 0]), scale(-1.2 if i == 1 else 1.1, .8, 1.4))
        nodes.append(obj(f'prepared-{i}', 'shared' if i < 2 else 'copy', transform))
    return nodes


def enclosure(wall_nodes, wall_links, attributes=None):
    v = [[-6, -4, -4], [6, -4, -4], [6, -4, 8], [-6, -4, 8], [-6, -4, 8], [6, -4, 8], [6, 5, 8], [-6, 5, 8],
         [-6, -4, -4], [-6, -4, 8], [-6, 5, 8], [-6, 5, -4], [6, -4, 8], [6, -4, -4], [6, 5, -4], [6, 5, 8]]
    indices = [i*4+j for i in range(4) for j in (0, 1, 2, 0, 2, 3)]
    return [shader('wall', wall_nodes, wall_links), mesh('enclosure', [c for p in v for c in p], indices, ['wall'], attributes=attributes),
            obj('enclosure-object', 'enclosure')]


def no_emission_sampling(nodes):
    """render_gate.cpp: every scene shader uses EMISSION_SAMPLING_NONE."""
    for entry in nodes:
        if entry.get('create') == 'shader':
            entry.setdefault('set', {})['emission_sampling_method'] = 'none'
    return nodes


def render_common(fov=1.1, camera=None):
    # The gate also clears direct lighting in kernel data; with no lamps and no
    # emission sampling the native scene derives the same state. filter_glossy 0
    # is Cycles' "off" (the kernel's FLT_MAX).
    integrator = dict(use_guiding=False, use_light_tree=False, use_adaptive_sampling=False, use_denoise=False,
                      min_bounce=3, max_bounce=4, max_diffuse_bounce=4, max_transmission_bounce=4, filter_glossy=0.0)
    return [dict(handle='integrator', scene='integrator', set=integrator),
            dict(handle='film', scene='film', set=dict(use_sample_count=False)),
            dict(handle='camera', scene='camera', set=dict(fov=fov, matrix=tfm(camera or translate(0, 0, -10))))]


def fixture_image():
    """image_fixture.h FixtureImageLoader(729, BYTE4, 13, 9) as raw byte4 rows."""
    data = bytearray()
    for i in range(13*9*4):
        v = ((i*31+729*7) % 251)/250
        if i % 4 == 3:
            v = ((i//4) % 5)/4
        data.append(int(v*255))
    return bytes(data)


def glass_render():
    wall = [node('d', 'diffuse_bsdf', color=[.6, .65, .7], roughness=.3)]
    nodes = no_emission_sampling(render_common()+prepared(True)+enclosure(wall, [['d', 'BSDF', 'output', 'Surface']])+world([.25, .4, .6], .8))
    return scene('linux-gates/glass-render', 'Twenty GGX refraction/glass graphs (glass_fixture.h) on the prepare() mesh inside the open enclosure',
                 'native/cycles/render_gate.cpp fixture() with CBC_GLASS_FIXTURE (codebrix_cycles_glass_render_gate)', 64, 48, 8, nodes)


def textured_plane():
    wall = [node('d', 'diffuse_bsdf', color=[.6, .65, .7], roughness=.3), node('uv', 'uvmap'),
            node('t', 'image_texture', image='fixture-729', colorspace='scene_linear', alpha_type='ignore',
                 interpolation='cubic', extension='periodic')]
    links = [['uv', 'UV', 't', 'Vector'], ['t', 'Color', 'd', 'Color'], ['d', 'BSDF', 'output', 'Surface']]
    corners = [[0, 0], [1, 0], [1, 1], [0, 0], [1, 1], [0, 1]]
    uv = []
    for i in range(24):
        uv += [r(corners[i % 6][0]+.17*(i//6)), corners[i % 6][1]]
    attributes = [dict(name='uv', value='point2', domain='corner', flags=['standard'], data=uv)]
    nodes = no_emission_sampling(render_common()+prepared(False)+enclosure(wall, links, attributes)+world([.25, .4, .6], .8))
    images = [dict(handle='fixture-729', width=13, height=9, format='byte4', file='textured-plane.fixture-729.byte4',
                   provenance='image_fixture.h FixtureImageLoader(729, IMAGE_DATA_TYPE_BYTE4, 13, 9) pixel formula')]
    return scene('linux-gates/textured-plane', 'Cubic, repeating 13x9 byte4 texture over UV-mapped enclosure walls plus the prepare() graphs',
                 'native/cycles/render_gate.cpp fixture() image mode (render gate --images, FixtureImageLoader 729)', 64, 48, 8, nodes, images)


def volume(closed):
    wall = [node('d', 'diffuse_bsdf', color=[.6, .65, .7], roughness=.3)]
    nodes = render_common()+prepared(False)+enclosure(wall, [['d', 'BSDF', 'output', 'Surface']])
    nodes[0]['set']['max_volume_bounce'] = 4
    bg = [node('b', 'background_shader', color=[.25, .4, .6], strength=.8)]
    links = [['b', 'Background', 'output', 'Surface']]
    if not closed:
        bg += [node('a', 'absorption_volume', color=[.8, .65, .4], density=.04),
               node('s', 'scatter_volume', color=[.8, .7, .6], density=.035, anisotropy=.45), node('add', 'add_closure')]
        links += [['a', 'Volume', 'add', 'Closure1'], ['s', 'Volume', 'add', 'Closure2'], ['add', 'Closure', 'output', 'Volume']]
    nodes += [shader('world', bg, links), dict(handle='bg', scene='background', set=dict(shader='world'))]
    if closed:
        low = [[-2.4, -2.2, -2], [-1.3, -1.4, -.5], [-.7, -.7, .2], [2.8, -.5, .2]]
        high = [[1.6, 2.3, 4], [2.6, 1.8, 5], [.6, .8, 2.5], [3.5, .5, 2]]
        tris = [0, 2, 1, 1, 2, 3, 4, 5, 6, 5, 7, 6, 0, 4, 2, 2, 4, 6, 1, 3, 5, 3, 7, 5, 0, 1, 4, 1, 5, 4, 2, 6, 3, 3, 6, 7]
        for i in range(4):
            vol = [node('a', 'absorption_volume', density=r(.045+.013*i), color=[.8, .65, .4]),
                   node('s', 'scatter_volume', density=r(.035+.009*i), color=[r(.8-.1*i), r(.4+.1*i), .6],
                        anisotropy=.35 if i < 2 else (-.4 if i == 2 else 0)), node('add', 'add_closure')]
            vlinks = [['a', 'Volume', 'add', 'Closure1'], ['s', 'Volume', 'add', 'Closure2'], ['add', 'Closure', 'output', 'Volume']]
            if i == 2:
                vol.append(node('t', 'transparent_bsdf', color=[.8, .9, .95])); vlinks.append(['t', 'BSDF', 'output', 'Surface'])
            if i == 3:
                vol.append(node('g', 'glass_bsdf', distribution='ggx', roughness=.18, IOR=1.33, color=[.8, .9, .95])); vlinks.append(['g', 'BSDF', 'output', 'Surface'])
            nodes.append(shader(f'volume-{i}', vol, vlinks, emission_sampling_method='none'))
            pts = []
            for k in range(8):
                pts += [high[i][0] if k & 1 else low[i][0], high[i][1] if k & 2 else low[i][1], high[i][2] if k & 4 else low[i][2]]
            nodes += [mesh(f'box-{i}', pts, tris, [f'volume-{i}']), obj(f'box-{i}-object', f'box-{i}')]
    no_emission_sampling(nodes)
    name = 'closed-volumes' if closed else 'world-volume'
    source = ('native/cycles/render_gate.cpp fixture() with CBC_CLOSED_VOLUME_FIXTURE and volume_boundary_fixture.h (4 boxes)' if closed
              else 'native/cycles/render_gate.cpp fixture() with CBC_VOLUME_FIXTURE (world absorption + scatter, g=0.45)')
    return scene('linux-gates/'+name, 'Finite homogeneous volume boxes' if closed else 'Homogeneous world volume over the prepare() scene',
                 source, 64, 48, 8, nodes)


# ---- lighting_gate.cpp fixture(): two quads, a center quad, lights ----

def quad(handle, shader_handle, vertices, transform=None, motion=None, object_motion=None):
    return [mesh(handle, [c for v in vertices for c in v], [0, 1, 2, 0, 2, 3], [shader_handle], motion=motion),
            obj(handle+'-object', handle, transform, object_motion)]


def object_motion(center, mode):
    count = 2 if mode % 3 == 0 else 3 if mode % 3 == 1 else 5
    frames = []
    for i in range(count):
        u = 2*i/(count-1)-1
        local = chain(rotate((.0001 if mode == 0 else .23)*u, normalize([.3, 1, .2])), scale(1+.08*u, 1-.05*u, 1+.03*u))
        if mode >= 3:
            local[1] += .03*u
        frames.append(chain(translate(.34*u, .11*u, .07*u), center, local))
    return frames


def deforming_motion(vertices, mode):
    steps = 5 if mode % 2 else 3
    positions = []
    for step in range(steps):
        u = 2*step/(steps-1)-1
        for p in vertices:
            if step == (steps-1)//2:
                positions += [r(c) for c in p]
            else:
                x, y, z = p
                positions += [r(x+.08*u*y), r(y+.04*u*x), r(z+.13*u*x*y+.035*u*u*x)]
    return dict(steps=steps, positions=positions)


def light_entry(handle, kind, values, transform, emitter='emitter'):
    values = dict(values, used_shaders=[emitter])
    return [dict(handle=handle, create=kind, set=values), obj(handle+'-object', handle, transform)]


def lighting(gate, mode):
    use_mis = gate != 'lighting'
    integrator = dict(use_guiding=False, use_light_tree=False, use_adaptive_sampling=False, use_denoise=False,
                      min_bounce=2, max_bounce=3, filter_glossy=0.0)
    if gate in ('object-motion', 'deforming-motion'):
        integrator['motion_blur'] = True
    if gate == 'transparent-lighting':
        integrator['transparent_max_bounce'] = 3 if mode == 7 else 16
    camera = dict(fov=.9, matrix=tfm(translate(0, 0, -7)))
    if gate in ('object-motion', 'deforming-motion'):
        camera['shuttertime'] = .6
    nodes = [dict(handle='integrator', scene='integrator', set=integrator),
             dict(handle='film', scene='film', set=dict(use_sample_count=False)),
             dict(handle='camera', scene='camera', set=camera)]
    none = dict(emission_sampling_method='none')
    nodes.append(shader('diffuse', [node('d', 'diffuse_bsdf', color=[.55, .32, .18], roughness=.31)], [['d', 'BSDF', 'output', 'Surface']], **none))
    nodes.append(shader('mixed', [node('d', 'diffuse_bsdf', color=[.55, .32, .18], roughness=.31),
                                  node('g', 'glossy_bsdf', distribution='ggx', color=[.2, .5, .8], roughness=.35), node('m', 'mix_closure', fac=.4)],
                        [['d', 'BSDF', 'm', 'Closure1'], ['g', 'BSDF', 'm', 'Closure2'], ['m', 'Closure', 'output', 'Surface']], **none))
    nodes += quad('back', 'diffuse', [[-4, -3, 3], [4, -3, 3], [4, 3, 3], [-4, 3, 3]])
    nodes += quad('floor', 'mixed', [[-4, -2, -2], [4, -2, -2], [4, -2, 3], [-4, -2, 3]])
    center = 'mixed'
    center_vertices = [[-.8, -1.2, 0], [.8, -1.2, 0], [.8, 1.1, 0], [-.8, 1.1, 0]]
    if gate in ('object-motion', 'deforming-motion'):
        nodes.append(shader('moving', [node('tc', 'texture_coordinate'), node('abs', 'vector_math', math_type='absolute'),
                                       node('d', 'diffuse_bsdf', roughness=.27),
                                       node('n', 'normal_map', space='object', color=[.55, .42, .97], strength=.35)],
                            [['tc', 'Object', 'abs', 'Vector1'], ['abs', 'Vector', 'd', 'Color'], ['n', 'Normal', 'd', 'Normal'],
                             ['d', 'BSDF', 'output', 'Surface']], **none))
        center = 'moving'
        center_tfm = scale(-1.1 if mode % 2 else 1, .9, 1.2)
        deform = deforming_motion(center_vertices, mode) if gate == 'deforming-motion' else None
        moving = gate == 'object-motion' or mode % 2
        nodes += quad('center', center, center_vertices, center_tfm, deform, object_motion(center_tfm, mode) if moving else None)
        if mode >= 4:
            inst = chain(translate(-1.6, .3, .7), rotate(.15, [0, 1, 0]), scale(.6, .7, .9))
            nodes.append(obj('instance', 'center', inst, object_motion(inst, mode+1) if moving else None))
    else:
        nodes += quad('center', center, center_vertices)
    if gate == 'transparent-lighting':
        variant = mode if mode < 4 else 4 if mode == 6 else 1
        color = [1, 1, 1] if variant == 0 else [0, 0, 0] if variant == 4 else [.82, .57, .31]
        tnodes, tlinks, result = [node('t', 'transparent_bsdf', color=color)], [], ('t', 'BSDF')
        if variant == 2:
            tnodes += [node('d', 'diffuse_bsdf', color=[.2, .4, .6]), node('m', 'mix_closure', fac=.65)]
            tlinks += [['d', 'BSDF', 'm', 'Closure1'], ['t', 'BSDF', 'm', 'Closure2']]; result = ('m', 'Closure')
        if variant == 3:
            tnodes += [node('geo', 'geometry'), node('c', 'checker_texture', scale=2.3, color1=[.8, .3, .6], color2=[.2, .9, .4])]
            tlinks += [['geo', 'Position', 'c', 'Vector'], ['c', 'Color', 't', 'Color']]
        tlinks.append([*result, 'output', 'Surface'])
        nodes.append(shader('pane', tnodes, tlinks, **none))
        for layer in range(4 if mode == 4 else 7 if mode == 5 else 1):
            y = .55+.13*layer
            nodes += quad(f'pane-{layer}', 'pane', [[-2.9, y, -1.8], [2.9, y, -1.8], [2.9, y, 2.8], [-2.9, y, 2.8]])
    nodes += [shader('world', [node('b', 'background_shader', color=[0, 0, 0])], [['b', 'Background', 'output', 'Surface']], **none),
              dict(handle='bg', scene='background', set=dict(shader='world'))]
    emitter_strength = 3 if gate == 'triangle-lighting' else 1
    emitter_sampling = 'none'
    if gate == 'triangle-lighting' and mode:
        emitter_sampling = 'front' if mode in (2, 4) else 'front_back'
    nodes.append(shader('emitter', [node('e', 'emission', color=[1, 1, 1], strength=emitter_strength)], [['e', 'Emission', 'output', 'Surface']],
                        emission_sampling_method=emitter_sampling))
    if gate == 'triangle-lighting':
        panel = [[-.8, -.5, 0], [.8, -.5, 0], [.8, .5, 0], [-.8, .5, 0]]
        panel_tfm = chain(translate(1.65, 1.55, -1.4), rotate(.18, [0, 1, 0]), rotate(.5, [1, 0, 0]), scale(-1.2 if mode in (2, 4) else 1, 1.1, 1))
        nodes += quad('panel', 'emitter', panel, panel_tfm, deforming_motion(panel, 1) if mode == 5 else None,
                      object_motion(panel_tfm, 1) if mode == 5 else None)
        if mode == 5:
            nodes[0]['set']['motion_blur'] = True; nodes[2]['set']['shuttertime'] = .6
        if mode in (3, 5):
            nodes += light_entry('point', 'pointlight', dict(radius=.45, is_sphere=True, use_mis=True, strength=[25, 35, 45]), translate(-2, 2, -1))
    elif gate == 'broad-lighting':
        for lamp in range(3 if mode == 11 else 1):
            if mode < 4 or (mode == 11 and lamp == 1):
                kind, values = 'spotlight', dict(radius=0 if mode == 0 else 1.8 if mode == 3 else .45, is_sphere=mode != 1,
                                                 angle=.25 if mode == 3 else 1.2, smooth=0 if mode == 0 else .35)
            elif mode == 11 and lamp == 2:
                kind, values = 'sunlight', dict(angle=.22, normalize=False)
            else:
                kind, values = 'arealight', dict(sizeu=1.4 if mode in (6, 9) else 2, sizev=1.4 if mode in (6, 9) else .9,
                                                 ellipse=mode in (5, 6, 8, 9), spread=0 if mode == 10 else .7 if mode >= 7 else math.pi,
                                                 normalize=mode != 4)
            sun = kind == 'sunlight'
            values.update(strength=[.3, .5, .7] if sun else [140, 100, 70], use_mis=True)
            transform = chain(translate(-2.7 if lamp == 1 else 2.2, 2, -2), rotate(2.85 if sun else 2.8, [0, 1, 0]), rotate(-.4, [1, 0, 0]))
            nodes += light_entry(f'light-{lamp}', kind, values, transform)
    else:
        for lamp in range(3 if mode >= 4 else 1):
            if gate == 'transparent-lighting' and mode == 2 and lamp == 0:
                kind, values = 'arealight', dict(sizeu=1.8, sizev=.7, spread=1.3)
            elif gate == 'transparent-lighting' and mode == 3 and lamp == 0:
                kind, values = 'spotlight', dict(radius=.45, is_sphere=True, angle=1.2, smooth=.35)
            elif mode == 3 or lamp == 2:
                kind, values = 'sunlight', dict(angle=.22, normalize=False)
            else:
                kind, values = 'pointlight', dict(radius=0 if mode == 0 else .45, is_sphere=mode != 1)
            sun = kind == 'sunlight'
            values.update(strength=[.3, .5, .7] if sun else [140, 100, 70], use_mis=use_mis)
            if gate != 'transparent-lighting' and mode == 6:
                values['cast_shadow'] = False
            if mode == 7:
                values['max_bounces'] = 0
            angle = 2.85 if sun else 2.8 if kind in ('arealight', 'spotlight') else .3
            transform = chain(translate(-2.7 if lamp == 1 else 2.2, 2, -2), rotate(angle, [0, 1, 0]), rotate(-.4, [1, 0, 0]))
            nodes += light_entry(f'light-{lamp}', kind, values, transform)
        if mode == 5:
            nodes[0]['set']['light_sampling_threshold'] = .3
    return nodes


GATES = {
    'triangle-lighting': ([0, 1, 2, 3, 5], 'codebrix_cycles_triangle_lighting_gate (CODEBRIX_TRIANGLE_LIGHT_GATE, MIS)'),
    'broad-lighting': (list(range(12)), 'codebrix_cycles_broad_lighting_gate (CODEBRIX_BROAD_LIGHT_GATE, MIS)'),
    'transparent-lighting': (list(range(8)), 'codebrix_cycles_transparent_lighting_gate (CODEBRIX_TRANSPARENT_LIGHT_GATE, MIS)'),
    'object-motion': (list(range(8)), 'codebrix_cycles_object_motion_render_gate (CODEBRIX_OBJECT_MOTION_GATE, MIS)'),
    'deforming-motion': (list(range(8)), 'codebrix_cycles_deforming_motion_render_gate (CODEBRIX_DEFORMING_MOTION_GATE, MIS)'),
}


def lighting_scenes():
    for gate, (modes, target) in GATES.items():
        for mode in modes:
            yield scene(f'linux-gates/{gate}-{mode}', f'{gate} gate fixture, mode {mode}',
                        f'native/cycles/lighting_gate.cpp fixture(scene, {mode}) in {target}', 32, 24, 8, lighting(gate, mode))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    files = {}
    (OUT/'textured-plane.fixture-729.byte4').write_bytes(fixture_image())
    for data in [glass_render(), textured_plane(), volume(False), volume(True), *lighting_scenes()]:
        path = HERE/'scenes'/(data['id']+'.json')
        path.write_text(json.dumps(data, indent=1)+'\n')
    for path in sorted(OUT.iterdir()):
        if path.name != 'manifest.json':
            files[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
    manifest = dict(schema='cbcOracleSceneManifest', version=1, family='linux-gates', generator='native/oracle/export_scenes.py',
                    note='Gate fixtures re-expressed through the public C ABI; compare backends against each other, not against gate films.',
                    files=files)
    (OUT/'manifest.json').write_text(json.dumps(manifest, indent=1, sort_keys=True)+'\n')
    print(len(files), 'files in', OUT.relative_to(HERE))


if __name__ == '__main__':
    main()
