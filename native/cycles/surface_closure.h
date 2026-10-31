// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "util/types.h"
namespace ccl {
// Canonical arena record, independent of native ShaderClosure pointer layout.
// Unused fields are zero. WGSL uses the compiler-generated size and offsets.
// Fields are reused per closure kind (the record size is per-path GPU memory):
// Oren-Nayar: roughness/a/b/multiscatter. Microfacet: roughness holds the
// native MicrofacetFresnel model as a float (0 none or Glass-node scalar
// fresnel_f0 in specialized scenes without Principled, 1 dielectric,
// 3 conductor (the Metallic BSDF's physical conductor: multiscatter = n,
// reflection_tint = k), 4 generalized Schlick with exponent < 0 and f90 = 1,
// 5 F82 tint), multiscatter the Fresnel f0, and an F82 closure stores its b term
// in reflection_tint. Beckmann and Ashikhmin-Shirley records are microfacet
// records with energy_scale 0. Velvet: roughness = sigma, a = 1/max(sigma, 0.01)^2.
// Toon: a = size, b = smooth (both scaled by pi/2).
// An active Principled thin film (thickness > 0.1) is a = thickness, b = film
// IOR; zero otherwise. The thin-walled transmission lobe is a GGX record.
// Sheen: roughness, a/b = LTC transformA/B, T = T and multiscatter = B.
struct SurfaceClosureRecord {
  packed_float3 weight;float sample_weight;packed_float3 N;uint kind;
  float roughness,a,b;packed_float3 multiscatter;
  float alpha_x,alpha_y,ior,energy_scale;packed_float3 T;
  float fresnel_f0;packed_float3 reflection_tint,transmission_tint;
};
static_assert(sizeof(SurfaceClosureRecord)==112);
}
