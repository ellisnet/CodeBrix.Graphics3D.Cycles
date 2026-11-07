#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Texture baking on the CPU backends through the C ABI, against known results.

Every expected value here is derived by hand or computed independently of the
library: the bake map by exact rational arithmetic from the UVs, surface values
from the scene's own numbers (a position from the triangle's corners, a colour
from the shader's sockets, sun light from its strength and angle, a closed
emissive box from the geometric series of its bounces). A result compared only
against itself proves nothing, so the bit-identical checks are limited to the
scene-edit cases, whose reference is a new host that never baked.

Usage: bake_gate.py --output DIR [--library PATH] [--case SUBSTRING]
Exit code 0 only when every case passes; status.json holds the verdict.
"""
import argparse
import ctypes as C
import json
import math
import os
import struct
import sys
import time
import traceback
from fractions import Fraction
from pathlib import Path
from build import WORK

OK, INVALID, UNSUPPORTED, FAILED, CANCELLED, NOT_READY, SMALL_BUFFER = range(7)
# The WebGPU case creates a host; a gate never touches the user's pipeline cache (native/DEVICE.md "Pipeline cache").
os.environ['CODEBRIX_CYCLES_PIPELINE_CACHE'] = 'off'

CPU_REFERENCE, CPU_EMBREE, WEBGPU = 2, 5, 1
BAKE = {name: i + 1 for i, name in enumerate(
    ('combined emission background ao diffuse diffuse_direct diffuse_indirect diffuse_color glossy glossy_direct glossy_indirect glossy_color '
     'transmission transmission_direct transmission_indirect transmission_color position normal uv roughness object_id material_id depth mist aov_color aov_value').split())}
COMPONENTS = dict(combined=4, roughness=1, object_id=1, material_id=1, depth=1, mist=1, aov_color=4, aov_value=1)
USE_CAMERA, INCLUDE_ALBEDO = 1, 2
VALUE_POINT2, VALUE_NORMAL = 10, 9
ATTR_VERTEX, ATTR_CORNER, ATTR_VERTEX_NORMAL = 3, 4, 7
ATTR_STANDARD = 1
IMAGE_FLOAT4 = 2


class HostConfig(C.Structure):
    _fields_ = [('size', C.c_uint32), ('abi', C.c_uint32), ('backend', C.c_uint32), ('threads', C.c_uint32), ('reserved', C.c_uint32 * 4)]


class RenderConfig(C.Structure):
    _fields_ = [('size', C.c_uint32), ('width', C.c_uint32), ('height', C.c_uint32), ('samples', C.c_uint32), ('reserved', C.c_uint32 * 4)]


class AttributeConfig(C.Structure):
    _fields_ = [('size', C.c_uint32), ('value_type', C.c_uint32), ('domain', C.c_uint32), ('flags', C.c_uint32), ('reserved', C.c_uint32 * 4)]


class ImageConfig(C.Structure):
    _fields_ = [('size', C.c_uint32), ('width', C.c_uint32), ('height', C.c_uint32), ('format', C.c_uint32), ('reserved', C.c_uint32 * 4)]


class BakeConfig(C.Structure):
    _fields_ = [('size', C.c_uint32), ('width', C.c_uint32), ('height', C.c_uint32), ('samples', C.c_uint32), ('type', C.c_uint32), ('flags', C.c_uint32),
                ('reserved', C.c_uint32 * 2), ('object', C.c_uint64), ('uv_attribute', C.c_char_p), ('aov_name', C.c_char_p), ('reserved2', C.c_uint64)]


EVENT = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p)
L = None


def last_error():
    buffer = C.create_string_buffer(4096); required = C.c_uint64()
    L.cbc_host_error(buffer, 4096, C.byref(required))
    return buffer.value.decode()


def check(result):
    if result != OK: raise RuntimeError(f'native call returned {result}: {last_error()}')


def floats(values): return (C.c_float * len(values))(*values)


class Scene:
    """One host. Worlds are black and integrators deterministic unless a case says otherwise."""

    def __init__(self, backend=CPU_REFERENCE):
        config = HostConfig(C.sizeof(HostConfig), 1, backend, 4)
        self.h = C.c_uint64()
        check(L.cbc_host_create(C.byref(config), C.byref(self.h)))
        self.h = self.h.value
        self.integrator, self.camera, self.film, self.background = (self.singleton(n) for n in ('integrator', 'camera', 'film', 'background'))
        for name in ('use_light_tree', 'use_guiding', 'use_adaptive_sampling', 'use_denoise', 'caustics_reflective', 'caustics_refractive'):
            self.ints(self.integrator, name, 0)
        self.string(self.integrator, 'sampling_pattern', 'tabulated_sobol')
        self.world(0, 0, 0)

    def close(self): check(L.cbc_host_destroy(self.h))
    def __enter__(self): return self
    def __exit__(self, *a): self.close()

    def singleton(self, name):
        n = C.c_uint64(); check(L.cbc_host_scene_node(self.h, name.encode(), C.byref(n))); return n.value

    def create(self, kind):
        n = C.c_uint64(); check(L.cbc_host_create_node(self.h, kind.encode(), C.byref(n))); return n.value

    def node(self, shader, kind):
        n = C.c_uint64(); check(L.cbc_host_shader_node(self.h, shader, kind.encode(), C.byref(n))); return n.value

    def output(self, shader):
        n = C.c_uint64(); check(L.cbc_host_shader_output(self.h, shader, C.byref(n))); return n.value

    def link(self, a, out, b, into): check(L.cbc_host_connect(self.h, a, out.encode(), b, into.encode()))
    def ints(self, n, name, *v): check(L.cbc_host_set_ints(self.h, n, name.encode(), (C.c_int64 * len(v))(*v), len(v)))
    def set(self, n, name, *v): check(L.cbc_host_set_floats(self.h, n, name.encode(), floats(v), len(v)))
    def string(self, n, name, v): check(L.cbc_host_set_string(self.h, n, name.encode(), v.encode()))
    def nodes(self, n, name, *v): check(L.cbc_host_set_nodes(self.h, n, name.encode(), (C.c_uint64 * len(v))(*v), len(v)))

    def world(self, r, g, b):
        shader = self.create('shader'); bg = self.node(shader, 'background_shader')
        self.set(bg, 'color', r, g, b); self.set(bg, 'strength', 1.0)
        self.link(bg, 'Background', self.output(shader), 'Surface')
        self.string(shader, 'emission_sampling_method', 'none')
        self.nodes(self.background, 'shader', shader)
        return shader

    def surface(self, kind, closure_out, **sockets):
        shader = self.create('shader'); n = self.node(shader, kind)
        for name, value in sockets.items():
            if isinstance(value, str): self.string(n, name, value)
            elif isinstance(value, tuple): self.set(n, name, *value)
            else: self.set(n, name, value)
        self.link(n, closure_out, self.output(shader), 'Surface')
        self.string(shader, 'emission_sampling_method', 'none')
        return shader

    def emission(self, color, strength=1.0): return self.surface('emission', 'Emission', color=color, strength=strength)
    def diffuse(self, color): return self.surface('diffuse_bsdf', 'BSDF', color=color)

    def mesh(self, shader, xyz, triangles, uv=None, uv_name='uv', standard=True, domain=ATTR_CORNER):
        m = self.create('mesh'); self.nodes(m, 'used_shaders', shader)
        check(L.cbc_host_mesh(self.h, m, floats(xyz), len(xyz) // 3, (C.c_uint32 * len(triangles))(*triangles), len(triangles) // 3))
        if uv is not None: self.uv(m, uv, uv_name, standard, domain)
        return m

    def uv(self, m, uv, name='uv', standard=True, domain=ATTR_CORNER):
        config = AttributeConfig(C.sizeof(AttributeConfig), VALUE_POINT2, domain, ATTR_STANDARD if standard else 0)
        check(L.cbc_host_attribute_floats(self.h, m, name.encode(), C.byref(config), floats(uv), len(uv)))

    def object(self, m, tfm=None):
        o = self.create('object'); self.nodes(o, 'geometry', m)
        if tfm: self.set(o, 'tfm', *tfm)
        return o

    def config(self, obj, kind, width, height, samples=1, flags=0, uv=None, aov=None):
        return BakeConfig(C.sizeof(BakeConfig), width, height, samples, BAKE[kind], flags, (C.c_uint32 * 2)(), obj,
                          uv.encode() if uv else None, aov.encode() if aov else None, 0)

    def bake(self, obj, kind, width, height, samples=1, flags=0, uv=None, aov=None):
        config = self.config(obj, kind, width, height, samples, flags, uv, aov)
        required, components = C.c_uint64(), C.c_uint32()
        if L.cbc_host_bake(self.h, C.byref(config), None, 0, C.byref(required), C.byref(components)) != SMALL_BUFFER:
            raise RuntimeError('size query: ' + last_error())
        assert components.value == COMPONENTS.get(kind, 3) and required.value == width * height * components.value, (kind, components.value, required.value)
        values = (C.c_float * required.value)()
        check(L.cbc_host_bake(self.h, C.byref(config), values, required.value, C.byref(required), C.byref(components)))
        return list(values)

    def map(self, obj, width, height, uv=None):
        config = self.config(obj, 'combined', width, height, 1, 0, uv)
        required = C.c_uint64(); triangles = (C.c_int32 * (width * height))(); coordinates = (C.c_float * (width * height * 6))()
        check(L.cbc_host_bake_map(self.h, C.byref(config), triangles, coordinates, width * height, C.byref(required)))
        return list(triangles), list(coordinates)

    def render(self, width, height, samples):
        config = RenderConfig(C.sizeof(RenderConfig), width, height, samples)
        values = (C.c_float * (width * height * 4))()
        check(L.cbc_host_render(self.h, C.byref(config), values, width * height * 4))
        return list(values)

    def pass_floats(self, name):
        required, components = C.c_uint64(), C.c_uint32()
        result = L.cbc_host_pass_floats(self.h, name.encode(), None, 0, C.byref(required), C.byref(components))
        if result != SMALL_BUFFER: return result, None
        values = (C.c_float * required.value)()
        check(L.cbc_host_pass_floats(self.h, name.encode(), values, required.value, C.byref(required), C.byref(components)))
        return OK, list(values)


def close(a, e): return abs(a - e) <= 5e-5 + 2e-4 * abs(e)


class Failed(Exception):
    pass


def require(test, message):
    if not test: raise Failed(message)


def compare(actual, expected, label, components=None):
    """Practical criterion per component; NaN and Inf fail; strict differences counted."""
    require(len(actual) == len(expected), f'{label}: {len(actual)} values, expected {len(expected)}')
    worst, strict = 0.0, 0
    for i, (a, e) in enumerate(zip(actual, expected)):
        require(math.isfinite(a), f'{label}[{i}] is not finite')
        d = abs(a - e); worst = max(worst, d)
        strict += d > 1e-7 + 1e-7 * abs(e)
        require(close(a, e), f'{label}[{i}] = {a!r}, expected {e!r}')
    return dict(label=label, values=len(actual), worst=worst, strict=strict)


# ---- The test mesh and the independent map ----------------------------------------------------
# Four vertices, two triangles; UV islands that do not touch, so some pixels stay empty.
QUAD = (-2.0, -1.0, 0.5, 2.0, -1.0, 0.5, 2.0, 1.0, 0.5, -2.0, 1.0, 0.5)
QUAD_TRIANGLES = (0, 1, 2, 0, 2, 3)
def f32(x): return struct.unpack('f', struct.pack('f', x))[0]


# No pixel centre at 16 x 12 lies exactly on an edge (map_exact checks it): there the kernel jitters even its first sample.
QUAD_UV = tuple(f32(v) for v in (0.0613, 0.0471, 0.8123, 0.0471, 0.8123, 0.5571,   # triangle 0: lower right
                                 0.0313, 0.2531, 0.5317, 0.7537, 0.0313, 0.9411))  # triangle 1: upper left
W, H = 16, 12


def exact_map(uv, width, height):
    """Rasterizes the UV triangles with exact rationals: what the map must be, independent of the library."""
    triangles = [-1] * (width * height); coordinates = [(0.0,) * 6] * (width * height)
    for t in range(len(uv) // 6):
        p = [(Fraction(uv[t * 6 + c * 2]) * width, Fraction(uv[t * 6 + c * 2 + 1]) * height) for c in range(3)]
        (x0, y0), (x1, y1), (x2, y2) = p
        d = (x0 - x2) * (y1 - y2) - (x1 - x2) * (y0 - y2)
        if d == 0: continue
        for y in range(height):
            for x in range(width):
                i = y * width + x
                if triangles[i] >= 0: continue
                px, py = Fraction(2 * x + 1, 2), Fraction(2 * y + 1, 2)
                # Solve p = u t0 + v t1 + (1 - u - v) t2.
                u = ((px - x2) * (y1 - y2) - (x1 - x2) * (py - y2)) / d
                v = ((x0 - x2) * (py - y2) - (px - x2) * (y0 - y2)) / d
                if u < 0 or v < 0 or u + v > 1: continue
                triangles[i] = t
                coordinates[i] = (float(u), float(v), float((y1 - y2) / d), float(-(x1 - x2) / d), float(-(y0 - y2) / d), float((x0 - x2) / d))
    return triangles, coordinates


def transform_point(t, p): return tuple(t[r * 4] * p[0] + t[r * 4 + 1] * p[1] + t[r * 4 + 2] * p[2] + t[r * 4 + 3] for r in range(3))
def transform_direction(t, d): return tuple(t[r * 4] * d[0] + t[r * 4 + 1] * d[1] + t[r * 4 + 2] * d[2] for r in range(3))


def normalize(v):
    n = math.sqrt(sum(c * c for c in v)); return tuple(c / n for c in v)


def rotation_z_x(angle_z, angle_x, tx, ty, tz, scale=1.0):
    """A rigid (optionally uniformly scaled) 3x4 transform: rotate about X, then about Z, then translate."""
    cz, sz, cx, sx = math.cos(angle_z), math.sin(angle_z), math.cos(angle_x), math.sin(angle_x)
    return tuple(f32(v) for v in (scale * cz, -scale * sz * cx, scale * sz * sx, tx,
                                  scale * sz, scale * cz * cx, -scale * cz * sx, ty,
                                  0.0, scale * sx, scale * cx, tz))


def surface_points(positions, triangles, coordinates, width, height, map_triangles):
    """Per pixel the surface point at the texel centre, or None where no triangle covers it."""
    points = []
    for i in range(width * height):
        t = map_triangles[i]
        if t < 0: points.append(None); continue
        u, v = coordinates[i][0], coordinates[i][1]
        a, b, c = (positions[triangles[t * 3 + k] * 3:triangles[t * 3 + k] * 3 + 3] for k in range(3))
        points.append(tuple(u * a[k] + v * b[k] + (1 - u - v) * c[k] for k in range(3)))
    return points


def per_pixel(points, value, components):
    """Expected film: value(point) where covered, zero elsewhere."""
    out = []
    for p in points: out.extend(value(p) if p is not None else (0.0,) * components)
    return out


# ---- Cases -------------------------------------------------------------------------------------
CASES = []
def case(function): CASES.append(function); return function


def quad_scene(s, shader, tfm=None):
    m = s.mesh(shader, QUAD, QUAD_TRIANGLES, QUAD_UV)
    return m, s.object(m, tfm)


@case
def map_exact():
    """The rasterized map equals an exact rational rasterization: coverage, barycentrics, differentials."""
    with Scene() as s:
        _, o = quad_scene(s, s.diffuse((.5, .5, .5)))
        triangles, flat = s.map(o, W, H)
        expected_t, expected_c = exact_map(QUAD_UV, W, H)
        require(triangles == expected_t, f'coverage differs: {triangles} {expected_t}')
        require(0 < sum(t >= 0 for t in triangles) < W * H, 'map must have covered and empty pixels')
        require(all(c[0] > 0 and c[1] > 0 and c[0] + c[1] < 1 for c, t in zip(expected_c, expected_t) if t >= 0), 'the test map must have no pixel centre on an edge')
        got = [flat[i * 6:i * 6 + 6] for i in range(W * H)]
        worst = max(abs(a - b) for g, e in zip(got, expected_c) for a, b in zip(g, e))
        require(worst <= 1e-6, f'coordinates differ by {worst}')
        return dict(covered=sum(t >= 0 for t in triangles), of=W * H, worstCoordinate=worst)


@case
def map_edges_and_overlap():
    """Shared edges leave no crack; overlap goes to the lowest index; zero-area and off-image triangles cover nothing."""
    with Scene() as s:
        # A fan of triangles sharing edges at awkward, non-dyadic coordinates: every pixel inside must be covered exactly once.
        n = 7; xyz = [0.0, 0.0, 0.0]; uv = []; tris = []
        centre = (f32(0.5), f32(0.5))
        for i in range(n + 1):
            xyz += [math.cos(i), math.sin(i), 0.0]
        ring = [(f32(0.5 + 0.47 * math.cos(2 * math.pi * i / n)), f32(0.5 + 0.47 * math.sin(2 * math.pi * i / n))) for i in range(n)]
        for i in range(n):
            tris += [0, i + 1, i + 2]
            uv += [centre[0], centre[1], ring[i][0], ring[i][1], ring[(i + 1) % n][0], ring[(i + 1) % n][1]]
        m = s.mesh(s.diffuse((.5, .5, .5)), xyz, tris, uv)
        o = s.object(m)
        triangles, _ = s.map(o, 97, 89)
        expected, _ = exact_map(uv, 97, 89)
        # Exact coverage must match pixel for pixel in which pixels are covered; on an exact edge either neighbour may own it.
        require([t >= 0 for t in triangles] == [t >= 0 for t in expected], 'a crack or an overdraw at a shared edge')
        # Overlap, degenerate and off-image triangles.
        over = (0.0, 0.0, 1.0, 0.0, 0.0, 1.0,   0.0, 0.0, 1.0, 0.0, 0.0, 1.0,   .5, .5, .5, .5, .5, .5,   2.0, 2.0, 3.0, 2.0, 2.0, 3.0)
        xyz2 = [float(i) for i in range(12 * 3)]
        m2 = s.mesh(s.diffuse((.5, .5, .5)), xyz2, list(range(12)), over)
        o2 = s.object(m2)
        t2, _ = s.map(o2, 8, 8)
        require(set(t2) == {-1, 0}, f'overlap: {set(t2)}')
        return dict(covered=sum(t >= 0 for t in triangles))


def points_for(tfm=None, width=W, height=H, uv=QUAD_UV):
    triangles, coordinates = exact_map(uv, width, height)
    points = surface_points(QUAD, QUAD_TRIANGLES, coordinates, width, height, triangles)
    if tfm: points = [transform_point(tfm, p) if p else None for p in points]
    return points, triangles, coordinates


TFM = rotation_z_x(.6, .4, 3.0, -2.0, 1.0, 1.5)


@case
def position_transformed():
    """World position at the texel centre, object transform applied by Cycles' static BVH path; the sample count does not change it."""
    with Scene() as s:
        _, o = quad_scene(s, s.diffuse((.5, .5, .5)), TFM)
        points, _, _ = points_for(TFM)
        expected = per_pixel(points, lambda p: p, 3)
        return [compare(s.bake(o, 'position', W, H, samples), expected, f'{samples} samples') for samples in (1, 8)]


@case
def position_instanced_and_target_order():
    """A mesh with two users is not transform-applied; the kernel's own transform path gives each instance's position. A later object and a later mesh bake right."""
    with Scene() as s:
        shader = s.diffuse((.5, .5, .5))
        sun = s.create('sunlight'); s.nodes(sun, 'used_shaders', s.emission((1, 1, 1))); s.set(sun, 'strength', 0.0, 0.0, 0.0)
        s.object(sun)
        first = s.mesh(shader, (5., 5., 5., 6., 5., 5., 5., 6., 5.), (0, 1, 2), (0., 0., 1., 0., 0., 1.))
        s.object(first)
        m = s.mesh(shader, QUAD, QUAD_TRIANGLES, QUAD_UV)
        a = s.object(m, TFM)
        t2 = rotation_z_x(-1.1, .2, -3.0, 2.0, -1.0, .75)
        b = s.object(m, t2)
        rows = [compare(s.bake(a, 'position', W, H), per_pixel(points_for(TFM)[0], lambda p: p, 3), 'first instance'),
                compare(s.bake(b, 'position', W, H), per_pixel(points_for(t2)[0], lambda p: p, 3), 'second instance')]
        return rows


@case
def normal_flat_and_smooth():
    """Geometric normal under a rotation; interpolated vertex normals with smooth faces (at one sample, the texel centre)."""
    with Scene() as s:
        m, o = quad_scene(s, s.diffuse((.5, .5, .5)), TFM)
        n = normalize(transform_direction(TFM, (0, 0, 1)))
        points, triangles, coordinates = points_for(TFM)
        rows = [compare(s.bake(o, 'normal', W, H, 4), per_pixel(points, lambda p: n, 3), 'flat rotated')]
    with Scene() as s:
        m = s.mesh(s.diffuse((.5, .5, .5)), QUAD, QUAD_TRIANGLES, QUAD_UV)
        vertex_normals = [normalize(v) for v in ((-.5, 0, 1), (.5, 0, 1), (.5, .5, 1), (-.5, .5, 1))]
        flat = [f32(c) for v in vertex_normals for c in v]
        config = AttributeConfig(C.sizeof(AttributeConfig), VALUE_NORMAL, ATTR_VERTEX_NORMAL, ATTR_STANDARD)
        check(L.cbc_host_attribute_floats(s.h, m, b'N', C.byref(config), floats(flat), len(flat)))
        s.ints(m, 'smooth', 1, 1)
        o = s.object(m)
        expected = []
        for i in range(W * H):
            t = triangles[i]
            if t < 0: expected += [0.0, 0.0, 0.0]; continue
            u, v = coordinates[i][0], coordinates[i][1]
            a, b, c = (vertex_normals[QUAD_TRIANGLES[t * 3 + k]] for k in range(3))
            expected += normalize(tuple(u * a[k] + v * b[k] + (1 - u - v) * c[k] for k in range(3)))
        rows.append(compare(s.bake(o, 'normal', W, H, 1), expected, 'smooth'))
        return rows


@case
def uv_is_the_pixel_centre():
    """The UV pass at the texel centre is ((x + .5) / W, (y + .5) / H): orientation and row order of the output."""
    with Scene() as s:
        _, o = quad_scene(s, s.diffuse((.5, .5, .5)))
        triangles, _ = exact_map(QUAD_UV, W, H)
        expected = []
        for y in range(H):
            for x in range(W):
                expected += [(x + .5) / W, (y + .5) / H, 1.0] if triangles[y * W + x] >= 0 else [0.0, 0.0, 0.0]
        rows = [compare(s.bake(o, 'uv', W, H, 1), expected, 'one sample')]
        # With many samples the jitter stays inside the texel: the average is within half a texel of the centre.
        many = s.bake(o, 'uv', W, H, 64)
        for i in range(W * H):
            if triangles[i] < 0: continue
            require(abs(many[i * 3] - expected[i * 3]) <= .5 / W and abs(many[i * 3 + 1] - expected[i * 3 + 1]) <= .5 / H, f'jitter left the texel at pixel {i}')
        return rows


@case
def custom_uv_attribute_and_vertex_domain():
    """A named UV attribute, on the vertex domain, is rasterized like the standard one."""
    with Scene() as s:
        m = s.mesh(s.diffuse((.5, .5, .5)), QUAD, QUAD_TRIANGLES)
        vertex_uv = tuple(f32(v) for v in (.1213, .1117, .8731, .1173, .8797, .8693, .1171, .8813))
        s.uv(m, vertex_uv, 'second', False, ATTR_VERTEX)
        o = s.object(m)
        corners = [vertex_uv[QUAD_TRIANGLES[c] * 2 + k] for c in range(6) for k in range(2)]
        expected_t, expected_c = exact_map(corners, W, H)
        triangles, _ = s.map(o, W, H, uv='second')
        require([t >= 0 for t in triangles] == [t >= 0 for t in expected_t], 'vertex-domain coverage')
        points = surface_points(QUAD, QUAD_TRIANGLES, expected_c, W, H, expected_t)
        return compare(s.bake(o, 'position', W, H, uv='second'), per_pixel(points, lambda p: p, 3), 'named vertex-domain UV')


@case
def constant_emission_and_empty_pixels():
    """Emission and Combined read the shader's colour where covered; empty pixels are zero and Combined alpha is 0 there, 1 where covered."""
    with Scene() as s:
        _, o = quad_scene(s, s.emission((1.5, .25, .75), 2.0))
        points = points_for()[0]
        return [compare(s.bake(o, 'emission', W, H, 16), per_pixel(points, lambda p: (3.0, .5, 1.5), 3), 'emission'),
                compare(s.bake(o, 'combined', W, H, 16), per_pixel(points, lambda p: (3.0, .5, 1.5, 1.0), 4), 'combined')]


def image_scene(s, width, height, values, interpolation='closest'):
    shader = s.create('shader'); texture = s.node(shader, 'image_texture'); emission = s.node(shader, 'emission')
    s.string(texture, 'interpolation', interpolation)
    s.set(emission, 'strength', 1.0)
    config = ImageConfig(C.sizeof(ImageConfig), width, height, IMAGE_FLOAT4)
    image = C.c_uint64(); data = floats(values)
    check(L.cbc_host_image_create(s.h, C.byref(config), data, len(values) * 4, C.byref(image)))
    check(L.cbc_host_image_bind(s.h, texture, image.value))
    s.link(texture, 'Color', emission, 'Color'); s.link(emission, 'Emission', s.output(shader), 'Surface')
    s.string(shader, 'emission_sampling_method', 'none')
    return shader


@case
def texture_round_trip():
    """An image on a full [0,1] quad, closest filter, baked at its own size returns itself at any sample count: row order, the UV convention and the per-pixel differentials together. Any error in the differentials would let a jittered sample read a neighbouring texel."""
    width, height = 8, 6
    pixels = [c for y in range(height) for x in range(width) for c in ((x + 1) / 8, (y + 1) / 8, (x * y + 1) / 64, 1.0)]
    full = (0., 0., 1., 0., 1., 1.,  0., 0., 1., 1., 0., 1.)
    with Scene() as s:
        m = s.mesh(image_scene(s, width, height, pixels), QUAD, QUAD_TRIANGLES, full)
        o = s.object(m)
        rgb = [c for i in range(width * height) for c in pixels[i * 4:i * 4 + 3]]
        rows = [compare(s.bake(o, 'emission', width, height, samples), rgb, f'{samples} samples') for samples in (1, 16, 64)]
        rows.append(compare(s.bake(o, 'combined', width, height, 16), pixels, 'combined'))
        # At twice the size each texel covers exactly four pixels; jitter is half as wide.
        twice = [c for y in range(height * 2) for x in range(width * 2) for c in pixels[(y // 2 * width + x // 2) * 4:(y // 2 * width + x // 2) * 4 + 3]]
        rows.append(compare(s.bake(o, 'emission', width * 2, height * 2, 16), twice, 'twice the size'))
        return rows


A, LIGHT = (.5, .25, .125), (2.0, 1.0, .5)


def lit_plane(s, shader, tfm=None):
    s.world(*LIGHT)
    return quad_scene(s, shader, tfm)[1]


@case
def diffuse_under_constant_world():
    """An open plane under a constant world, world not sampled as a light: every cosine-sampled ray returns L, so each pass is exact and noise-free."""
    with Scene() as s:
        o = lit_plane(s, s.diffuse(A), TFM)
        points = points_for(TFM)[0]
        product = tuple(a * l for a, l in zip(A, LIGHT))
        return [compare(s.bake(o, 'combined', W, H, 8), per_pixel(points, lambda p: product + (1.0,), 4), 'combined'),
                compare(s.bake(o, 'diffuse_color', W, H, 8), per_pixel(points, lambda p: A, 3), 'colour'),
                compare(s.bake(o, 'diffuse_direct', W, H, 8), per_pixel(points, lambda p: LIGHT, 3), 'direct'),
                compare(s.bake(o, 'diffuse_direct', W, H, 8, INCLUDE_ALBEDO), per_pixel(points, lambda p: product, 3), 'direct with albedo'),
                compare(s.bake(o, 'diffuse_indirect', W, H, 8), per_pixel(points, lambda p: (0.0, 0.0, 0.0), 3), 'indirect'),
                compare(s.bake(o, 'diffuse', W, H, 8), per_pixel(points, lambda p: LIGHT, 3), 'diffuse'),
                compare(s.bake(o, 'diffuse', W, H, 8, INCLUDE_ALBEDO), per_pixel(points, lambda p: product, 3), 'diffuse with albedo')]


@case
def glossy_and_transmission_under_constant_world():
    """A sharp mirror reflects L times its colour; a sharp refraction through one surface transmits L times its colour; the colour passes read the closure weight."""
    with Scene() as s:
        C1 = (.75, .5, .25)
        o = lit_plane(s, s.surface('glossy_bsdf', 'BSDF', color=C1, roughness=0.0))
        points = points_for()[0]
        product = tuple(a * l for a, l in zip(C1, LIGHT))
        rows = [compare(s.bake(o, 'combined', W, H, 8), per_pixel(points, lambda p: product + (1.0,), 4), 'mirror combined'),
                compare(s.bake(o, 'glossy_color', W, H, 8), per_pixel(points, lambda p: C1, 3), 'mirror colour'),
                compare(s.bake(o, 'glossy_direct', W, H, 8), per_pixel(points, lambda p: LIGHT, 3), 'mirror direct'),
                compare(s.bake(o, 'glossy', W, H, 8), per_pixel(points, lambda p: LIGHT, 3), 'mirror glossy'),
                compare(s.bake(o, 'glossy_indirect', W, H, 8), per_pixel(points, lambda p: (0.0, 0.0, 0.0), 3), 'mirror indirect')]
    with Scene() as s:
        C2 = (.25, .5, .75)
        o = lit_plane(s, s.surface('refraction_bsdf', 'BSDF', color=C2, roughness=0.0, IOR=1.5))
        points = points_for()[0]
        product = tuple(a * l for a, l in zip(C2, LIGHT))
        rows += [compare(s.bake(o, 'combined', W, H, 8), per_pixel(points, lambda p: product + (1.0,), 4), 'refraction combined'),
                 compare(s.bake(o, 'transmission_color', W, H, 8), per_pixel(points, lambda p: C2, 3), 'refraction colour'),
                 compare(s.bake(o, 'transmission_direct', W, H, 8), per_pixel(points, lambda p: LIGHT, 3), 'refraction direct'),
                 compare(s.bake(o, 'transmission', W, H, 8), per_pixel(points, lambda p: LIGHT, 3), 'refraction transmission'),
                 compare(s.bake(o, 'transmission_indirect', W, H, 8), per_pixel(points, lambda p: (0.0, 0.0, 0.0), 3), 'refraction indirect')]
    return rows


@case
def roughness_pass():
    """surface_shader_average_roughness returns the closure's roughness at one closure."""
    with Scene() as s:
        _, o = quad_scene(s, s.surface('glossy_bsdf', 'BSDF', color=(1, 1, 1), roughness=.25))
        return compare(s.bake(o, 'roughness', W, H, 4), per_pixel(points_for()[0], lambda p: (.25,), 1), 'roughness')


@case
def sun_direct_light_and_shadow():
    """A delta sun: radiance = albedo * strength * cos(angle) / pi, no noise. A blocker above part of the plane shadows exactly the pixels whose ray towards the sun it stops."""
    strength = 3.0
    # The sun's light travels along the holder's -Z; the holder is rotated about X by this angle.
    angle = .5
    c, sn = math.cos(angle), math.sin(angle)
    sun_tfm = (1., 0., 0., 0.,  0., f32(c), f32(-sn), 0.,  0., f32(sn), f32(c), 0.)
    to_sun = (0.0, -sn, c)
    with Scene() as s:
        emitter = s.emission((1, 1, 1))
        sun = s.create('sunlight'); s.nodes(sun, 'used_shaders', emitter); s.set(sun, 'strength', strength, strength, strength); s.set(sun, 'angle', 0.0)
        s.object(sun, sun_tfm)
        o = quad_scene(s, s.diffuse(A))[1]
        points = points_for()[0]
        cosine = to_sun[2]
        lit = tuple(a * strength * cosine / math.pi for a in A)
        rows = [compare(s.bake(o, 'diffuse_direct', W, H, 4, INCLUDE_ALBEDO), per_pixel(points, lambda p: lit, 3), 'lit'),
                compare(s.bake(o, 'combined', W, H, 4), per_pixel(points, lambda p: lit + (1.0,), 4), 'lit combined')]
        # A blocker: a large rectangle at z = 2, covering x < 0.
        blocker = s.mesh(s.diffuse((0, 0, 0)), (-50., -50., 2., 0., -50., 2., 0., 50., 2., -50., 50., 2.), QUAD_TRIANGLES)
        s.object(blocker)

        def shadowed(p):
            t = (2.0 - p[2]) / to_sun[2]
            return p[0] + t * to_sun[0] < 0.0
        # Pixels whose jitter could straddle the shadow boundary are excluded by baking one sample, at the texel centre.
        expected = per_pixel(points, lambda p: (0.0, 0.0, 0.0) if shadowed(p) else lit, 3)
        require(any(p and shadowed(p) for p in points) and any(p and not shadowed(p) for p in points), 'the blocker must split the plane')
        rows.append(compare(s.bake(o, 'diffuse_direct', W, H, 1, INCLUDE_ALBEDO), expected, 'half shadowed'))
        return rows


def furnace(s, a, e, bounces):
    """A closed box of emissive diffuse surfaces, lit only through BSDF sampling: no light sampling, no Russian roulette."""
    shader = s.create('shader')
    diffuse = s.node(shader, 'diffuse_bsdf'); emission = s.node(shader, 'emission'); add = s.node(shader, 'add_closure')
    s.set(diffuse, 'color', *a); s.set(emission, 'color', *e); s.set(emission, 'strength', 1.0)
    s.link(diffuse, 'BSDF', add, 'Closure1'); s.link(emission, 'Emission', add, 'Closure2'); s.link(add, 'Closure', s.output(shader), 'Surface')
    s.string(shader, 'emission_sampling_method', 'none')
    s.ints(s.integrator, 'max_bounce', bounces); s.ints(s.integrator, 'max_diffuse_bounce', bounces); s.ints(s.integrator, 'min_bounce', bounces)
    # The cube's outward faces wind so the normal points inward: the bake target's surface faces the inside.
    xyz = (-1., -1., -1., 1., -1., -1., 1., 1., -1., -1., 1., -1., -1., -1., 1., 1., -1., 1., 1., 1., 1., -1., 1., 1.)
    tris = (0, 1, 2, 0, 2, 3,  5, 4, 7, 5, 7, 6,  4, 0, 3, 4, 3, 7,  1, 5, 6, 1, 6, 2,  3, 2, 6, 3, 6, 7,  4, 5, 1, 4, 1, 0)
    uv = (0.0, 0.0, 1.0, 0.0, 1.0, 1.0,  0.0, 0.0, 1.0, 1.0, 0.0, 1.0) + (-1.0,) * 60
    m = s.mesh(shader, xyz, tris, uv)
    return s.object(m)


@case
def closed_box_series():
    """Inside a closed box every path bounces until the limit: Combined = E (1 + a + ... + a^(n+1)), Emission = E, direct = E, indirect = E (a + ... + a^n), both divided by the colour once as the passes define."""
    a, e = (.5, .25, .75), (.5, 1.0, .25)
    n = 4
    with Scene() as s:
        o = furnace(s, a, e, n)
        size = 6
        triangles, _ = s.map(o, size, size, None)
        expected_covered = [t >= 0 for t in triangles]
        require(all(expected_covered), 'the two faces with UVs must cover the whole map')

        def constant(v): return [c for _ in range(size * size) for c in v]
        # max_bounce n lets a path shade n + 1 surfaces after the first (kernel/integrator/path_state.h: the last only emits).
        combined = tuple(e[k] * sum(a[k] ** i for i in range(n + 2)) for k in range(3))
        # The path's first hit emits E; the next surface it hits emits E times a: direct with colour; the rest is indirect.
        direct = tuple(e[k] for k in range(3))
        indirect = tuple(e[k] * sum(a[k] ** i for i in range(1, n + 1)) for k in range(3))
        return [compare(s.bake(o, 'combined', size, size, 8), constant(combined + (1.0,)), 'combined'),
                compare(s.bake(o, 'emission', size, size, 8), constant(e), 'emission'),
                compare(s.bake(o, 'diffuse_color', size, size, 8), constant(a), 'colour'),
                compare(s.bake(o, 'diffuse_direct', size, size, 8), constant(direct), 'direct'),
                compare(s.bake(o, 'diffuse_indirect', size, size, 8), constant(indirect), 'indirect')]


@case
def ambient_occlusion_open_and_closed():
    """AO is 1 on an open plane and 0 inside a closed box; the AO distance decides."""
    with Scene() as s:
        _, o = quad_scene(s, s.diffuse((1, 1, 1)))
        points = points_for()[0]
        rows = [compare(s.bake(o, 'ao', W, H, 8), per_pixel(points, lambda p: (1.0, 1.0, 1.0), 3), 'open')]
    with Scene() as s:
        o = furnace(s, (1.0, 1.0, 1.0), (0.0, 0.0, 0.0), 1)
        rows.append(compare(s.bake(o, 'ao', 6, 6, 8), [0.0] * 108, 'closed'))
    return rows


def incoming_shader(s):
    shader = s.create('shader'); geometry = s.node(shader, 'geometry'); emission = s.node(shader, 'emission')
    s.set(emission, 'strength', 1.0)
    s.link(geometry, 'Incoming', emission, 'Color'); s.link(emission, 'Emission', s.output(shader), 'Surface')
    s.string(shader, 'emission_sampling_method', 'none')
    return shader


@case
def view_direction_with_and_without_camera():
    """Without the flag the view direction is the normal (P + N towards P); with it, towards the camera. The Incoming vector shows it."""
    camera_position = (.75, -.5, 4.0)
    # Looks along -Z from camera_position (the position is what the direction uses).
    with Scene() as s:
        s.set(s.camera, 'matrix', -1., 0., 0., camera_position[0], 0., 1., 0., camera_position[1], 0., 0., -1., camera_position[2])
        o = quad_scene(s, incoming_shader(s))[1]
        points = points_for()[0]
        rows = [compare(s.bake(o, 'emission', W, H, 1), per_pixel(points, lambda p: (0.0, 0.0, 1.0), 3), 'along the normal')]

        def towards(p):
            d = normalize(tuple(camera_position[k] - p[k] for k in range(3)))
            require(d[2] > .2, 'the test keeps the camera well above the surface: the kernel nudges below 0.2')
            return d
        rows.append(compare(s.bake(o, 'emission', W, H, 1, USE_CAMERA), per_pixel(points, towards, 3), 'towards the camera'))
        return rows


@case
def background_bakes_the_world():
    """The background type evaluates the world along the direction from the origin to each surface point: a world whose colour is its direction reads normalize(P)."""
    with Scene() as s:
        world = s.create('shader'); coordinate = s.node(world, 'texture_coordinate'); bg = s.node(world, 'background_shader')
        s.set(bg, 'strength', 1.0)
        s.link(coordinate, 'Generated', bg, 'Color'); s.link(bg, 'Background', s.output(world), 'Surface')
        s.string(world, 'emission_sampling_method', 'none')
        s.nodes(s.background, 'shader', world)
        o = quad_scene(s, s.diffuse((.5, .5, .5)), TFM)[1]
        points = points_for(TFM)[0]
        return compare(s.bake(o, 'background', W, H, 1), per_pixel(points, normalize, 3), 'direction world')


@case
def id_depth_mist_and_aov():
    """Object and material index from their sockets; depth from the camera axis; mist from the film's sockets; AOVs from the shader."""
    with Scene() as s:
        shader = s.create('shader'); diffuse = s.node(shader, 'diffuse_bsdf'); aov = s.node(shader, 'aov_output')
        s.set(diffuse, 'color', .5, .5, .5); s.link(diffuse, 'BSDF', s.output(shader), 'Surface')
        s.string(aov, 'name', 'Mine'); s.set(aov, 'color', .25, .5, .75); s.set(aov, 'value', .625)
        s.string(shader, 'emission_sampling_method', 'none')
        s.ints(shader, 'pass_id', 9)
        s.set(s.camera, 'matrix', 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., -3.5)
        s.set(s.film, 'mist_start', 1.0); s.set(s.film, 'mist_depth', 8.0); s.set(s.film, 'mist_falloff', 1.0)
        m = s.mesh(shader, QUAD, QUAD_TRIANGLES, QUAD_UV)
        o = s.object(m); s.ints(o, 'pass_id', 7)
        for kind, name in (('aov_color', 'Mine'), ('aov_value', 'Mine Value')):
            p = s.create('pass'); s.string(p, 'type', kind); s.string(p, 'name', name)
        # One AOV output node writes one pass, by the name it was given: the value pass gets its own node.
        aov_value = s.node(shader, 'aov_output'); s.string(aov_value, 'name', 'Mine Value'); s.set(aov_value, 'value', .625)
        points = points_for()[0]
        rows = [compare(s.bake(o, 'object_id', W, H, 4), per_pixel(points, lambda p: (7.0,), 1), 'object id'),
                compare(s.bake(o, 'material_id', W, H, 4), per_pixel(points, lambda p: (9.0,), 1), 'material id'),
                compare(s.bake(o, 'depth', W, H, 4), [v for p in points for v in ((p[2] + 3.5,) if p else (1e10,))], 'depth (empty pixels read 1e10)')]

        def mist(p):
            distance = math.sqrt(p[0] ** 2 + p[1] ** 2 + (p[2] + 3.5) ** 2)
            return (min(max((distance - 1.0) / 8.0, 0.0), 1.0),)
        mist_film = s.bake(o, 'mist', W, H, 1)
        rows.append(compare(mist_film, [mist(p)[0] if p else 1.0 for p in points], 'mist (empty pixels read 1)'))
        rows.append(compare(s.bake(o, 'aov_color', W, H, 4, aov='Mine'), per_pixel(points, lambda p: (.25, .5, .75, 1.0), 4), 'aov colour'))
        rows.append(compare(s.bake(o, 'aov_value', W, H, 4, aov='Mine Value'), per_pixel(points, lambda p: (.625,), 1), 'aov value'))
        return rows


@case
def adaptive_sampling_and_denoising():
    """Adaptive sampling converges to the exact constant; a denoised bake runs and keeps a constant interior within one percent."""
    with Scene() as s:
        s.ints(s.integrator, 'use_adaptive_sampling', 1)
        o = lit_plane(s, s.diffuse(A))
        points = points_for()[0]
        product = tuple(a * l for a, l in zip(A, LIGHT))
        rows = [compare(s.bake(o, 'combined', W, H, 16), per_pixel(points, lambda p: product + (1.0,), 4), 'adaptive')]
    # Denoising is an image operation in UV space: interior pixels keep the value; near an edge of the layout it smears, as it would at any image edge.
    with Scene() as s:
        s.ints(s.integrator, 'use_denoise', 1)
        s.world(*LIGHT)
        o = s.object(s.mesh(s.diffuse(A), QUAD, QUAD_TRIANGLES, (0., 0., 1., 0., 1., 1., 0., 0., 1., 1., 0., 1.)))
        size = 64
        film = s.bake(o, 'combined', size, size, 16)
        require(all(math.isfinite(v) for v in film), 'denoised bake not finite')
        centre = (size // 2 * size + size // 2) * 4
        worst = max(abs(film[centre + k] - product[k]) / product[k] for k in range(3))
        require(worst < .01, f'denoised interior differs by {worst}')
        rows.append(dict(label='denoised interior', worstRelative=worst))
    return rows


@case
def embree_backend_agrees():
    """The Embree CPU backend gives the same known results."""
    with Scene(CPU_EMBREE) as s:
        o = lit_plane(s, s.diffuse(A), TFM)
        points = points_for(TFM)[0]
        product = tuple(a * l for a, l in zip(A, LIGHT))
        return [compare(s.bake(o, 'combined', W, H, 8), per_pixel(points, lambda p: product + (1.0,), 4), 'combined'),
                compare(s.bake(o, 'position', W, H), per_pixel(points, lambda p: p, 3), 'position')]


def edit_scene(s):
    world = s.world(.3, .4, .5)
    material = s.create('shader'); value = s.node(material, 'color'); diffuse = s.node(material, 'diffuse_bsdf')
    s.set(value, 'value', .7, .25, .1); s.link(value, 'Color', diffuse, 'Color'); s.link(diffuse, 'BSDF', s.output(material), 'Surface')
    s.string(material, 'emission_sampling_method', 'none')
    s.ints(s.integrator, 'seed', 3); s.ints(s.integrator, 'max_bounce', 3); s.ints(s.integrator, 'min_bounce', 2)
    s.set(s.camera, 'matrix', 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., -6.)
    mesh = s.mesh(material, QUAD, QUAD_TRIANGLES, QUAD_UV)
    obj = s.object(mesh, TFM)
    floor = s.mesh(s.diffuse((.4, .6, .4)), (-9., -3., -9., 9., -3., -9., 9., -3., 9., -9., -3., 9.), QUAD_TRIANGLES)
    s.object(floor)
    return dict(value=value, mesh=mesh, obj=obj, world=world)


T2 = rotation_z_x(-.7, .3, -.5, .5, .5, .8)
QUAD2 = (-1.0, -2.0, 0.0, 1.0, -2.0, 0.0, 1.0, 2.0, 0.5, -1.0, 2.0, 0.5)


def fresh_render(edit=None):
    """A new host that never baked: the reference for every render after a bake."""
    with Scene() as s:
        n = edit_scene(s)
        if edit: edit(s, n)
        return s.render(32, 24, 8)


def bits(values): return struct.pack(f'{len(values)}f', *values)


EDITS = dict(
    transform=lambda s, n: s.set(n['obj'], 'tfm', *T2),
    material=lambda s, n: s.set(n['value'], 'value', .1, .6, .3),
    mesh=lambda s, n: check(L.cbc_host_mesh(s.h, n['mesh'], floats(QUAD2), 4, (C.c_uint32 * 6)(*QUAD_TRIANGLES), 2)) or s.uv(n['mesh'], QUAD_UV),
    instance=lambda s, n: s.object(n['mesh'], T2),
    render_pass=lambda s, n: (lambda p: (s.string(p, 'type', 'position'), s.string(p, 'name', 'P')))(s.create('pass')),
    world=lambda s, n: s.world(.6, .1, .2),
)


@case
def render_after_bake_is_untouched():
    """A bake leaves no trace: bake then render, and render, bake, render, are bit-identical to a new host's render."""
    reference = fresh_render()
    rows = []
    with Scene() as s:
        n = edit_scene(s)
        s.bake(n['obj'], 'combined', W, H, 4)
        require(bits(s.render(32, 24, 8)) == bits(reference), 'bake, render differs from a new host')
        rows.append('bake, render')
    with Scene() as s:
        n = edit_scene(s)
        first = s.render(32, 24, 8)
        require(bits(first) == bits(reference), 'first render differs')
        s.bake(n['obj'], 'position', W, H)
        s.bake(n['obj'], 'emission', W, H, 4)
        require(bits(s.render(32, 24, 8)) == bits(reference), 'render, bake, render differs from a new host')
        rows.append('render, bake, render')
    return rows


@case
def edit_after_bake():
    """Bake, edit, render is bit-identical to a new host built with the edit; bake, edit, bake reads the edit."""
    rows = []
    for name, edit in EDITS.items():
        reference = fresh_render(edit)
        with Scene() as s:
            n = edit_scene(s)
            s.bake(n['obj'], 'combined', W, H, 4)
            edit(s, n)
            require(bits(s.render(32, 24, 8)) == bits(reference), f'{name}: bake, edit, render differs from a new host')
        rows.append(name)
    # The baked object deleted: nothing of the bake refers to it any more.
    reference = fresh_render(lambda s, n: check(L.cbc_host_delete_node(s.h, n['obj'])))
    with Scene() as s:
        n = edit_scene(s)
        s.bake(n['obj'], 'combined', W, H, 4)
        check(L.cbc_host_delete_node(s.h, n['obj']))
        require(bits(s.render(32, 24, 8)) == bits(reference), 'bake, delete, render differs from a new host')
    rows.append('deleted')
    with Scene() as s:
        n = edit_scene(s)
        points = points_for(TFM)[0]
        rows.append(compare(s.bake(n['obj'], 'position', W, H), per_pixel(points, lambda p: p, 3), 'position'))
        EDITS['transform'](s, n)
        rows.append(compare(s.bake(n['obj'], 'position', W, H), per_pixel(points_for(T2)[0], lambda p: p, 3), 'position after a transform edit'))
        EDITS['mesh'](s, n)
        triangles, coordinates = exact_map(QUAD_UV, W, H)
        moved = [transform_point(T2, p) if p else None for p in surface_points(QUAD2, QUAD_TRIANGLES, coordinates, W, H, triangles)]
        rows.append(compare(s.bake(n['obj'], 'position', W, H), per_pixel(moved, lambda p: p, 3), 'position after a mesh edit'))
        rows.append(compare(s.bake(n['obj'], 'diffuse_color', W, H, 4), per_pixel(moved, lambda p: (.7, .25, .1), 3), 'colour before a material edit'))
        EDITS['material'](s, n)
        rows.append(compare(s.bake(n['obj'], 'diffuse_color', W, H, 4), per_pixel(moved, lambda p: (.1, .6, .3), 3), 'colour after a material edit'))
    return rows


@case
def caller_passes_do_not_change_a_bake():
    """The kernel picks its path by which passes exist. A caller's position, normal and background passes must not turn a Combined bake into something else, must not duplicate the bake's own pass, and must work in the next render."""
    def add(s):
        for kind, name in (('position', 'P'), ('normal', 'N'), ('background', 'B'), ('diffuse_color', 'DC')):
            p = s.create('pass'); s.string(p, 'type', kind); s.string(p, 'name', name)
        # A light group too: a combined pass with a group, and a lamp in that group.
        group = s.create('pass'); s.string(group, 'type', 'combined'); s.string(group, 'name', 'Group'); s.string(group, 'lightgroup', 'lamps')
        sun = s.create('sunlight'); s.nodes(sun, 'used_shaders', s.emission((1, 1, 1))); s.set(sun, 'strength', 2.0, 2.0, 2.0)
        s.string(s.object(sun), 'lightgroup', 'lamps')
    with Scene() as reference_scene:
        n = edit_scene(reference_scene); add(reference_scene)
        reference = reference_scene.render(32, 24, 8)
        reference_position = reference_scene.pass_floats('P')[1]
        reference_group = reference_scene.pass_floats('Group')[1]
    with Scene() as s:
        n = edit_scene(s); add(s)
        points = points_for(TFM)[0]
        rows = [compare(s.bake(n['obj'], 'diffuse_color', W, H, 4), per_pixel(points, lambda p: (.7, .25, .1), 3), 'colour beside a caller colour pass'),
                compare(s.bake(n['obj'], 'position', W, H), per_pixel(points, lambda p: p, 3), 'position beside a caller position pass')]
        combined = s.bake(n['obj'], 'combined', W, H, 1)
        require(any(v != 0.0 for v in combined), 'the Combined bake is empty')
        require(s.pass_floats('P')[0] == NOT_READY, 'a bake must clear the last render results')
        require(bits(s.render(32, 24, 8)) == bits(reference), 'render after bakes with caller passes differs from a new host')
        require(bits(s.pass_floats('P')[1]) == bits(reference_position), 'the caller position pass differs after bakes')
        require(bits(s.pass_floats('Group')[1]) == bits(reference_group), 'the caller light group pass differs after bakes')
    return rows


@case
def bump_takes_the_shading_path():
    """A bump node sends the normal bake through shading (no fast path): a constant height leaves the geometric normal."""
    with Scene() as s:
        shader = s.create('shader'); bump = s.node(shader, 'bump'); diffuse = s.node(shader, 'diffuse_bsdf')
        s.set(bump, 'height', .5); s.set(bump, 'strength', 1.0); s.set(diffuse, 'color', .5, .5, .5)
        s.link(bump, 'Normal', diffuse, 'Normal'); s.link(diffuse, 'BSDF', s.output(shader), 'Surface')
        s.string(shader, 'emission_sampling_method', 'none')
        o = quad_scene(s, shader)[1]
        return compare(s.bake(o, 'normal', W, H, 4), per_pixel(points_for()[0], lambda p: (0.0, 0.0, 1.0), 3), 'bump normal')


@case
def true_displacement_moves_the_position():
    """True displacement happens before the bake: the position reads the displaced surface."""
    with Scene() as s:
        shader = s.create('shader'); displacement = s.node(shader, 'displacement'); diffuse = s.node(shader, 'diffuse_bsdf')
        s.set(displacement, 'height', .25); s.set(displacement, 'midlevel', 0.0); s.set(displacement, 'scale', 2.0)
        s.link(diffuse, 'BSDF', s.output(shader), 'Surface'); s.link(displacement, 'Displacement', s.output(shader), 'Displacement')
        s.string(shader, 'displacement_method', 'true')
        s.string(shader, 'emission_sampling_method', 'none')
        o = quad_scene(s, shader)[1]
        points = points_for()[0]
        return compare(s.bake(o, 'position', W, H), per_pixel(points, lambda p: (p[0], p[1], p[2] + .5), 3), 'displaced')


@case
def object_motion_bakes_at_the_static_transform():
    """With object motion and motion blur on, the kernel places the surface by the object's transform socket."""
    with Scene() as s:
        s.ints(s.integrator, 'motion_blur', 1); s.set(s.camera, 'shuttertime', .5)
        _, o = quad_scene(s, s.diffuse((.5, .5, .5)), TFM)
        steps = list(TFM) + list(TFM) + list(TFM); steps[3] -= .5; steps[27] += .5
        s.set(o, 'motion', *steps)
        return compare(s.bake(o, 'position', W, H), per_pixel(points_for(TFM)[0], lambda p: p, 3), 'position under motion')


@case
def closed_box_equals_camera_render():
    """The same closed box seen by a camera render has the same exact value: bake and camera share the integrator after the first hit."""
    a, e, n = (.5, .25, .75), (.5, 1.0, .25), 4
    with Scene() as s:
        o = furnace(s, a, e, n)
        s.set(s.camera, 'matrix', 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0.)
        expected = tuple(e[k] * sum(a[k] ** i for i in range(n + 2)) for k in range(3))
        return [compare(s.render(4, 4, 8), [c for _ in range(16) for c in expected + (1.0,)], 'camera'),
                compare(s.bake(o, 'combined', 4, 4, 8), [c for _ in range(16) for c in expected + (1.0,)], 'bake')]


@case
def noisy_light_matches_camera_render():
    """A plane lit by two soft lamps, viewed head on by an orthographic camera whose pixels are the bake's texels: bake and camera are independent estimates of one image."""
    size = 16
    with Scene() as s:
        emitter = s.emission((1, 1, 1))
        for x, y, power in ((-.8, .5, 8.0), (.9, -.4, 5.0)):
            lamp = s.create('pointlight'); s.nodes(lamp, 'used_shaders', emitter); s.set(lamp, 'radius', .25); s.ints(lamp, 'is_sphere', 1); s.ints(lamp, 'use_mis', 1)
            s.set(lamp, 'strength', power, power, power)
            holder = s.object(lamp, (1., 0., 0., x, 0., 1., 0., y, 0., 0., 1., -1.5))
            # The lamps stand between the camera and the plane: PATH_RAY_CAMERA off.
            visibility = C.c_uint64(); required = C.c_uint64()
            check(L.cbc_host_get_uints(s.h, holder, b'visibility', C.byref(visibility), 1, C.byref(required)))
            value = C.c_uint64(visibility.value & ~1)
            check(L.cbc_host_set_uints(s.h, holder, b'visibility', C.byref(value), 1))
        s.ints(s.integrator, 'max_bounce', 0)
        # Facing -Z, towards the camera at z = -4 (the camera looks along +Z).
        plane = s.mesh(s.diffuse(A), (-1., -1., 0., 1., 1., 0., 1., -1., 0., -1., 1., 0.), (0, 1, 2, 0, 3, 1), (0., 0., 1., 1., 1., 0., 0., 0., 0., 1., 1., 1.))
        o = s.object(plane)
        s.string(s.camera, 'camera_type', 'orthograph')
        s.set(s.camera, 'matrix', 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., -4.)
        samples = 1024
        camera = s.render(size, size, samples)
        bake = s.bake(o, 'combined', size, size, samples)
        pixel_errors = [abs(a - b) / max(b, 1e-3) for a, b in zip(bake, camera)]
        mean_a, mean_b = sum(bake) / len(bake), sum(camera) / len(camera)
        require(sum(camera[:3]) > 0 and abs(mean_a - mean_b) <= .005 * mean_b, f'image means {mean_a} and {mean_b}')
        require(max(pixel_errors) <= .05, f'worst pixel differs by {max(pixel_errors)}')
        return dict(meanBake=mean_a, meanCamera=mean_b, worstPixelRelative=max(pixel_errors))


@case
def repeatable_across_threads():
    """A noisy bake repeats bit for bit and does not depend on the thread count, as a CPU render does. This is repeatability only: the known-result cases above are the proof of correctness."""
    films = []
    for threads in (1, 4, 1):
        config = HostConfig(C.sizeof(HostConfig), 1, CPU_REFERENCE, threads)
        s = Scene.__new__(Scene)
        Scene.__init__(s)
        check(L.cbc_host_destroy(s.h))
        h = C.c_uint64(); check(L.cbc_host_create(C.byref(config), C.byref(h))); s.h = h.value
        s.integrator, s.camera, s.film, s.background = (s.singleton(n) for n in ('integrator', 'camera', 'film', 'background'))
        for name in ('use_light_tree', 'use_guiding', 'use_adaptive_sampling', 'use_denoise', 'caustics_reflective', 'caustics_refractive'):
            s.ints(s.integrator, name, 0)
        s.string(s.integrator, 'sampling_pattern', 'tabulated_sobol')
        n = edit_scene(s)
        films.append(bits(s.bake(n['obj'], 'combined', W, H, 16)))
        s.close()
    require(films[0] == films[1] == films[2], 'a CPU bake is not repeatable')
    return 'bit-identical'


@case
def approximate_shadow_catcher_refused():
    """With an approximate shadow catcher and an opaque background the film gains a background pass, which the kernel reads as a request to bake the world: refused by name."""
    with Scene() as s:
        _, o = quad_scene(s, s.diffuse((.5, .5, .5)))
        s.ints(o, 'is_shadow_catcher', 1); s.ints(s.film, 'use_approximate_shadow_catcher', 1)
        config = s.config(o, 'combined', W, H)
        values = (C.c_float * (W * H * 4))(); required, components = C.c_uint64(), C.c_uint32()
        got = L.cbc_host_bake(s.h, C.byref(config), values, W * H * 4, C.byref(required), C.byref(components))
        require(got == UNSUPPORTED and 'shadow catcher' in last_error(), f'status {got}: {last_error()}')
        s.ints(s.film, 'use_approximate_shadow_catcher', 0)
        return compare(s.bake(o, 'normal', W, H, 2), per_pixel(points_for()[0], lambda p: (0.0, 0.0, 1.0), 3), 'accurate shadow catcher bakes')


@case
def refusals_and_size_queries():
    """Wrong input and missing UVs are refused by name; the size query does not bake; the Combined bake of a mesh with no UV attribute fails with CBC_INVALID."""
    with Scene() as s:
        shader = s.diffuse((.5, .5, .5))
        m, o = quad_scene(s, shader)
        no_uv = s.object(s.mesh(shader, QUAD, QUAD_TRIANGLES))
        values = (C.c_float * (W * H * 4))(); required, components = C.c_uint64(), C.c_uint32()

        def status(config, capacity=W * H * 4, buffer=values):
            return L.cbc_host_bake(s.h, C.byref(config), buffer, capacity, C.byref(required), C.byref(components))
        good = s.config(o, 'combined', W, H, 2)
        require(status(good, 0, None) == SMALL_BUFFER and required.value == W * H * 4 and components.value == 4, 'size query')
        require(status(good, W * H * 4 - 1) == SMALL_BUFFER, 'short buffer')
        for label, config, expected in (
                ('no UV attribute', s.config(no_uv, 'combined', W, H), INVALID),
                ('unknown UV name', s.config(o, 'combined', W, H, uv='nothing'), INVALID),
                ('zero size', s.config(o, 'combined', 0, H), INVALID),
                ('zero samples', s.config(o, 'combined', W, H, 0), INVALID),
                ('huge', s.config(o, 'combined', 40000, H), INVALID),
                ('a non-object', s.config(m, 'combined', W, H), INVALID),
                ('stale handle', s.config(987654321, 'combined', W, H), INVALID),
                ('an AOV name on another type', s.config(o, 'combined', W, H, aov='x'), INVALID),
                ('an AOV bake without a name', s.config(o, 'aov_color', W, H), INVALID),
                ('an AOV bake without its pass', s.config(o, 'aov_color', W, H, aov='nothing'), INVALID)):
            got = status(config)
            require(got == expected, f'{label}: status {got} ({last_error()})')
        bad = s.config(o, 'combined', W, H); bad.type = 99
        require(status(bad) == INVALID and 'bake type' in last_error(), 'unknown type')
        bad = s.config(o, 'combined', W, H); bad.flags = 64
        require(status(bad) == INVALID, 'unknown flag')
        bad = s.config(o, 'combined', W, H); bad.reserved[0] = 1
        require(status(bad) == INVALID, 'reserved word')
        bad = s.config(o, 'combined', W, H); bad.size = 32
        require(status(bad) == INVALID, 'wrong struct size')
        # After all those refusals the host still bakes and renders.
        require(len(s.bake(o, 'normal', W, H)) == W * H * 3, 'bake after refusals')
        return 'all refused by name'


@case
def webgpu_refuses_by_name():
    """On the WebGPU device a bake is refused with a message that says so, and never falls back. The map itself needs no device."""
    resolved = C.c_uint32()
    if L.cbc_host_resolve_backend(WEBGPU, C.byref(resolved)) != OK:
        return 'not compiled'
    try:
        s = Scene(WEBGPU)
    except RuntimeError as error:
        return f'no WebGPU device here: {error}'
    with s:
        _, o = quad_scene(s, s.diffuse((.5, .5, .5)))
        values = (C.c_float * (W * H * 4))(); required, components = C.c_uint64(), C.c_uint32()
        config = s.config(o, 'combined', W, H)
        got = L.cbc_host_bake(s.h, C.byref(config), values, W * H * 4, C.byref(required), C.byref(components))
        message = last_error()
        require(got == UNSUPPORTED and 'WebGPU device does not bake' in message, f'status {got}: {message}')
        require(not any(values), 'the output changed')
        triangles, _ = s.map(o, W, H)
        require(triangles == exact_map(QUAD_UV, W, H)[0], 'the map differs on the WebGPU host')
        return message


@case
def cancel_from_callback():
    """A callback that returns nonzero cancels a bake (CBC_CANCELLED), the output stays unchanged, and the host bakes again."""
    with Scene() as s:
        _, o = quad_scene(s, s.emission((1, 1, 1)))
        callback = EVENT(lambda user, event: 1)
        check(L.cbc_host_set_event_callback(s.h, callback, None))
        config = s.config(o, 'combined', W, H, 4)
        values = (C.c_float * (W * H * 4))(*([-7.0] * (W * H * 4))); required, components = C.c_uint64(), C.c_uint32()
        got = L.cbc_host_bake(s.h, C.byref(config), values, W * H * 4, C.byref(required), C.byref(components))
        require(got == CANCELLED, f'status {got}: {last_error()}')
        require(all(v == -7.0 for v in values), 'a cancelled bake wrote the output')
        check(L.cbc_host_set_event_callback(s.h, EVENT(0), None))
        return compare(s.bake(o, 'emission', W, H, 2), per_pixel(points_for()[0], lambda p: (1.0, 1.0, 1.0), 3), 'bake after a cancel')


def main():
    global L
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--library', type=Path, default=WORK/'cycles-build/lib/libcodebrix_cycles_host.so')
    parser.add_argument('--case', default='')
    args = parser.parse_args()
    L = C.CDLL(str(args.library.resolve()))
    L.cbc_host_set_event_callback.argtypes = [C.c_uint64, EVENT, C.c_void_p]
    # Handles are 64-bit: every call passes them explicitly as such.
    for name, types in dict(cbc_host_bake=[C.c_uint64, C.c_void_p, C.c_void_p, C.c_uint64, C.c_void_p, C.c_void_p],
                            cbc_host_bake_map=[C.c_uint64, C.c_void_p, C.c_void_p, C.c_void_p, C.c_uint64, C.c_void_p],
                            cbc_host_attribute_floats=[C.c_uint64, C.c_uint64, C.c_char_p, C.c_void_p, C.c_void_p, C.c_uint64],
                            cbc_host_set_floats=[C.c_uint64, C.c_uint64, C.c_char_p, C.c_void_p, C.c_uint64],
                            cbc_host_set_ints=[C.c_uint64, C.c_uint64, C.c_char_p, C.c_void_p, C.c_uint64],
                            cbc_host_set_string=[C.c_uint64, C.c_uint64, C.c_char_p, C.c_char_p],
                            cbc_host_set_nodes=[C.c_uint64, C.c_uint64, C.c_char_p, C.c_void_p, C.c_uint64],
                            cbc_host_scene_node=[C.c_uint64, C.c_char_p, C.c_void_p],
                            cbc_host_create_node=[C.c_uint64, C.c_char_p, C.c_void_p],
                            cbc_host_shader_node=[C.c_uint64, C.c_uint64, C.c_char_p, C.c_void_p],
                            cbc_host_shader_output=[C.c_uint64, C.c_uint64, C.c_void_p],
                            cbc_host_connect=[C.c_uint64, C.c_uint64, C.c_char_p, C.c_uint64, C.c_char_p],
                            cbc_host_mesh=[C.c_uint64, C.c_uint64, C.c_void_p, C.c_uint64, C.c_void_p, C.c_uint64],
                            cbc_host_render=[C.c_uint64, C.c_void_p, C.c_void_p, C.c_uint64],
                            cbc_host_pass_floats=[C.c_uint64, C.c_char_p, C.c_void_p, C.c_uint64, C.c_void_p, C.c_void_p],
                            cbc_host_image_create=[C.c_uint64, C.c_void_p, C.c_void_p, C.c_uint64, C.c_void_p],
                            cbc_host_image_bind=[C.c_uint64, C.c_uint64, C.c_uint64],
                            cbc_host_get_uints=[C.c_uint64, C.c_uint64, C.c_char_p, C.c_void_p, C.c_uint64, C.c_void_p],
                            cbc_host_set_uints=[C.c_uint64, C.c_uint64, C.c_char_p, C.c_void_p, C.c_uint64],
                            cbc_host_delete_node=[C.c_uint64, C.c_uint64],
                            cbc_host_error=[C.c_void_p, C.c_uint64, C.c_void_p],
                            cbc_host_resolve_backend=[C.c_uint32, C.c_void_p],
                            cbc_host_destroy=[C.c_uint64]).items():
        getattr(L, name).argtypes = types
    require(C.sizeof(BakeConfig) == 64, 'CBCBakeConfig layout')
    out = args.output.resolve(); out.mkdir(parents=True, exist_ok=True)
    rows = []
    for function in CASES:
        if args.case not in function.__name__: continue
        started = time.time()
        try:
            detail = function(); passed = True
        except Exception as error:  # Every failure is a row; the gate reports them all.
            detail = ''.join(traceback.format_exception_only(type(error), error)).strip(); passed = False
            if not isinstance(error, Failed): detail += '\n' + traceback.format_exc()
        rows.append(dict(case=function.__name__, passed=passed, seconds=round(time.time()-started, 2), detail=detail, proves=function.__doc__))
        print(('PASS ' if passed else 'FAIL ') + function.__name__ + f' ({rows[-1]["seconds"]} s)' + ('' if passed else ': ' + str(detail)), flush=True)
    passed = all(r['passed'] for r in rows) and bool(rows)
    (out/'status.json').write_text(json.dumps(dict(complete=True, passed=passed, cases=len(rows), failed=[r['case'] for r in rows if not r['passed']], rows=rows), indent=1, default=str))
    return 0 if passed else 1


if __name__ == '__main__':
    sys.exit(main())
