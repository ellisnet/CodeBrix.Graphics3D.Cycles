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
// Specialization feature mask. One bit per optional WGSL region family; bit n
// lives in word n/64 at position n%64. The mask is a fixed array of
// shader_feature_words 64-bit words (256 bits; widened from one 64-bit word in
// Step zero, 2026-10-22, when bits 0-61 and 63 were in use). Bit numbers never
// change meaning once assigned: films, gate records and evidence name features
// by their marker names (shader_feature_names), and the persisted pipeline log
// and reports carry those names, never the numeric mask. New features take the
// next free bit (62, then 64 onwards). Widening further only changes
// shader_feature_words.
inline constexpr unsigned shader_feature_words=4;
inline constexpr unsigned shader_feature_bits=64*shader_feature_words;
struct ShaderFeatures {
  uint64_t words[shader_feature_words]{};
  static constexpr ShaderFeatures bit(unsigned n){ShaderFeatures f;f.words[n/64]=uint64_t(1)<<(n%64);return f;}
  // Bits [first, last) set.
  static constexpr ShaderFeatures range(unsigned first,unsigned last){ShaderFeatures f;for(unsigned n=first;n<last;n++)f.words[n/64]|=uint64_t(1)<<(n%64);return f;}
  constexpr bool test(unsigned n) const{return n<shader_feature_bits&&((words[n/64]>>(n%64))&1u)!=0;}
  constexpr bool any() const{for(uint64_t w:words)if(w)return true;return false;}
  constexpr unsigned count() const{unsigned c=0;for(uint64_t w:words)for(;w;w&=w-1)c++;return c;}
  explicit constexpr operator bool() const{return any();}
  constexpr ShaderFeatures operator|(const ShaderFeatures &o) const{ShaderFeatures f;for(unsigned i=0;i<shader_feature_words;i++)f.words[i]=words[i]|o.words[i];return f;}
  constexpr ShaderFeatures operator&(const ShaderFeatures &o) const{ShaderFeatures f;for(unsigned i=0;i<shader_feature_words;i++)f.words[i]=words[i]&o.words[i];return f;}
  constexpr ShaderFeatures operator~() const{ShaderFeatures f;for(unsigned i=0;i<shader_feature_words;i++)f.words[i]=~words[i];return f;}
  constexpr ShaderFeatures &operator|=(const ShaderFeatures &o){for(unsigned i=0;i<shader_feature_words;i++)words[i]|=o.words[i];return *this;}
  constexpr ShaderFeatures &operator&=(const ShaderFeatures &o){for(unsigned i=0;i<shader_feature_words;i++)words[i]&=o.words[i];return *this;}
  constexpr bool operator==(const ShaderFeatures &o) const{for(unsigned i=0;i<shader_feature_words;i++)if(words[i]!=o.words[i])return false;return true;}
  constexpr bool operator!=(const ShaderFeatures &o) const{return !(*this==o);}
};
inline constexpr ShaderFeatures SF_NONE{};
inline constexpr ShaderFeatures SF_GEOMETRY=ShaderFeatures::bit(0);
inline constexpr ShaderFeatures SF_TEX_COORD=ShaderFeatures::bit(1);
inline constexpr ShaderFeatures SF_IMAGE=ShaderFeatures::bit(2);
inline constexpr ShaderFeatures SF_ATTR=ShaderFeatures::bit(3);
inline constexpr ShaderFeatures SF_NORMAL_MAP=ShaderFeatures::bit(4);
inline constexpr ShaderFeatures SF_TANGENT=ShaderFeatures::bit(5);
inline constexpr ShaderFeatures SF_VERTEX_COLOR=ShaderFeatures::bit(6);
inline constexpr ShaderFeatures SF_MATH=ShaderFeatures::bit(7);
inline constexpr ShaderFeatures SF_VECTOR_MATH=ShaderFeatures::bit(8);
inline constexpr ShaderFeatures SF_CLAMP=ShaderFeatures::bit(9);
inline constexpr ShaderFeatures SF_CONVERT=ShaderFeatures::bit(10);
inline constexpr ShaderFeatures SF_SEPARATE_VECTOR=ShaderFeatures::bit(11);
inline constexpr ShaderFeatures SF_COMBINE_VECTOR=ShaderFeatures::bit(12);
inline constexpr ShaderFeatures SF_CLOSURE_VOLUME=ShaderFeatures::bit(13);
inline constexpr ShaderFeatures SF_CLOSURE_HOLDOUT=ShaderFeatures::bit(14);
inline constexpr ShaderFeatures SF_CLOSURE_SET_NORMAL=ShaderFeatures::bit(15);
inline constexpr ShaderFeatures SF_TEX_CHECKER=ShaderFeatures::bit(16);
inline constexpr ShaderFeatures SF_TEX_GRADIENT=ShaderFeatures::bit(17);
inline constexpr ShaderFeatures SF_GAMMA=ShaderFeatures::bit(18);
inline constexpr ShaderFeatures SF_INVERT=ShaderFeatures::bit(19);
inline constexpr ShaderFeatures SF_BRIGHTCONTRAST=ShaderFeatures::bit(20);
inline constexpr ShaderFeatures SF_BSDF_DIFFUSE=ShaderFeatures::bit(21);
inline constexpr ShaderFeatures SF_BSDF_TRANSLUCENT=ShaderFeatures::bit(22);
inline constexpr ShaderFeatures SF_BSDF_TRANSPARENT=ShaderFeatures::bit(23);
inline constexpr ShaderFeatures SF_BSDF_GLOSSY=ShaderFeatures::bit(24);
inline constexpr ShaderFeatures SF_BSDF_GLASS=ShaderFeatures::bit(25);
inline constexpr ShaderFeatures SF_LIGHT_AREA=ShaderFeatures::bit(26);
inline constexpr ShaderFeatures SF_MOTION_OBJECT=ShaderFeatures::bit(27);
inline constexpr ShaderFeatures SF_MOTION_CAMERA=ShaderFeatures::bit(28);
// Emissive-triangle sampling and forward-hit MIS: any triangle in the light
// distribution or any shader flagged as a MIS emitter (SD_MIS_FRONT/BACK).
inline constexpr ShaderFeatures SF_LIGHT_TRIANGLE=ShaderFeatures::bit(29);
// Object shadow terminator offsets (Phase E3): the geometry offset on
// smooth-shaded triangles with direct lighting, the shading offset whenever
// an object's frequency multiplier exceeds 1.
inline constexpr ShaderFeatures SF_TERMINATOR_GEOMETRY=ShaderFeatures::bit(30);
inline constexpr ShaderFeatures SF_TERMINATOR_SHADING=ShaderFeatures::bit(31);
// Principled BSDF closure setup, its Fresnel models and the sheen closure.
inline constexpr ShaderFeatures SF_BSDF_PRINCIPLED=ShaderFeatures::bit(32);
// Principled thin-film iridescence and thin-walled transmission.
inline constexpr ShaderFeatures SF_BSDF_THIN_FILM=ShaderFeatures::bit(33);
inline constexpr ShaderFeatures SF_BSDF_THIN_WALL=ShaderFeatures::bit(34);
// Texture, colour and utility node handlers (wavefront_svm_nodes.wgsl).
inline constexpr ShaderFeatures SF_TEX_NOISE=ShaderFeatures::bit(35);
inline constexpr ShaderFeatures SF_TEX_WHITE_NOISE=ShaderFeatures::bit(36);
inline constexpr ShaderFeatures SF_TEX_WAVE=ShaderFeatures::bit(37);
inline constexpr ShaderFeatures SF_TEX_MAGIC=ShaderFeatures::bit(38);
inline constexpr ShaderFeatures SF_TEX_BRICK=ShaderFeatures::bit(39);
inline constexpr ShaderFeatures SF_MAPPING=ShaderFeatures::bit(40);
inline constexpr ShaderFeatures SF_MAP_RANGE=ShaderFeatures::bit(41);
inline constexpr ShaderFeatures SF_MIX=ShaderFeatures::bit(42);
inline constexpr ShaderFeatures SF_HSV=ShaderFeatures::bit(43);
inline constexpr ShaderFeatures SF_COLOR_SEPCOMB=ShaderFeatures::bit(44);
inline constexpr ShaderFeatures SF_RAMP=ShaderFeatures::bit(45);
inline constexpr ShaderFeatures SF_FRESNEL=ShaderFeatures::bit(46);
inline constexpr ShaderFeatures SF_VECTOR_ROTATE=ShaderFeatures::bit(47);
inline constexpr ShaderFeatures SF_BLACKBODY=ShaderFeatures::bit(48);
inline constexpr ShaderFeatures SF_NORMAL=ShaderFeatures::bit(49);
inline constexpr ShaderFeatures SF_SHADING_INFO=ShaderFeatures::bit(50);
inline constexpr ShaderFeatures SF_LIGHT_PATH=ShaderFeatures::bit(51);
inline constexpr ShaderFeatures SF_TEX_VORONOI=ShaderFeatures::bit(52);
// Wavelength and wireframe handlers (wavefront_svm_nodes.wgsl).
inline constexpr ShaderFeatures SF_WAVELENGTH=ShaderFeatures::bit(53);
inline constexpr ShaderFeatures SF_WIREFRAME=ShaderFeatures::bit(54);
// Bump stages and dual-number derivative variants (wavefront_svm_derivatives.wgsl;
// mapping variants in wavefront_svm_nodes.wgsl).
inline constexpr ShaderFeatures SF_BUMP=ShaderFeatures::bit(55);
inline constexpr ShaderFeatures SF_BUMP_STATE=ShaderFeatures::bit(56);
inline constexpr ShaderFeatures SF_GEOMETRY_DERIVATIVE=ShaderFeatures::bit(57);
inline constexpr ShaderFeatures SF_TEX_COORD_DERIVATIVE=ShaderFeatures::bit(58);
inline constexpr ShaderFeatures SF_VALUE_DERIVATIVE=ShaderFeatures::bit(59);
inline constexpr ShaderFeatures SF_VECTOR_MATH_DERIVATIVE=ShaderFeatures::bit(60);
inline constexpr ShaderFeatures SF_MAPPING_DERIVATIVE=ShaderFeatures::bit(61);
// Gabor texture (wavefront_svm_nodes.wgsl; lane L, 2026-10-22).
inline constexpr ShaderFeatures SF_TEX_GABOR=ShaderFeatures::bit(63);
// Background (world) light (lane m1-lighting, 2026-10-23): a LIGHT_BACKGROUND
// lamp is uploaded (environment importance sampling in the light stage,
// background MIS, the importance-map shader evaluation and dD clamp).
inline constexpr ShaderFeatures SF_LIGHT_BACKGROUND=ShaderFeatures::bit(64);
// A light without constant emission can be sampled (non-constant world through
// the background light, or an emissive triangle with node emission): deferral
// to the SHADE_LIGHT_NEE kernel (wavefront_light_nee.wgsl).
inline constexpr ShaderFeatures SF_LIGHT_NEE=ShaderFeatures::bit(65);
// Light tree (lane m2-lighttree, 2026-10-23): integrator.use_light_tree is set
// (tree selection in shade_surface_light_select, tree forward pdfs for triangle,
// lamp, sun and background hits; wavefront_light_tree.wgsl).
inline constexpr ShaderFeatures SF_LIGHT_TREE=ShaderFeatures::bit(94);
// KERNEL_FEATURE_LIGHT_LINKING is set (receiver light sets on both samplers and
// on forward hits; wavefront_light_link.wgsl).
inline constexpr ShaderFeatures SF_LIGHT_LINKING=ShaderFeatures::bit(95);
// The light selection entry shade_surface_light_select runs (tree or linking).
inline constexpr ShaderFeatures SF_LIGHT_SELECT=ShaderFeatures::bit(96);
// Shadow catcher (integrator.has_shadow_catcher): the closest-hit path split,
// the shifted visibility of split paths and the shadow catcher film passes
// (wavefront_shadow_catcher.wgsl). Scene-only in superset mode.
inline constexpr ShaderFeatures SF_SHADOW_CATCHER=ShaderFeatures::bit(102);
// Ambient occlusion (lane m2-ao, 2026-10-23). SVM_NODE_AO: the Ambient Occlusion
// node record (svm/ao.h) is reachable; its handler stores ao = 1 as under every
// node mask without KERNEL_FEATURE_NODE_RAYTRACE. SVM_NODE_RAYTRACE: the node's
// ray tracing (wavefront_ao.wgsl). It is a module-variant bit, never a scene
// feature: only prepare_surface_raytrace, the SHADE_SURFACE_RAYTRACE queue's
// SVM stage, is built with it (build_pipelines' extra features), so it is not in
// SF_ALL either. AO_SHADOW: the AO shadow ray of fast GI's additive mode and the
// AO pass (KERNEL_FEATURE_AO) in the surface and shadow stages.
inline constexpr ShaderFeatures SF_NODE_AO=ShaderFeatures::bit(106);
inline constexpr ShaderFeatures SF_NODE_RAYTRACE=ShaderFeatures::bit(107);
inline constexpr ShaderFeatures SF_AO_SHADOW=ShaderFeatures::bit(108);
// Manifold next-event estimation (lane m2-mnee, 2026-10-23): integrator.use_caustics
// (KERNEL_FEATURE_MNEE). The INTERSECT_MNEE entries and constants, the closest
// flags and receiver routing, the surface consumption and cleanup and the
// background culling (wavefront_mnee.wgsl and MNEE regions). Bits 111-115 are
// reserved for MNEE entry splits.
inline constexpr ShaderFeatures SF_MNEE=ShaderFeatures::bit(110);
// Every assigned scene feature (bits 0-61, 63, 64, 65, 94-96, 102, 106, 108 and 110;
// bit 62 is unassigned; 107 is the raytrace module variant).
inline constexpr ShaderFeatures SF_ALL=ShaderFeatures::range(0,62)|SF_TEX_GABOR|SF_LIGHT_BACKGROUND|SF_LIGHT_NEE|SF_LIGHT_TREE|SF_LIGHT_LINKING|SF_LIGHT_SELECT|SF_SHADOW_CATCHER|SF_NODE_AO|SF_AO_SHADOW|SF_MNEE;
inline constexpr ShaderFeatures SF_PRINCIPLED_ALL=SF_BSDF_PRINCIPLED|SF_BSDF_THIN_FILM|SF_BSDF_THIN_WALL;
// Superset specialization mode (CODEBRIX_CYCLES_SHADER_SPECIALIZATION=superset,
// desktop diagnostic): every handler is compiled in except these regions, which
// are kept only when the uploaded scene reaches them. They are the regions that
// pushed the unspecialized entry points past what Intel Mesa 25.0.7 compiles
// correctly (native/DEVICE.md, "Linux reference fixes on the updated Mesa").
inline constexpr ShaderFeatures SF_SUPERSET_SCENE_ONLY=SF_BUMP|SF_BUMP_STATE|SF_GEOMETRY_DERIVATIVE|SF_TEX_COORD_DERIVATIVE|
  SF_VALUE_DERIVATIVE|SF_VECTOR_MATH_DERIVATIVE|SF_MAPPING_DERIVATIVE|SF_TEX_GABOR|SF_SHADOW_CATCHER|SF_NODE_AO|SF_AO_SHADOW|SF_MNEE;
// Specialization modes: Scene (default, only the regions the scene reaches),
// Superset (the on/off identity check) and Off (every region; diagnostic).
//
// The superset is every region except the scene-only regions above that the
// scene does not reach. Compiled whole it exceeds the Intel Mesa 25.0.7 backend's
// 16-bit instruction/register range (twice over for prepare_surface), so it is
// compiled in groups: group g is the scene plus the g-th greedy fill of the
// remaining superset regions (ascending bit order) that keeps every entry point
// of the surface and background modules within the inlined-size budget
// (wgsl_inlined_bytes; default superset_default_budget_bytes, override with
// CODEBRIX_CYCLES_SUPERSET_BUDGET_BYTES). The groups together cover the whole
// superset except regions that do not fit next to the scene ("unfit", reported).
enum class SpecializationMode {Scene,Superset,Off};
const char *specialization_mode_name(SpecializationMode mode);
// Parses CODEBRIX_CYCLES_SHADER_SPECIALIZATION: unset or "1" Scene, "superset" or
// "superset:G" Superset group G (default 0), "0" Off; anything else throws.
SpecializationMode specialization_mode_from_environment(unsigned *superset_group=nullptr);
// Inlined size of a WGSL entry point (every call inlined, as Vulkan drivers do),
// in bytes of WGSL text without comments. Intel Mesa 25.0.7 emits about one
// backend instruction per 15-20 such bytes (measured, lane L 2026-10-22).
size_t wgsl_inlined_bytes(const std::string &source,const std::string &entry);
inline constexpr size_t superset_default_budget_bytes=1000000;
// Pipeline-build size guard (Step zero, lane L decision 4). Intel Mesa drivers
// without upstream fix b7e1ac830984 (25.0.7 on the Linux reference laptop, 24.0.8
// on the HP) keep backend instruction and register numbers in 16-bit fields and
// miscompile or crash on an entry point above 65,535 backend instructions; the
// maintained envelope keeps every gate entry under 60,000
// (native/tools/intel_compile_envelope.py). The guard estimates that count from
// the WGSL alone: wgsl_inlined_bytes of every entry point a pipeline build
// compiles. Measured on Mesa 25.0.7 (merge wave 4 envelope, surface-module
// entries above 500 KB): 16.9 to 22.1 inlined bytes per backend instruction,
// central value about 20. The default threshold
// pipeline_size_warning_default_bytes = 1,110,000 bytes (lowered from 1,200,000
// at merge wave 5) sits at 65,535 instructions at the densest measured ratio
// (65,535 x 16.9 = 1,107,542 bytes, rounded up; about 55,500 instructions at the
// central 20), so it may warn on entries that measure safe (at Step zero the node
// render's shade_background was 1,126,723 bytes, 40,103 instructions): a false
// warning is acceptable, a missed one is a driver crash. At or above the
// threshold (CODEBRIX_CYCLES_PIPELINE_SIZE_WARN_BYTES overrides, 0 disables) the build
// warns once per entry and size: on stderr before the compile, in the device
// report's "warnings" (host C ABI device/capability report, managed
// ICapabilityReport.Warnings) and in the process report. The warning never
// changes the compiled variant, the tier or the "degraded" flag (that flag
// stays first-use latency only).
inline constexpr size_t pipeline_size_warning_default_bytes=1110000;
inline constexpr double pipeline_size_bytes_per_instruction_central=20.0,
  pipeline_size_bytes_per_instruction_low=16.9,pipeline_size_bytes_per_instruction_high=22.1;
// Inlined size (wgsl_inlined_bytes) of each named entry point, parsing the module once.
std::vector<size_t> wgsl_inlined_bytes(const std::string &source,const std::vector<std::string> &entries);
// The guard threshold for this process (environment override or the default).
size_t pipeline_size_warning_bytes();
// The warning text for one entry at or above the threshold.
std::string pipeline_size_warning(const char *module,const char *entry,size_t inlined_bytes,size_t threshold);
struct SupersetGroups {std::vector<ShaderFeatures> groups;ShaderFeatures unfit;size_t scene_bytes=0;std::vector<size_t> group_bytes;};
// Greedy grouping of `candidates` next to `scene` for the given module sources.
SupersetGroups superset_groups(const std::vector<std::string> &sources,ShaderFeatures scene,ShaderFeatures candidates,size_t budget);
struct ShaderFeatureName {const char *name;ShaderFeatures bit;};
const std::vector<ShaderFeatureName> &shader_feature_names();
std::string shader_feature_list(ShaderFeatures features);

struct SvmCensus {
  ShaderFeatures features;  // Handlers any reachable decode path can execute.
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
std::string specialize_wgsl(const std::string &source,ShaderFeatures enabled);

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
