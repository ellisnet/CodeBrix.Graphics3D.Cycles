// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Harness for the ORIGINAL upstream traversal.h (static, non-instanced triangles).
#include "reference.cpp"
#include <stdexcept>
#include "kernel/bvh/types.h"

namespace ccl {
using KernelGlobals = int;
struct Self { int prim = -1; int object = -1; };
struct Ray { float3 P, D; float tmin, tmax; Self self; };
struct Intersection { float t, u, v; int prim, object, type; };
static constexpr int OBJECT_NONE = -1, PRIM_NONE = -1;
static constexpr uint PRIMITIVE_TRIANGLE = 1, PRIMITIVE_ALL = 0xffff;
static constexpr uint PATH_RAY_VISIBILITY_SHADOW_OPAQUE = 128;
static struct { struct { int root = 1; } bvh; } kernel_data;
static const float *scene;
// Used only by the image prototype's shadow reference; default probe behavior
// remains unchanged. This small harness is intentionally single-threaded.
static int reference_skip_primitive = -1;
static float4 load4(int row) { return make_float4(scene[row*4], scene[row*4+1], scene[row*4+2], scene[row*4+3]); }
static float4 fetch_bvh_nodes(int i) { return load4(i); }
static float4 fetch_bvh_leaf_nodes(int i) { return load4(int(scene[1]) + i); }
static int fetch_prim_object(int) { return 0; }
static int fetch_prim_index(int i) { return int(scene[(int(scene[2]) + i)*4]); }
static int fetch_object_node(int) { throw std::runtime_error("Instancing outside probe scope"); }
static bool intersection_skip_self_shadow(Self, int, int) { return false; }
static void bvh_instance_push(KernelGlobals, int, const Ray*, float3*, float3*, float3*) { throw std::runtime_error("Instancing outside probe scope"); }
static void bvh_instance_pop(const Ray*, float3*, float3*, float3*) { throw std::runtime_error("Instancing outside probe scope"); }
#define kernel_assert(x) util_assert(x)
#define kernel_data_fetch(table, index) fetch_##table(index)
// Exact source excerpts; generated and hashed by bvh.py.
#include "bvh_extracted.h"
static bool triangle_intersect(KernelGlobals, Intersection *isect, float3 P, float3 D,
                               float tmin, float tmax, uint, int object, int prim, int)
{
    if (prim == reference_skip_primitive) return false;
    int row = int(scene[3]) + prim * 3;
    auto v = [&](int i) { auto a = load4(i); return make_float3(a.x, a.y, a.z); };
    float u, w, t;
    if (!ray_triangle_intersect(P, D, tmin, tmax, v(row), v(row+1), v(row+2), &u, &w, &t)) return false;
    isect->t = t; isect->u = u; isect->v = w;
    isect->prim = prim; isect->object = object; isect->type = PRIMITIVE_TRIANGLE;
    return true;
}
#define BVH_FUNCTION_NAME bvh_intersect
#define BVH_FUNCTION_FEATURES 0
#include "kernel/bvh/traversal.h"
}

extern "C" void cycles_bvh(const float *data, const float *rays, float *out, int count)
{
    using namespace ccl;
    scene = data;
    for (int i = 0; i < count; ++i) {
        const float *r = rays + i*8;
        Ray ray{make_float3(r[0], r[1], r[2]), make_float3(r[4], r[5], r[6]), r[3], r[7], {}};
        Intersection isect{};
        BVH_bvh_intersect(0, &ray, &isect, 0);
        out[i*4] = isect.t; out[i*4+1] = isect.u;
        out[i*4+2] = isect.v; out[i*4+3] = float(isect.prim);
    }
}
