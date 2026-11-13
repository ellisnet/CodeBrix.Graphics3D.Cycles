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
// True displacement of a mesh with deformation motion (SF_EVAL_DISPLACE and an object with SD_OBJECT_HAS_VERTEX_MOTION):
// shader_setup_from_displace places the point by the mesh's static (centre) positions as for any mesh, and
// GeometryManager::displace adds the offset to every motion step. Without it, shader_eval_displace keeps its invalid-data stop.
inline constexpr ShaderFeatures SF_DISPLACE_VERTEX_MOTION=ShaderFeatures::bit(76);
// The Light Path node's path-state outputs (Backfacing, Ray Length and the depths) in a scene with transparent shadows or
// volumes, or in the shader of a lamp, background light or emissive triangle that is not constant: the depths of a shadow
// evaluation read the shadow path's counters, a volume evaluation reads every output, an emitter's emission evaluation reads
// the counters of the path it is made for (the shadow path in shade_light_nee, the main path at a lamp hit or a sun), and the
// importance map's world evaluation reads the masked values (depths 0, Ray Depth 1), as light_path.h does
// (wavefront_svm_nodes.wgsl svm_light_path). Without it those evaluations keep error 2.
inline constexpr ShaderFeatures SF_LIGHT_PATH_STATE=ShaderFeatures::bit(77);
// A Principled Volume node whose density, colour or temperature attribute has a name that is not a standard attribute
// (the host's geometry can carry it as an object- or mesh-element attribute): the attribute's value scales the input as
// in svm_node_principled_volume (wavefront_svm_closures.wgsl vp_attribute). Without it a found attribute is error 16.
inline constexpr ShaderFeatures SF_PRINCIPLED_VOLUME_ATTRIBUTE=ShaderFeatures::bit(78);
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
// Every scene with object, deformation or camera motion: the host-exact motion transforms of the MNEE_MOTION regions.
// Their last bits decide ray-triangle folds near a shared edge (native/DEVICE.md, "Motion arithmetic last bits"), and
// the colour passes divide the direct and indirect light passes, so a near-zero colour amplifies them.
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
// background sampler with MIS kept shade_surface_light at 57,567 Intel instructions, and with portals (BACKGROUND_PORTAL). A module-variant bit like
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
// Christensen-Burley subsurface scattering (range 216-219). SUBSURFACE_DISK: a Burley BSSRDF closure in the scene (the
// Subsurface Scattering node or the Principled BSDF's subsurface method): subsurface_bounce's disk branch in the surface
// stages. SUBSURFACE_DISK_ENTRY: the DEVICE_KERNEL_INTEGRATOR_INTERSECT_SUBSURFACE entry for disk paths
// (integrator/subsurface_disk.h with bvh/local.h's reservoir of hits), a module-variant bit like SUBSURFACE_ENTRY, never a
// scene feature and not in SF_ALL: subsurface.cpp builds it next to the random walk's entry.
inline constexpr ShaderFeatures SF_SUBSURFACE_DISK=ShaderFeatures::bit(216);
inline constexpr ShaderFeatures SF_SUBSURFACE_DISK_ENTRY=ShaderFeatures::bit(217);
// A thin-walled Principled BSDF with subsurface (bsdf_thin_subsurface_setup, closure/bsdf_oren_nayar.h): diffuse and
// translucent closures, or Oren-Nayar and the rough translucent closure (CLOSURE_BSDF_ROUGH_TRANSLUCENT_ID), weighted by
// the subsurface anisotropy; no BSSRDF.
inline constexpr ShaderFeatures SF_BSDF_THIN_SUBSURFACE=ShaderFeatures::bit(218);
// Subsurface scattering with volumes (range 232-237): integrator_volume_stack_update_for_subsurface after subsurface_scatter
// (integrator/intersect_volume_stack.h), the entry intersect_subsurface_volume_stack of the volume stack module and the
// record of each exit the subsurface entries leave for it. A scene feature: KERNEL_FEATURE_VOLUME with SUBSURFACE.
inline constexpr ShaderFeatures SF_SUBSURFACE_VOLUME=ShaderFeatures::bit(232);
// Subsurface scattering with a holdout object in the scene: integrate_surface skips the holdout (an object holdout too) at
// the exit point (connect_surface_light). A scene feature.
inline constexpr ShaderFeatures SF_SUBSURFACE_HOLDOUT=ShaderFeatures::bit(233);
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
// Volume direct light from spot, area and emissive-triangle lights (VOLUME_LIGHT with such a light): their samples
// from the volume segment (light_sample<true>) and valid ray segments (volume_valid_direct_ray_segment).
inline constexpr ShaderFeatures SF_VOLUME_LIGHT_SHAPES=ShaderFeatures::bit(177);
// The light tree's volume segments (light_tree_sample<true>, light_tree_pdf<true>): shade_volume_tree_select and the
// forward pdfs of volume-scattered paths.
inline constexpr ShaderFeatures SF_VOLUME_LIGHT_TREE=ShaderFeatures::bit(178);
// Light passes with volumes: the volume direct and indirect passes (film_write_direct_light, film_write_surface_emission,
// film_write_background ...) and the light passes of volume emission (film_write_volume_emission).
inline constexpr ShaderFeatures SF_VOLUME_LIGHT_PASSES=ShaderFeatures::bit(179);
// The shadow catcher with volumes: the split after a volume segment (integrator_intersect_next_kernel_after_volume), the
// volume stack re-init of the catcher path, the catcher film split of volume emission and guidance.
inline constexpr ShaderFeatures SF_VOLUME_SHADOW_CATCHER=ShaderFeatures::bit(180);
// Curves (data.bvh.have_curves; range 186-193): the static curve shapes the scene's KernelCurve records use
// (kernel/geom/curve_intersect.h ribbon_intersect, curve_intersect_recursive, linear_curve_intersect), BVH2's unaligned
// nodes, curve_shader_setup and curve attributes (wavefront_curve_intersect.wgsl, wavefront_curve_shader_data.wgsl).
inline constexpr ShaderFeatures SF_CURVE_RIBBON=ShaderFeatures::bit(186);
inline constexpr ShaderFeatures SF_CURVE_THICK=ShaderFeatures::bit(187);
inline constexpr ShaderFeatures SF_CURVE_THICK_LINEAR=ShaderFeatures::bit(188);
inline constexpr ShaderFeatures SF_CURVES=SF_CURVE_RIBBON|SF_CURVE_THICK|SF_CURVE_THICK_LINEAR;
// The Hair Info node (svm/geometry.h svm_node_hair_info), reachable on any geometry.
inline constexpr ShaderFeatures SF_NODE_HAIR_INFO=ShaderFeatures::bit(189);
// Curves B (range 194-199). Deformation motion: curve records with PRIMITIVE_MOTION, keys at ray time (geom/motion_curve.h).
inline constexpr ShaderFeatures SF_CURVE_MOTION=ShaderFeatures::bit(194);
// Curves with transparent shadows: the shadow-all traversal's curve hits (baked transparency, bvh_shadow_all_anyhit_filter)
// and the kernel that bakes it (SHADER_EVAL_CURVE_SHADOW_TRANSPARENCY).
inline constexpr ShaderFeatures SF_CURVE_SHADOW=ShaderFeatures::bit(195);
// Curves with the shader raytrace nodes or subsurface scattering: the AO, Bevel, Raycast and random walk traversals'
// unaligned nodes and curve leaves, and curve shader data from a state-free ray for the Raycast node's hits.
inline constexpr ShaderFeatures SF_CURVE_RAYTRACE=ShaderFeatures::bit(196);
// A hair object that is a shadow catcher: the continuation after the catcher's background or volume reads a curve's shader.
inline constexpr ShaderFeatures SF_CURVE_SHADOW_CATCHER=ShaderFeatures::bit(197);
// Curves with the Raycast node: curve hits of its scene traversal and their shader data (curve_setup_hit). The local traversals
// (subsurface, Bevel, only-local Raycast) pass curves over and need only CURVE_RAYTRACE.
inline constexpr ShaderFeatures SF_CURVE_RAYCAST=ShaderFeatures::bit(198);
// Point clouds (range 206-211): points in the scene (kernel/geom/point_intersect.h, kernel/geom/point.h; wavefront_point.wgsl,
// wavefront_point_shader_data.wgsl), their motion (kernel/geom/motion_point.h), the Point Info and Particle Info nodes,
// each reachable on any geometry (svm/geometry.h).
inline constexpr ShaderFeatures SF_POINTS=ShaderFeatures::bit(206);
inline constexpr ShaderFeatures SF_POINTS_MOTION=ShaderFeatures::bit(207);
inline constexpr ShaderFeatures SF_NODE_POINT_INFO=ShaderFeatures::bit(208);
inline constexpr ShaderFeatures SF_NODE_PARTICLE_INFO=ShaderFeatures::bit(209);
inline constexpr ShaderFeatures SF_POINTS_ALL=SF_POINTS|SF_POINTS_MOTION|SF_NODE_POINT_INFO|SF_NODE_PARTICLE_INFO;
// Hair closures (range 200-203). HAIR: the Hair BSDF node's reflection and transmission closures (closure/bsdf_hair.h).
// HAIR_CHIANG: the Principled Hair BSDF node's Chiang model, all three colour parametrizations
// (closure/bsdf_principled_hair_chiang.h). Bits 202 and 203 are free.
inline constexpr ShaderFeatures SF_BSDF_HAIR=ShaderFeatures::bit(200);
inline constexpr ShaderFeatures SF_BSDF_HAIR_CHIANG=ShaderFeatures::bit(201);
// The Principled Hair BSDF node's Huang model (closure/bsdf_principled_hair_huang.h, range 204-205).
inline constexpr ShaderFeatures SF_BSDF_HAIR_HUANG=ShaderFeatures::bit(204);
inline constexpr ShaderFeatures SF_VOLUME_PHASE_KINDS=SF_PHASE_FOURNIER_FORAND|SF_PHASE_RAYLEIGH|SF_PHASE_DRAINE|SF_PHASE_MIE;
// KERNEL_FEATURE_SHADOW_LINKING is set (lane range 120-127): blocker sets in the closest and shadow traversals, the
// indirect emitters the main path leaves to the dedicated light kernels, and the kernels themselves
// (wavefront_shadow_link.wgsl, shadow_linking.cpp).
inline constexpr ShaderFeatures SF_SHADOW_LINKING=ShaderFeatures::bit(120);
// Volumes with a sampling pattern other than tabulated Sobol (lane range 220-225): the volume integrators' random numbers
// (VOLUME_SAMPLING_PATTERN regions, native_rng_pattern). Taken from the uploaded KernelData at every shader_features() call,
// never kept from an earlier call: scene preparation runs on zero KernelData, which reads as Sobol-Burley
// (SAMPLING_PATTERN_SOBOL_BURLEY is 0), so tabulated Sobol scenes keep their volume text.
inline constexpr ShaderFeatures SF_VOLUME_SAMPLING_PATTERN=ShaderFeatures::bit(220);
// A volume shader with a Light Path node (lane range 220-222): the node's ray type outputs in volume evaluations, and in
// heterogeneous media the rays other than camera rays estimate such an entry's extrema from its own evaluations
// (volume_object_get_extrema, volume_estimate_extrema) in the resumable octree set-up and advance of
// shade_volume_heterogeneous and of the heterogeneous shadows (VOLUME_LIGHT_PATH).
inline constexpr ShaderFeatures SF_VOLUME_LIGHT_PATH=ShaderFeatures::bit(221);
// Volumes in a scene with BVH motion (KernelData bvh.have_motion; lane range 223-225): the volume stack initialization's
// motion traversal (moving instances and motion triangles at the ray's time) and boundary normals at that time, and the
// object motion transforms of each stack entry in the volume shader evaluations (VOLUME_MOTION regions). Taken from the
// uploaded KernelData at every shader_features() call, as SF_VOLUME_SAMPLING_PATTERN.
inline constexpr ShaderFeatures SF_VOLUME_MOTION=ShaderFeatures::bit(223);
// Background light portals (range 226-231): area lights with is_portal (integrator.num_portals > 0), stored after
// the lamps in the light table. The portal strategy of background_light_sample and background_light_pdf
// (light/background.h background_portal_sample, background_portal_pdf; wavefront_background_light.wgsl).
inline constexpr ShaderFeatures SF_BACKGROUND_PORTAL=ShaderFeatures::bit(226);
// The Ray Portal BSDF (closure/bsdf_ray_portal.h; KERNEL_FEATURE_NODE_PORTAL): its closure setup, the portal bounce of
// shade_surface (integrate_surface_ray_portal) with path_state_next's portal bounces, the shadow rays a portal blocks
// (integrate_transparent_surface_shadow), the portal bounce of the path and shadow states and the Light Path node's
// portal depth.
inline constexpr ShaderFeatures SF_RAY_PORTAL=ShaderFeatures::bit(227);
// MNEE (caustics) from spot and background lights (lane range 238-243): the pinned light_sample of the type for the
// walk's seed and light_sample_update after it (light/spot.h spot_light_sample and spot_light_mnee_sample_update,
// light/light.h's background branch with light/background.h background_light_sample), in wavefront_mnee.wgsl's MNEE_SPOT
// and MNEE_BACKGROUND regions. Set when a light of that type has use_caustics. Area lights (bit 239, MNEE_AREA): light/area.h
// area_light_sample and area_light_mnee_sample_update. The spot and area samplers and light updates run in entries of their
// own (mnee_light_sample before mnee_seed, mnee_light_update after mnee_solve; region MNEE_LIGHT_STAGES).
inline constexpr ShaderFeatures SF_MNEE_SPOT=ShaderFeatures::bit(238);
inline constexpr ShaderFeatures SF_MNEE_AREA=ShaderFeatures::bit(239);
inline constexpr ShaderFeatures SF_MNEE_BACKGROUND=ShaderFeatures::bit(240);
// MNEE in a scene with Beckmann closures: a Beckmann refraction caster's half-vector offset (-logf(1 - u)), Smith term and
// refraction Fresnel in the walk (integrator/mnee.h mnee_sample_bsdf_dh and mnee_eval_bsdf_contribution; MNEE_BECKMANN regions).
inline constexpr ShaderFeatures SF_MNEE_BECKMANN=ShaderFeatures::bit(241);
// MNEE in a scene with volumes (KERNEL_FEATURE_VOLUME): the caustic receiver's routing after shade_volume
// (integrator_intersect_next_kernel_after_volume) and the volume stack of the MNEE shadow path (MNEE_VOLUME regions).
inline constexpr ShaderFeatures SF_MNEE_VOLUME=ShaderFeatures::bit(242);
// MNEE in a scene with the Huang hair closure: the closure's evaluation entry takes an MNEE-sampled path's direction
// from its MNEE slot (MNEE_HUANG region of wavefront_hair_huang.wgsl).
inline constexpr ShaderFeatures SF_MNEE_HUANG=ShaderFeatures::bit(243);
// Cameras, film and nodes (range 247-253). LOCAL_INSTANCE_SKIP: the local traversal (bvh/local.h scene_intersect_local,
// for the Bevel node, and for the Ambient Occlusion and Raycast nodes with only_local) of a transform-applied object starts at
// the scene root, so in a scene with instances it reaches instance leaves; it passes them over, since another object cannot be
// a local hit (the pinned BVH2 triangle loop would read the leaves' negative primitive addresses; MetalRT and Embree give
// every geometry its own BVH or filter by object). Set when a raytrace node, an instance leaf in the top-level BVH and a
// transform-applied object whose shader traces locally are all in the scene; without the region the leaf still reports
// unsupported (16).
inline constexpr ShaderFeatures SF_LOCAL_INSTANCE_SKIP=ShaderFeatures::bit(247);
// CAMERA_STEREO: spherical stereo (KernelCamera interocular_offset non-zero, a left or right Stereo Eye): the eye offset of
// camera_sample_perspective and camera_sample_to_ray (kernel/camera/camera.h) with projection.h spherical_stereo_transform
// (pole merge, convergence distance), and their ray differentials. Taken from the uploaded KernelData at every
// shader_features() call; it also sets CAMERA_PROJECTIONS, whose camera_projected_ray carries it.
inline constexpr ShaderFeatures SF_CAMERA_STEREO=ShaderFeatures::bit(248);
// CAMERA_PANORAMA_INVERSE: a panoramic camera's inverse mappings (projection.h direction_to_panorama) for the Texture
// Coordinate node's Window output (camera_world_to_ndc) and the motion pass (camera_motion_vector and its direction form).
// Set with a panoramic camera and a Texture Coordinate node or motion vectors in the film.
inline constexpr ShaderFeatures SF_CAMERA_PANORAMA_INVERSE=ShaderFeatures::bit(249);
// LIGHT_MOTION: a light object with object motion (SD_OBJECT_MOTION on a PRIMITIVE_LAMP object): shader_setup_from_sample's
// object transforms at the sample's time for its emission shader. KernelLight keeps one transform, so the light itself
// does not move (as on every pinned device).
inline constexpr ShaderFeatures SF_LIGHT_MOTION=ShaderFeatures::bit(250);
// SVM_GEOMETRY_TANGENT: the Geometry node's Tangent output (geom/primitive.h primitive_tangent) on triangles, plain and
// with derivatives inside bump (census: a NODE_GEOMETRY or NODE_GEOMETRY_DERIVATIVE record with NODE_GEOM_T).
inline constexpr ShaderFeatures SF_GEOMETRY_TANGENT=ShaderFeatures::bit(251);
// SVM_MOTION_NORMAL_DERIVATIVE: the smooth normal with derivatives (the Texture Coordinate node's Normal inside bump,
// svm_node_tex_coord_derivative) on motion triangles (geom/motion_triangle.h motion_triangle_smooth_normal with the du/dv
// offsets). Set with a derivative texture coordinate in the census and a motion-triangle object.
inline constexpr ShaderFeatures SF_MOTION_NORMAL_DERIVATIVE=ShaderFeatures::bit(252);
// Lights and baking (range 244-246, 253). LIGHT_AREA_DEGENERATE: an area light whose axis length underflows to zero (light/area.h
// with a zero len_u or len_v; on the CPU the axis holds infinities and NaNs, and the device's copy holds a zero axis): its next-event
// samples are rejected, a camera ray's quad test passes on that axis, and its emission is evaluated as the pinned spread clamp gives it
// (native/DEVICE.md, item 148).
inline constexpr ShaderFeatures SF_LIGHT_AREA_DEGENERATE=ShaderFeatures::bit(244);
// BAKE_DEFORM: a bake of a mesh with deformation motion (SD_OBJECT_HAS_VERTEX_MOTION on the bake object): init_from_bake.h
// places the texel on the static positions and marks the hit a plain triangle (PRIMITIVE_TRIANGLE), so the stages read that
// mesh's plain triangles at the static positions, as the motion pass without motion blur does (DEFORMING_PLAIN_TRIANGLES
// regions; native/DEVICE.md, item 115).
inline constexpr ShaderFeatures SF_BAKE_DEFORM=ShaderFeatures::bit(245);
// LIGHT_SPOT: a spot light in the scene: its attenuation in the oracle's float3 order (wavefront_emitters.wgsl spot_attenuation;
// native/DEVICE.md, item 151).
inline constexpr ShaderFeatures SF_LIGHT_SPOT=ShaderFeatures::bit(246);
// LIGHT_LAMP_SAMPLE: a point or spot light, or a sun light with an angle: their samples in the oracle's float3 order, products
// rounded before their sums (wavefront_lighting.wgsl light_cone and light_sample_lamp, the LAMP_SAMPLE_EXACT regions, which
// volume direct light also takes; native/DEVICE.md, item 152).
inline constexpr ShaderFeatures SF_LIGHT_LAMP_SAMPLE=ShaderFeatures::bit(253);
// MNEE_WALK_STAGES (overflow bit 66): the MNEE walk (integrator/mnee.h mnee_newton_solver) in two entries, mnee_solve (the
// constraints, the Newton step and the checks after the projection) and mnee_project (the projection of every vertex onto
// its caster: the walk's ray trace and mnee_setup_manifold_vertex), dispatched in turn once per iteration with the walk's
// state in the record (wavefront_mnee.wgsl, MR_WALK). Set with MNEE_MOTION beside curves or points (the scene's, or a
// device's accumulated features), where
// the traversal inlined into one walk entry passed the compile envelope (native/DEVICE.md 126). A size-control variant like
// MNEE_LIGHT_STAGES, not in SF_ALL: Off and superset modules keep the one-entry walk, and the host dispatches what the
// module holds.
inline constexpr ShaderFeatures SF_MNEE_WALK_STAGES=ShaderFeatures::bit(66);
// Every assigned scene feature (bits 0-61, 63, 64, 65, 76-78, 94-96, 102, 106, 108, 110, 120,
// 128-160, 162-168, 170-180, 186-189, 194-198, 200, 201, 204, 206-209, 216, 218, 220, 221, 223, 226, 227, 232, 233, 238-243, 244-253; bit 62 is unassigned; 107, 161, 169 and 217 are module variants).
inline constexpr ShaderFeatures SF_ALL=SF_BSDF_HAIR|SF_BSDF_HAIR_CHIANG|SF_POINTS_ALL|ShaderFeatures::range(0,62)|SF_TEX_GABOR|SF_LIGHT_BACKGROUND|SF_LIGHT_NEE|SF_LIGHT_TREE|SF_LIGHT_LINKING|SF_LIGHT_SELECT|SF_SHADOW_CATCHER|SF_NODE_AO|SF_AO_SHADOW|SF_MNEE|
  SF_BSDF_METALLIC|SF_BSDF_BECKMANN|SF_BSDF_ASHIKHMIN_SHIRLEY|SF_BSDF_VELVET|SF_BSDF_TOON|SF_NODE_BEVEL|SF_NODE_RAYCAST|SF_NODE_RADIAL_TILING|SF_FILTER_GLOSSY|SF_TERMINATOR_WIDE|SF_SHADOW_MOTION|SF_MNEE_MOTION|SF_NODE_DISPLACEMENT|SF_EVAL_DISPLACE|SF_BUMP_UNDISPLACED|SF_FILM_DATA_PASSES|SF_NODE_AOV|SF_FILM_CRYPTOMATTE|SF_FILM_DENOISING|SF_FILM_MOTION|SF_FILM_MOTION_EXACT|
  SF_TEX_ENVIRONMENT|SF_TEX_IMAGE_BOX|SF_SAMPLING_PATTERN|SF_CAMERA_PROJECTIONS|SF_LIGHT_FALLOFF|SF_LIGHT_LAMP_SHADER|SF_NODE_IES|SF_BSDF_THIN_FILM_CONDUCTOR|SF_TEX_SKY|SF_BACKGROUND_SUN|SF_TEX_SKY_SCATTERING|SF_MATH_ARC|SF_VOLUME_SHADOW|SF_VOLUME_LIGHT|
  SF_VOLUME_PHASES|SF_VOLUME_PHASE_KINDS|SF_VOLUME_HETEROGENEOUS|SF_VOLUME_RAY_MARCHING|SF_VOLUME_LIGHT_SHAPES|SF_VOLUME_LIGHT_TREE|SF_VOLUME_LIGHT_PASSES|SF_VOLUME_SHADOW_CATCHER|SF_NODE_VOLUME_COEFFICIENTS|SF_NODE_PRINCIPLED_VOLUME|SF_VOLUME_EMISSION|SF_SUBSURFACE|SF_FILM_MOTION_DEFORM|SF_SHADOW_LINKING|SF_CURVES|SF_NODE_HAIR_INFO|SF_CURVE_MOTION|SF_CURVE_SHADOW|SF_CURVE_RAYTRACE|SF_CURVE_SHADOW_CATCHER|SF_CURVE_RAYCAST|SF_BSDF_HAIR_HUANG|SF_VOLUME_SAMPLING_PATTERN|SF_VOLUME_LIGHT_PATH|SF_VOLUME_MOTION|SF_BACKGROUND_PORTAL|SF_RAY_PORTAL|SF_SUBSURFACE_DISK|SF_BSDF_THIN_SUBSURFACE|SF_MNEE_SPOT|SF_MNEE_AREA|SF_MNEE_BACKGROUND|SF_MNEE_BECKMANN|SF_MNEE_VOLUME|SF_MNEE_HUANG|SF_SUBSURFACE_VOLUME|SF_SUBSURFACE_HOLDOUT|SF_LOCAL_INSTANCE_SKIP|SF_CAMERA_STEREO|SF_CAMERA_PANORAMA_INVERSE|SF_LIGHT_MOTION|SF_GEOMETRY_TANGENT|SF_MOTION_NORMAL_DERIVATIVE|SF_LIGHT_AREA_DEGENERATE|SF_BAKE_DEFORM|SF_LIGHT_SPOT|SF_LIGHT_LAMP_SAMPLE|SF_DISPLACE_VERTEX_MOTION|SF_LIGHT_PATH_STATE|SF_PRINCIPLED_VOLUME_ATTRIBUTE;
// The families above, for regions shared by all of them (closure classification).
inline constexpr ShaderFeatures SF_BSDF_FAMILIES=SF_BSDF_METALLIC|SF_BSDF_BECKMANN|SF_BSDF_ASHIKHMIN_SHIRLEY|SF_BSDF_VELVET|SF_BSDF_TOON|SF_BSDF_HAIR|SF_BSDF_HAIR_CHIANG|SF_BSDF_HAIR_HUANG;
inline constexpr ShaderFeatures SF_PRINCIPLED_ALL=SF_BSDF_PRINCIPLED|SF_BSDF_THIN_FILM|SF_BSDF_THIN_WALL;
// Superset specialization mode (CODEBRIX_CYCLES_SHADER_SPECIALIZATION=superset,
// desktop diagnostic): every handler is compiled in except these regions, which
// are kept only when the uploaded scene reaches them. They are the regions that
// pushed the unspecialized entry points past what Intel Mesa 25.0.7 compiles
// correctly (native/DEVICE.md, "Linux reference fixes on the updated Mesa").
inline constexpr ShaderFeatures SF_SUPERSET_SCENE_ONLY=SF_POINTS_ALL|SF_BUMP|SF_BUMP_STATE|SF_GEOMETRY_DERIVATIVE|SF_TEX_COORD_DERIVATIVE|
  SF_VALUE_DERIVATIVE|SF_VECTOR_MATH_DERIVATIVE|SF_MAPPING_DERIVATIVE|SF_TEX_GABOR|SF_SHADOW_CATCHER|SF_NODE_AO|SF_AO_SHADOW|SF_MNEE|SF_BSDF_FAMILIES|
  SF_NODE_BEVEL|SF_NODE_RAYCAST|SF_NODE_RADIAL_TILING|SF_FILTER_GLOSSY|SF_TERMINATOR_WIDE|SF_SHADOW_MOTION|SF_MNEE_MOTION|SF_NODE_DISPLACEMENT|SF_EVAL_DISPLACE|SF_BUMP_UNDISPLACED|SF_FILM_DATA_PASSES|SF_NODE_AOV|SF_FILM_CRYPTOMATTE|SF_FILM_DENOISING|SF_FILM_MOTION|SF_FILM_MOTION_EXACT|
  SF_TEX_ENVIRONMENT|SF_TEX_IMAGE_BOX|SF_SAMPLING_PATTERN|SF_CAMERA_PROJECTIONS|SF_LIGHT_FALLOFF|SF_LIGHT_LAMP_SHADER|SF_NODE_IES|SF_BSDF_THIN_FILM_CONDUCTOR|SF_TEX_SKY|SF_BACKGROUND_SUN|SF_TEX_SKY_SCATTERING|SF_MATH_ARC|
  SF_VOLUME_SHADOW|SF_VOLUME_LIGHT|SF_VOLUME_LIGHT_SHAPES|SF_VOLUME_LIGHT_TREE|SF_VOLUME_LIGHT_PASSES|SF_VOLUME_SHADOW_CATCHER|SF_VOLUME_PHASES|SF_VOLUME_PHASE_KINDS|SF_NODE_VOLUME_COEFFICIENTS|SF_NODE_PRINCIPLED_VOLUME|SF_VOLUME_EMISSION|SF_SUBSURFACE|SF_FILM_MOTION_DEFORM|SF_SHADOW_LINKING|SF_CURVES|SF_NODE_HAIR_INFO|SF_CURVE_MOTION|SF_CURVE_SHADOW|SF_CURVE_RAYTRACE|SF_CURVE_SHADOW_CATCHER|SF_CURVE_RAYCAST|SF_BSDF_HAIR_HUANG|SF_VOLUME_SAMPLING_PATTERN|SF_VOLUME_LIGHT_PATH|SF_VOLUME_MOTION|SF_BACKGROUND_PORTAL|SF_RAY_PORTAL|SF_SUBSURFACE_DISK|SF_BSDF_THIN_SUBSURFACE|SF_MNEE_SPOT|SF_MNEE_AREA|SF_MNEE_BACKGROUND|SF_MNEE_BECKMANN|SF_MNEE_VOLUME|SF_MNEE_HUANG|SF_SUBSURFACE_VOLUME|SF_SUBSURFACE_HOLDOUT|SF_LOCAL_INSTANCE_SKIP|SF_CAMERA_STEREO|SF_CAMERA_PANORAMA_INVERSE|SF_LIGHT_MOTION|SF_GEOMETRY_TANGENT|SF_MOTION_NORMAL_DERIVATIVE|SF_LIGHT_AREA_DEGENERATE|SF_BAKE_DEFORM|SF_LIGHT_SPOT|SF_LIGHT_LAMP_SAMPLE|SF_DISPLACE_VERTEX_MOTION|SF_LIGHT_PATH_STATE|SF_PRINCIPLED_VOLUME_ATTRIBUTE;
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
// compiles. Measured on Mesa 25.0.7 over the oracle corpus and the superset
// groups (684 distinct entries; every entry above 100,000 bytes): 14.4 inlined
// bytes per backend instruction at the densest (intersect_subsurface of the
// subsurface scenes, 799,982 bytes, 55,539 instructions), 14.4 to 24.8 among the
// entries above 40,000 instructions, central value about 20 (median 20.3 above
// 500,000 bytes). Entries below 100,000 bytes (film filters and small helpers,
// densest 9.0) are not extrapolated: they say nothing about an entry near the
// limit. The default threshold pipeline_size_warning_default_bytes = 940,000
// bytes sits at 65,535 instructions at the densest measured ratio (65,535 x
// 14.404 = 943,987 bytes, rounded down; about 47,000 instructions at the central
// 20), so it may warn on entries that measure safe (the corpus' prepare_surface
// of displacement/both is 1,006,721 bytes, 42,348 instructions): a false
// warning is acceptable, a missed one is a driver crash. At or above the
// threshold (CODEBRIX_CYCLES_PIPELINE_SIZE_WARN_BYTES overrides, 0 disables) the build
// warns once per entry and size: on stderr before the compile, in the device
// report's "warnings" (host C ABI device/capability report, managed
// ICapabilityReport.Warnings) and in the process report. The warning never
// changes the compiled variant, the tier or the "degraded" flag (that flag
// stays first-use latency only).
inline constexpr size_t pipeline_size_warning_default_bytes=940000;
inline constexpr double pipeline_size_bytes_per_instruction_central=20.0,
  pipeline_size_bytes_per_instruction_low=14.4,pipeline_size_bytes_per_instruction_high=24.8;
// Inlined size (wgsl_inlined_bytes) of each named entry point, parsing the module once.
std::vector<size_t> wgsl_inlined_bytes(const std::string &source,const std::vector<std::string> &entries);
// Inlined copies of one function (call-tree paths) in each named entry point (native/DEVICE.md 107: build_pipelines
// refuses an entry with more than one copy of portal_sampler_function, before the driver compiles it).
std::vector<size_t> wgsl_inlined_copies(const std::string &source,const std::vector<std::string> &entries,const std::string &callee);
inline constexpr const char *portal_sampler_function="bgl_portal_light_sample";
std::string portal_sampler_copies_error(const char *module,const char *entry,size_t copies);
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
  // A Bevel node, or an Ambient Occlusion or Raycast node with only_local: a local traversal (bvh/local.h).
  bool local_traversal=false;
  uint64_t stopped_paths=0; // Paths ending where the GPU interpreter reports an error.
  // First reachable feature the device rejects before rendering (empty: none).
  // Principled subsurface is refused here when a constant enables it or its
  // weight is linked; the GPU handler also reports it (error 16) and never
  // approximates it.
  std::string unsupported;
  // Word offsets of the reachable Raycast node records (svm_canonical_padding).
  std::vector<uint32_t> raycast_records;
};
// Mirrors the WGSL interpreter's decode exactly: every program start reachable
// through a shader header, shader jump or conditional jump, record sizes from
// the native SVM node layouts, BSDF data records and texture-coordinate
// transforms. Paths stop where the GPU would stop (END, unknown opcode/closure,
// or a bounds error); the result is a superset of executable handlers.
// `roots` (optional) limits the program starts to these shader indices.
SvmCensus svm_census(const std::vector<uint32_t> &words,size_t shader_count,const std::vector<uint32_t> *roots=nullptr,bool displacement=false);
// The SVM program as uploaded, with the padding byte of every reachable SVMNodeRaycast record cleared: the scene compiler
// builds the record on the stack, so that byte is whatever the stack held (native/DEVICE.md 55); no kernel reads it.
void svm_canonical_padding(std::vector<uint32_t> &words);

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
