// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Scene-driven WGSL specialization and pipeline compile reporting.
//
// Vulkan drivers inline every WGSL function into each entry point. The generic
// SVM interpreter therefore hands the driver one function containing every
// supported node handler, which exhausted the HP Intel compiler's allocator and
// took minutes on the Pixel's Adreno compiler. Production modules mark optional
// node/closure handlers with comment regions:
//
//   //#if FEATURE else FALLBACK
//   ...handler...
//   //#endif
//
// The unspecialized module keeps every handler (marker lines are comments, so
// the WGSL is unchanged). A specialized module removes handlers for features the
// uploaded scene cannot reach and substitutes FALLBACK, which always reports an
// unsupported-opcode error rather than producing substitute radiance.
#include <cstdint>
#include <string>
#include <vector>

namespace ccl {
enum ShaderFeatureBit : uint64_t {
  SF_GEOMETRY=1ull<<0, SF_TEX_COORD=1ull<<1, SF_IMAGE=1ull<<2, SF_ATTR=1ull<<3, SF_NORMAL_MAP=1ull<<4,
  SF_TANGENT=1ull<<5, SF_VERTEX_COLOR=1ull<<6, SF_MATH=1ull<<7, SF_VECTOR_MATH=1ull<<8, SF_CLAMP=1ull<<9,
  SF_CONVERT=1ull<<10, SF_SEPARATE_VECTOR=1ull<<11, SF_COMBINE_VECTOR=1ull<<12, SF_CLOSURE_VOLUME=1ull<<13,
  SF_CLOSURE_HOLDOUT=1ull<<14, SF_CLOSURE_SET_NORMAL=1ull<<15, SF_TEX_CHECKER=1ull<<16, SF_TEX_GRADIENT=1ull<<17,
  SF_GAMMA=1ull<<18, SF_INVERT=1ull<<19, SF_BRIGHTCONTRAST=1ull<<20,
  SF_BSDF_DIFFUSE=1ull<<21, SF_BSDF_TRANSLUCENT=1ull<<22, SF_BSDF_TRANSPARENT=1ull<<23,
  SF_BSDF_GLOSSY=1ull<<24, SF_BSDF_GLASS=1ull<<25, SF_LIGHT_AREA=1ull<<26,
  SF_MOTION_OBJECT=1ull<<27, SF_MOTION_CAMERA=1ull<<28,
  // Emissive-triangle sampling and forward-hit MIS: any triangle in the light
  // distribution or any shader flagged as a MIS emitter (SD_MIS_FRONT/BACK).
  SF_LIGHT_TRIANGLE=1ull<<29,
  // Object shadow terminator offsets (Phase E3): the geometry offset on
  // smooth-shaded triangles with direct lighting, the shading offset whenever
  // an object's frequency multiplier exceeds 1.
  SF_TERMINATOR_GEOMETRY=1ull<<30, SF_TERMINATOR_SHADING=1ull<<31,
  // Principled BSDF closure setup, its Fresnel models and the sheen closure.
  SF_BSDF_PRINCIPLED=1ull<<32,
  // Principled thin-film iridescence and thin-walled transmission.
  SF_BSDF_THIN_FILM=1ull<<33, SF_BSDF_THIN_WALL=1ull<<34,
  // Texture, colour and utility node handlers (wavefront_svm_nodes.wgsl).
  SF_TEX_NOISE=1ull<<35, SF_TEX_WHITE_NOISE=1ull<<36, SF_TEX_WAVE=1ull<<37, SF_TEX_MAGIC=1ull<<38,
  SF_TEX_BRICK=1ull<<39, SF_MAPPING=1ull<<40, SF_MAP_RANGE=1ull<<41, SF_MIX=1ull<<42, SF_HSV=1ull<<43,
  SF_COLOR_SEPCOMB=1ull<<44, SF_RAMP=1ull<<45, SF_FRESNEL=1ull<<46, SF_VECTOR_ROTATE=1ull<<47,
  SF_BLACKBODY=1ull<<48, SF_NORMAL=1ull<<49, SF_SHADING_INFO=1ull<<50, SF_LIGHT_PATH=1ull<<51,
  SF_TEX_VORONOI=1ull<<52,
  // Wavelength and wireframe handlers (wavefront_svm_nodes.wgsl).
  SF_WAVELENGTH=1ull<<53, SF_WIREFRAME=1ull<<54,
  // Bump stages and dual-number derivative variants (wavefront_svm_derivatives.wgsl;
  // mapping variants in wavefront_svm_nodes.wgsl). Bits 62 and 63 stay free.
  SF_BUMP=1ull<<55, SF_BUMP_STATE=1ull<<56, SF_GEOMETRY_DERIVATIVE=1ull<<57,
  SF_TEX_COORD_DERIVATIVE=1ull<<58, SF_VALUE_DERIVATIVE=1ull<<59,
  SF_VECTOR_MATH_DERIVATIVE=1ull<<60, SF_MAPPING_DERIVATIVE=1ull<<61,
  SF_ALL=(1ull<<62)-1,
  SF_PRINCIPLED_ALL=SF_BSDF_PRINCIPLED|SF_BSDF_THIN_FILM|SF_BSDF_THIN_WALL,
};
struct ShaderFeatureName {const char *name;uint64_t bit;};
const std::vector<ShaderFeatureName> &shader_feature_names();
std::string shader_feature_list(uint64_t features);

struct SvmCensus {
  uint64_t features=0;      // Handlers any reachable decode path can execute.
  uint64_t decoded_nodes=0; // Distinct node records decoded.
  uint64_t stopped_paths=0; // Paths ending where the GPU interpreter reports an error.
  // First reachable feature the device rejects before rendering (empty: none).
  // Principled subsurface is refused here when a constant enables it or its
  // weight is linked; the GPU handler also reports it (error 16) and never
  // approximates it.
  std::string unsupported;
};
// Mirrors the WGSL interpreter's decode exactly: every program start reachable
// through a shader header, shader jump or conditional jump, record sizes from
// the native SVM node layouts, BSDF data records and texture-coordinate
// transforms. Paths stop where the GPU would stop (END, unknown opcode/closure,
// or a bounds error); the result is a superset of executable handlers.
// `roots` (optional) limits the program starts to these shader indices.
SvmCensus svm_census(const std::vector<uint32_t> &words,size_t shader_count,const std::vector<uint32_t> *roots=nullptr);

// Applies feature markers. Unknown feature names or malformed regions throw.
std::string specialize_wgsl(const std::string &source,uint64_t enabled);

// Declared per-pipeline compile budget (ms). Exceeding it marks the session
// degraded (first-use latency) with a warning; it never changes the variant.
// Override with CODEBRIX_CYCLES_PIPELINE_BUDGET_MS (0 disables). A non-fatal
// pipeline-creation failure retries once in the conservative tier; the tier
// is otherwise chosen before any compile by device_tier.h (request, device
// record, known-adapter table, canary), can be forced with
// CODEBRIX_CYCLES_PIPELINE_TIER=conservative|full, or chosen
// after a budget overrun with CODEBRIX_CYCLES_PIPELINE_BUDGET_ACTION=conservative
// (old-hardware deployments only; default "warn").
inline constexpr double pipeline_default_budget_ms=60000.0;
void pipeline_process_warning(const std::string &message);
// Conservative tier: entries declared @workgroup_size(64) are rewritten to
// @workgroup_size(1) (precedent: surface and transparent-shadow stages). Only
// the declaration changes and the dispatch follows it; driver code generation
// may still differ in the last bits, so this tier is never chosen silently.
std::string conservative_wgsl(const std::string &source);
// Workgroup size declared for an entry point in a WGSL module (0 if absent).
uint32_t declared_workgroup_size(const std::string &source,const char *entry);
// Process-wide summary of every pipeline the WebGPU devices created, including
// tier, degradation flag and warnings, for gate/replay/capability JSON.
std::string pipeline_process_report_json();
// Latest device-tier decision (DeviceTierPolicy::report_json) for the process report.
void pipeline_process_decision(const std::string &json);
}
