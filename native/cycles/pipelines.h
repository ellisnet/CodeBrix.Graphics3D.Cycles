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
// The seven INTERSECT_MNEE entry points (mnee_receiver to mnee_contribute). A module-variant bit like
// SVM_NODE_RAYTRACE, never a scene feature and not in SF_ALL: only the MNEE module (mnee.cpp, built with the caustic
// casters' SVM regions only) carries them, so no other surface module holds an entry the device never builds there.
inline constexpr ShaderFeatures SF_MNEE_ENTRIES=ShaderFeatures::bit(111);
// Diagnostic debug output (CODEBRIX_CYCLES_DEBUG_OUTPUT, off by default): the
// first camera hit writes one SurfaceData field to the Combined film instead of
// radiance (wavefront_surface.wgsl, DEBUG_OUTPUT region). A module-variant bit
// like SVM_NODE_RAYTRACE, never a scene feature and not in SF_ALL, so every
// module compiles byte-identically unless the variable is set.
inline constexpr ShaderFeatures SF_DEBUG_OUTPUT=ShaderFeatures::bit(116);
// The debug output mode (0: off; 1 shading-normal, 2 geometric-normal,
// 3 svm-normal, 4 uv, 5 flags), from CODEBRIX_CYCLES_DEBUG_OUTPUT; an unknown
// name throws. A set mode is noted as an active diagnostic.
unsigned debug_output_mode();
// Driver workaround (docs/TESTING-WINDOWS-X64.md, W1): on the Intel Direct3D 12
// driver the first compile of a pipeline that walks the light tree is correct
// and the driver's cached compile of the same DXIL is not. The DRIVER_CACHE_SALT
// region (wavefront_light_tree.wgsl) compares the walk's receiver with a constant
// that no object index reaches, different for every compile, so those pipelines
// never come from the driver's cache; the images are unchanged. A module-variant
// bit like SF_DEBUG_OUTPUT, not in SF_ALL; build_pipelines sets it on Intel
// Direct3D 12 adapters unless CODEBRIX_CYCLES_DRIVER_WORKAROUNDS=none (a
// diagnostic, to test a new driver) is set.
inline constexpr ShaderFeatures SF_DRIVER_CACHE_SALT=ShaderFeatures::bit(117);
// Closures and surface nodes of the remaining SVM BSDF and raytrace node
// families (bits 128 onwards). METALLIC: the Metallic BSDF node (physical
// conductor and F82 tint Fresnel, svm/closure.h CLOSURE_BSDF_PHYSICAL_CONDUCTOR
// and CLOSURE_BSDF_F82_CONDUCTOR); the closures it makes are GGX or Beckmann
// microfacet records with the conductor (model 3) or F82 tint (model 5) Fresnel.
inline constexpr ShaderFeatures SF_BSDF_METALLIC=ShaderFeatures::bit(128);
// BECKMANN: the Beckmann distribution (Glossy, Anisotropic, Refraction, Glass and
// Metallic BSDF nodes): CLOSURE_BSDF_MICROFACET_BECKMANN_ID, _REFRACTION_ID, _GLASS_ID.
inline constexpr ShaderFeatures SF_BSDF_BECKMANN=ShaderFeatures::bit(129);
// ASHIKHMIN_SHIRLEY: the Glossy BSDF node's Ashikhmin-Shirley distribution.
// VELVET: the Sheen BSDF node's Ashikhmin velvet distribution. TOON: the Toon
// BSDF node (diffuse and glossy components).
inline constexpr ShaderFeatures SF_BSDF_ASHIKHMIN_SHIRLEY=ShaderFeatures::bit(130);
inline constexpr ShaderFeatures SF_BSDF_VELVET=ShaderFeatures::bit(131);
inline constexpr ShaderFeatures SF_BSDF_TOON=ShaderFeatures::bit(132);
// BEVEL and RAYCAST: the Bevel and Raycast shader nodes (svm/bevel.h, svm/raycast.h):
// their records everywhere, their ray tracing only in prepare_surface_raytrace
// (SVM_NODE_RAYTRACE, the SHADE_SURFACE_RAYTRACE queue of the AO node).
inline constexpr ShaderFeatures SF_NODE_BEVEL=ShaderFeatures::bit(133);
inline constexpr ShaderFeatures SF_NODE_RAYCAST=ShaderFeatures::bit(134);
// RADIAL_TILING: the Radial Tiling node (svm/radial_tiling.h, radial_tiling_shared.h).
inline constexpr ShaderFeatures SF_NODE_RADIAL_TILING=ShaderFeatures::bit(135);
// Integrator glossy filtering (filter_glossy != FLT_MAX; surface_shader_prepare_closures' bsdf_blur).
inline constexpr ShaderFeatures SF_FILTER_GLOSSY=ShaderFeatures::bit(136);
// Shadow terminator shading offsets above 1 (kernel multiplier above 2, infinite or negative).
inline constexpr ShaderFeatures SF_TERMINATOR_WIDE=ShaderFeatures::bit(137);
// Transparent shadows in scenes with motion blur (object or deformation motion): the all-hits traversal
// at the shadow ray's time.
inline constexpr ShaderFeatures SF_SHADOW_MOTION=ShaderFeatures::bit(138);
// MNEE (caustics) in scenes with motion blur: caster vertices of deforming objects from the centre frame,
// as the pinned mnee_setup_manifold_vertex fetches them.
inline constexpr ShaderFeatures SF_MNEE_MOTION=ShaderFeatures::bit(139);
// Displacement nodes (svm/displace.h: NODE_SET_DISPLACEMENT, NODE_DISPLACEMENT, NODE_VECTOR_DISPLACEMENT), in a bump
// or a displacement program.
inline constexpr ShaderFeatures SF_NODE_DISPLACEMENT=ShaderFeatures::bit(140);
// True displacement (a referenced shader with SD_HAS_DISPLACEMENT): DEVICE_KERNEL_SHADER_EVAL_DISPLACE (the
// shader_eval_displace entry and the displacement program's jump).
inline constexpr ShaderFeatures SF_EVAL_DISPLACE=ShaderFeatures::bit(141);
// Bump from displacement over displaced geometry (displacement method Both): enter_bump_eval's undisplaced
// position and normal.
inline constexpr ShaderFeatures SF_BUMP_UNDISPLACED=ShaderFeatures::bit(142);
// Film data passes (film_passes.cpp, wavefront_film_passes.wgsl): a data pass the
// surface stage's film_write_data_passes writes is enabled (depth, position,
// normal, roughness, UV, object and material index, the diffuse, glossy and
// transmission colour passes, mist): the shade_surface_passes entry.
inline constexpr ShaderFeatures SF_FILM_DATA_PASSES=ShaderFeatures::bit(143);
// AOV output nodes (svm/aov.h: NODE_AOV_START, NODE_AOV_COLOR, NODE_AOV_VALUE) are reachable:
// their film writes in the surface and background shader evaluations.
inline constexpr ShaderFeatures SF_NODE_AOV=ShaderFeatures::bit(144);
// Cryptomatte (film.cryptomatte_passes): the id/weight slot writes of film_write_data_passes.
inline constexpr ShaderFeatures SF_FILM_CRYPTOMATTE=ShaderFeatures::bit(145);
// Denoising features (KERNEL_FEATURE_DENOISING, film/denoising_passes.h): the camera's
// feature flag and throughput, the surface features in shade_surface_passes, the
// albedo of emission and background, the background depth.
inline constexpr ShaderFeatures SF_FILM_DENOISING=ShaderFeatures::bit(146);
// Motion pass (Scene::MOTION_PASS): the surface stage's object and camera motion
// vectors and the background's camera motion vector.
inline constexpr ShaderFeatures SF_FILM_MOTION=ShaderFeatures::bit(147);
// Colour passes in a scene with motion blur: the host-exact motion transforms of the
// MNEE_MOTION regions (the colour passes divide the direct and indirect light passes,
// so last-bit differences of a near-zero colour are amplified by the pass accessor).
inline constexpr ShaderFeatures SF_FILM_MOTION_EXACT=ShaderFeatures::bit(148);
// Image texture projections that only some image nodes reach (compiler pressure: the
// shared svm_image handler otherwise inlines every projection into each SVM entry).
// TEX_ENVIRONMENT: the Environment Texture node's projections (NODE_TEX_ENVIRONMENT:
// equirectangular and mirror ball). TEX_IMAGE_BOX: the Image Texture node's box
// projection (NODE_TEX_IMAGE_BOX, three blended lookups).
inline constexpr ShaderFeatures SF_TEX_ENVIRONMENT=ShaderFeatures::bit(149);
inline constexpr ShaderFeatures SF_TEX_IMAGE_BOX=ShaderFeatures::bit(150);
// A sampling pattern other than tabulated Sobol (integrator.sampling_pattern: Sobol-Burley or a
// blue-noise pattern; kernel/sample/pattern.h): every random number of the path (native_rng_pattern)
// and the blue-noise pixel seed (wavefront_common.wgsl, wavefront_camera.wgsl).
inline constexpr ShaderFeatures SF_SAMPLING_PATTERN=ShaderFeatures::bit(151);
// Every camera but the pinhole perspective camera (kernel/camera/camera.h): orthographic and panoramic
// cameras and depth of field (wavefront_camera.wgsl camera_projected_ray), and the panoramic depth pass.
inline constexpr ShaderFeatures SF_CAMERA_PROJECTIONS=ShaderFeatures::bit(152);
// The Light Falloff shader node (svm/light_path.h svm_node_light_falloff).
inline constexpr ShaderFeatures SF_LIGHT_FALLOFF=ShaderFeatures::bit(153);
// A point, spot, area or sun light whose emission shader is not constant (with LIGHT_NEE): the shader evaluated
// at the light point for next-event estimation and for hits (light/light.h light_normal_uv_from_position).
inline constexpr ShaderFeatures SF_LIGHT_LAMP_SHADER=ShaderFeatures::bit(154);
// The IES Texture node (svm/ies.h, util/ies.h kernel_ies_interp).
inline constexpr ShaderFeatures SF_NODE_IES=ShaderFeatures::bit(155);
// A Metallic BSDF thin film over the physical conductor Fresnel (with BSDF_THIN_FILM): the polarized
// conductor reflectance at the film's bottom interface (closure/bsdf_util.h fresnel_conductor_polarized
// without the F82 fit, fresnel_iridescence_channel<true>).
inline constexpr ShaderFeatures SF_BSDF_THIN_FILM_CONDUCTOR=ShaderFeatures::bit(156);
// The Sky Texture node's analytic models (svm/sky.h: Preetham, Hosek-Wilkie).
inline constexpr ShaderFeatures SF_TEX_SKY=ShaderFeatures::bit(157);
// A scattering Sky Texture node with the sun disc (with SVM_TEX_SKY): background light sun guiding
// (light/background.h background_sun_sample / _pdf, the sun strategy of background_light_sample / _pdf).
inline constexpr ShaderFeatures SF_BACKGROUND_SUN=ShaderFeatures::bit(158);
// The Sky Texture node's single and multiple scattering models (their precomputed sky image and sun disc).
inline constexpr ShaderFeatures SF_TEX_SKY_SCATTERING=ShaderFeatures::bit(159);
// The Math node's arcsine and arccosine (the correctly rounded trig_arc adapter; census of the Math node type).
inline constexpr ShaderFeatures SF_MATH_ARC=ShaderFeatures::bit(160);
// The background light's sample in its own surface entry (compiler pressure): shade_surface_light leaves a
// sampled background light pending and shade_surface_light_background, dispatched next, runs bgl_sample with the
// same inputs (wavefront_surface.wgsl). Set for scenes with sun guiding (BACKGROUND_SUN), whose two-strategy
// background sampler with MIS kept shade_surface_light at 57,567 Intel instructions. A module-variant bit like
// SVM_NODE_RAYTRACE, never a scene feature and not in SF_ALL: surface.cpp builds the light entries with it.
inline constexpr ShaderFeatures SF_BACKGROUND_SAMPLE_ENTRY=ShaderFeatures::bit(161);
// Shader-defined volumes with shadow paths (KERNEL_FEATURE_VOLUME): the shadow path carries the volume stack
// (integrator_state_copy_volume_stack_to_shadow) and the shadow stage attenuates through the homogeneous media
// between the recorded boundary hits (integrate_transparent_volume_shadow, volume_shadow_null_scattering).
inline constexpr ShaderFeatures SF_VOLUME_SHADOW=ShaderFeatures::bit(162);
// Volume direct light (volumes with integrator.use_direct_light): the light segment entry, distance, equiangular
// and MIS direct scatter sampling in shade_volume and the volume light entry (integrate_volume_direct_light).
inline constexpr ShaderFeatures SF_VOLUME_LIGHT=ShaderFeatures::bit(163);
// Volume phase functions other than Henyey-Greenstein (census of the Volume Scatter and Volume Coefficients nodes'
// closure types): Fournier-Forand, Rayleigh, Draine and Mie (closure/volume_*.h).
inline constexpr ShaderFeatures SF_VOLUME_PHASES=ShaderFeatures::bit(164);
// The Volume Coefficients and Principled Volume nodes (svm/closure.h svm_node_volume_coefficients,
// svm_node_principled_volume).
inline constexpr ShaderFeatures SF_NODE_VOLUME_COEFFICIENTS=ShaderFeatures::bit(165);
inline constexpr ShaderFeatures SF_NODE_PRINCIPLED_VOLUME=ShaderFeatures::bit(166);
// Volume emission (volume_emission_integrate, film_write_volume_emission): volumes whose programs can emit.
inline constexpr ShaderFeatures SF_VOLUME_EMISSION=ShaderFeatures::bit(167);
// Subsurface scattering (KERNEL_FEATURE_SUBSURFACE): the BSSRDF closures of the Principled BSDF and the Subsurface
// Scattering node (closure/bssrdf.h bssrdf_alloc, bssrdf_setup), their pick and subsurface_bounce in the surface
// stages, and the diffuse closure at the exit point (subsurface_shader_data_setup).
inline constexpr ShaderFeatures SF_SUBSURFACE=ShaderFeatures::bit(168);
// The DEVICE_KERNEL_INTEGRATOR_INTERSECT_SUBSURFACE entry (subsurface_random_walk, subsurface_scatter) in the surface
// module. A module-variant bit like SVM_NODE_RAYTRACE, never a scene feature and not in SF_ALL: subsurface.cpp builds
// the entry with it.
inline constexpr ShaderFeatures SF_SUBSURFACE_ENTRY=ShaderFeatures::bit(169);
// The motion pass (or a denoising motion feature) with deforming geometry: without motion blur a deforming mesh keeps
// plain triangles with SD_OBJECT_HAS_VERTEX_MOTION and its motion steps after the centre positions
// (primitive_motion_data_without_camera's deformation motion).
inline constexpr ShaderFeatures SF_FILM_MOTION_DEFORM=ShaderFeatures::bit(170);
// The phase function families beyond Henyey-Greenstein, one bit each (census of the closure types, alongside
// VOLUME_PHASES): a module keeps only the lobes its scenes use (compile envelope). Mie sets the Draine bit too (its
// fit is a Henyey-Greenstein and a Draine lobe).
inline constexpr ShaderFeatures SF_PHASE_FOURNIER_FORAND=ShaderFeatures::bit(171);
inline constexpr ShaderFeatures SF_PHASE_RAYLEIGH=ShaderFeatures::bit(172);
inline constexpr ShaderFeatures SF_PHASE_DRAINE=ShaderFeatures::bit(173);
inline constexpr ShaderFeatures SF_PHASE_MIE=ShaderFeatures::bit(174);
// Heterogeneous volumes (a volume shader with SD_HETEROGENEOUS_VOLUME): the density prepass's 16 Sobol-Burley samples
// per voxel (kernel_volume_density_evaluate), the majorant octree tracer, null scattering and the heterogeneous shadows.
inline constexpr ShaderFeatures SF_VOLUME_HETEROGENEOUS=ShaderFeatures::bit(175);
// Biased volume ray marching (KernelIntegrator::volume_ray_marching): the shade_volume_ray_marching entry
// (DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME_RAY_MARCHING) and the ray-marched shadows (volume_shadow_ray_marching); set
// together with VOLUME_HETEROGENEOUS, whose shader evaluation and phase records it shares.
inline constexpr ShaderFeatures SF_VOLUME_RAY_MARCHING=ShaderFeatures::bit(176);
inline constexpr ShaderFeatures SF_VOLUME_PHASE_KINDS=SF_PHASE_FOURNIER_FORAND|SF_PHASE_RAYLEIGH|SF_PHASE_DRAINE|SF_PHASE_MIE;
// Every assigned scene feature (bits 0-61, 63, 64, 65, 94-96, 102, 106, 108, 110,
// 128-160, 162-168, 170-176; bit 62 is unassigned; 107, 161 and 169 are module variants).
inline constexpr ShaderFeatures SF_ALL=ShaderFeatures::range(0,62)|SF_TEX_GABOR|SF_LIGHT_BACKGROUND|SF_LIGHT_NEE|SF_LIGHT_TREE|SF_LIGHT_LINKING|SF_LIGHT_SELECT|SF_SHADOW_CATCHER|SF_NODE_AO|SF_AO_SHADOW|SF_MNEE|
  SF_BSDF_METALLIC|SF_BSDF_BECKMANN|SF_BSDF_ASHIKHMIN_SHIRLEY|SF_BSDF_VELVET|SF_BSDF_TOON|SF_NODE_BEVEL|SF_NODE_RAYCAST|SF_NODE_RADIAL_TILING|SF_FILTER_GLOSSY|SF_TERMINATOR_WIDE|SF_SHADOW_MOTION|SF_MNEE_MOTION|SF_NODE_DISPLACEMENT|SF_EVAL_DISPLACE|SF_BUMP_UNDISPLACED|SF_FILM_DATA_PASSES|SF_NODE_AOV|SF_FILM_CRYPTOMATTE|SF_FILM_DENOISING|SF_FILM_MOTION|SF_FILM_MOTION_EXACT|
  SF_TEX_ENVIRONMENT|SF_TEX_IMAGE_BOX|SF_SAMPLING_PATTERN|SF_CAMERA_PROJECTIONS|SF_LIGHT_FALLOFF|SF_LIGHT_LAMP_SHADER|SF_NODE_IES|SF_BSDF_THIN_FILM_CONDUCTOR|SF_TEX_SKY|SF_BACKGROUND_SUN|SF_TEX_SKY_SCATTERING|SF_MATH_ARC|SF_VOLUME_SHADOW|SF_VOLUME_LIGHT|
  SF_VOLUME_PHASES|SF_VOLUME_PHASE_KINDS|SF_VOLUME_HETEROGENEOUS|SF_VOLUME_RAY_MARCHING|SF_NODE_VOLUME_COEFFICIENTS|SF_NODE_PRINCIPLED_VOLUME|SF_VOLUME_EMISSION|SF_SUBSURFACE|SF_FILM_MOTION_DEFORM;
// The families above, for regions shared by all of them (closure classification).
inline constexpr ShaderFeatures SF_BSDF_FAMILIES=SF_BSDF_METALLIC|SF_BSDF_BECKMANN|SF_BSDF_ASHIKHMIN_SHIRLEY|SF_BSDF_VELVET|SF_BSDF_TOON;
inline constexpr ShaderFeatures SF_PRINCIPLED_ALL=SF_BSDF_PRINCIPLED|SF_BSDF_THIN_FILM|SF_BSDF_THIN_WALL;
// Superset specialization mode (CODEBRIX_CYCLES_SHADER_SPECIALIZATION=superset,
// desktop diagnostic): every handler is compiled in except these regions, which
// are kept only when the uploaded scene reaches them. They are the regions that
// pushed the unspecialized entry points past what Intel Mesa 25.0.7 compiles
// correctly (native/DEVICE.md, "Linux reference fixes on the updated Mesa").
inline constexpr ShaderFeatures SF_SUPERSET_SCENE_ONLY=SF_BUMP|SF_BUMP_STATE|SF_GEOMETRY_DERIVATIVE|SF_TEX_COORD_DERIVATIVE|
  SF_VALUE_DERIVATIVE|SF_VECTOR_MATH_DERIVATIVE|SF_MAPPING_DERIVATIVE|SF_TEX_GABOR|SF_SHADOW_CATCHER|SF_NODE_AO|SF_AO_SHADOW|SF_MNEE|SF_BSDF_FAMILIES|
  SF_NODE_BEVEL|SF_NODE_RAYCAST|SF_NODE_RADIAL_TILING|SF_FILTER_GLOSSY|SF_TERMINATOR_WIDE|SF_SHADOW_MOTION|SF_MNEE_MOTION|SF_NODE_DISPLACEMENT|SF_EVAL_DISPLACE|SF_BUMP_UNDISPLACED|SF_FILM_DATA_PASSES|SF_NODE_AOV|SF_FILM_CRYPTOMATTE|SF_FILM_DENOISING|SF_FILM_MOTION|SF_FILM_MOTION_EXACT|
  SF_TEX_ENVIRONMENT|SF_TEX_IMAGE_BOX|SF_SAMPLING_PATTERN|SF_CAMERA_PROJECTIONS|SF_LIGHT_FALLOFF|SF_LIGHT_LAMP_SHADER|SF_NODE_IES|SF_BSDF_THIN_FILM_CONDUCTOR|SF_TEX_SKY|SF_BACKGROUND_SUN|SF_TEX_SKY_SCATTERING|SF_MATH_ARC|
  SF_VOLUME_SHADOW|SF_VOLUME_LIGHT|SF_VOLUME_PHASES|SF_VOLUME_PHASE_KINDS|SF_NODE_VOLUME_COEFFICIENTS|SF_NODE_PRINCIPLED_VOLUME|SF_VOLUME_EMISSION|SF_SUBSURFACE|SF_FILM_MOTION_DEFORM;
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
SvmCensus svm_census(const std::vector<uint32_t> &words,size_t shader_count,const std::vector<uint32_t> *roots=nullptr,bool displacement=false);

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
