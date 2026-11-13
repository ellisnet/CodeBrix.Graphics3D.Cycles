// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "pipelines.h"
#include "kernel/svm/types.h"
#include "kernel/svm/node_types.h"
#include "kernel/types.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <random>
#include <cstring>
#include <cstdlib>
#include <mutex>
#include <sstream>
#include <cctype>
#include <functional>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace ccl {
const std::vector<ShaderFeatureName> &shader_feature_names(){
  static const std::vector<ShaderFeatureName> names={
    {"SVM_NODE_GEOMETRY",SF_GEOMETRY},{"SVM_NODE_TEX_COORD",SF_TEX_COORD},{"SVM_NODE_IMAGE",SF_IMAGE},
    {"SVM_TEX_ENVIRONMENT",SF_TEX_ENVIRONMENT},{"SVM_TEX_IMAGE_BOX",SF_TEX_IMAGE_BOX},{"SAMPLING_SOBOL_BURLEY",SF_SAMPLING_PATTERN|SF_VOLUME_HETEROGENEOUS},{"VOLUME_HETEROGENEOUS",SF_VOLUME_HETEROGENEOUS},{"VOLUME_RAY_MARCHING",SF_VOLUME_RAY_MARCHING},{"VOLUME_LIGHT_SHAPES",SF_VOLUME_LIGHT_SHAPES},{"VOLUME_LIGHT_TREE",SF_VOLUME_LIGHT_TREE},{"VOLUME_LIGHT_PASSES",SF_VOLUME_LIGHT_PASSES},{"VOLUME_SHADOW_CATCHER",SF_VOLUME_SHADOW_CATCHER},{"CAMERA_PROJECTIONS",SF_CAMERA_PROJECTIONS},{"SVM_NODE_LIGHT_FALLOFF",SF_LIGHT_FALLOFF},{"LIGHT_LAMP_SHADER",SF_LIGHT_LAMP_SHADER},{"SVM_NODE_IES",SF_NODE_IES},{"SVM_BSDF_THIN_FILM_CONDUCTOR",SF_BSDF_THIN_FILM_CONDUCTOR},{"SVM_TEX_SKY_ANALYTIC",SF_TEX_SKY},{"SVM_TEX_SKY_SCATTERING",SF_TEX_SKY_SCATTERING},{"SVM_TEX_SKY",SF_TEX_SKY|SF_TEX_SKY_SCATTERING},{"BACKGROUND_SUN",SF_BACKGROUND_SUN},{"SVM_MATH_ARC",SF_MATH_ARC},
    // The guarded arctangent (wavefront_trig.wgsl): the equirectangular lookups of environment textures and the
    // background light, the lamp and IES coordinates.
    {"TRIG_ATAN2_GUARD",SF_TEX_ENVIRONMENT|SF_LIGHT_BACKGROUND|SF_LIGHT_LAMP_SHADER|SF_NODE_IES|SF_TEX_SKY|SF_TEX_SKY_SCATTERING|SF_BSDF_HAIR_CHIANG|SF_BSDF_HAIR_HUANG|SF_CAMERA_PANORAMA_INVERSE},
    {"SVM_NODE_ATTR",SF_ATTR},{"SVM_NODE_NORMAL_MAP",SF_NORMAL_MAP},{"SVM_NODE_TANGENT",SF_TANGENT},
    {"SVM_NODE_VERTEX_COLOR",SF_VERTEX_COLOR},{"SVM_NODE_MATH",SF_MATH},{"SVM_NODE_VECTOR_MATH",SF_VECTOR_MATH},
    {"SVM_NODE_CLAMP",SF_CLAMP},{"SVM_NODE_CONVERT",SF_CONVERT},{"SVM_NODE_SEPARATE_VECTOR",SF_SEPARATE_VECTOR},
    {"SVM_NODE_COMBINE_VECTOR",SF_COMBINE_VECTOR},{"SVM_NODE_CLOSURE_VOLUME",SF_CLOSURE_VOLUME},
    {"SVM_NODE_CLOSURE_HOLDOUT",SF_CLOSURE_HOLDOUT},{"SVM_NODE_CLOSURE_SET_NORMAL",SF_CLOSURE_SET_NORMAL},
    {"SVM_NODE_TEX_CHECKER",SF_TEX_CHECKER},{"SVM_NODE_TEX_GRADIENT",SF_TEX_GRADIENT},{"SVM_NODE_GAMMA",SF_GAMMA},
    {"SVM_NODE_INVERT",SF_INVERT},{"SVM_NODE_BRIGHTCONTRAST",SF_BRIGHTCONTRAST},
    {"SVM_BSDF_DIFFUSE",SF_BSDF_DIFFUSE},{"SVM_BSDF_TRANSLUCENT",SF_BSDF_TRANSLUCENT},
    {"SVM_BSDF_TRANSPARENT",SF_BSDF_TRANSPARENT},{"SVM_BSDF_GLOSSY",SF_BSDF_GLOSSY},{"SVM_BSDF_GLASS",SF_BSDF_GLASS},
    {"LIGHT_AREA",SF_LIGHT_AREA},{"MOTION_OBJECT",SF_MOTION_OBJECT},{"MOTION_CAMERA",SF_MOTION_CAMERA},
    {"LIGHT_TRIANGLE",SF_LIGHT_TRIANGLE},{"LIGHT_BACKGROUND",SF_LIGHT_BACKGROUND},{"LIGHT_NEE",SF_LIGHT_NEE},
    {"LIGHT_TREE",SF_LIGHT_TREE},{"LIGHT_LINKING",SF_LIGHT_LINKING},{"LIGHT_SELECT",SF_LIGHT_SELECT},
    {"SHADOW_CATCHER",SF_SHADOW_CATCHER},{"SHADOW_LINKING",SF_SHADOW_LINKING},{"MNEE",SF_MNEE},{"MNEE_ENTRIES",SF_MNEE_ENTRIES},{"BACKGROUND_SAMPLE_ENTRY",SF_BACKGROUND_SAMPLE_ENTRY},{"VOLUME_SHADOW",SF_VOLUME_SHADOW},{"VOLUME_LIGHT",SF_VOLUME_LIGHT},{"VOLUME_PHASES",SF_VOLUME_PHASES},{"PHASE_FOURNIER_FORAND",SF_PHASE_FOURNIER_FORAND},{"PHASE_RAYLEIGH",SF_PHASE_RAYLEIGH},{"PHASE_DRAINE",SF_PHASE_DRAINE},{"PHASE_MIE",SF_PHASE_MIE},{"VL_RECORD",SF_VOLUME_LIGHT|SF_VOLUME_PHASES|SF_VOLUME_HETEROGENEOUS},{"PHASE_STAGE",SF_VOLUME_PHASES|SF_VOLUME_HETEROGENEOUS},{"VL_SAMPLING",SF_VOLUME_LIGHT|SF_VOLUME_HETEROGENEOUS},{"SVM_NODE_VOLUME_COEFFICIENTS",SF_NODE_VOLUME_COEFFICIENTS},{"SVM_NODE_PRINCIPLED_VOLUME",SF_NODE_PRINCIPLED_VOLUME},{"VOLUME_EMISSION",SF_VOLUME_EMISSION},{"SUBSURFACE",SF_SUBSURFACE},{"FILM_MOTION_DEFORM",SF_FILM_MOTION_DEFORM},{"SUBSURFACE_ENTRY",SF_SUBSURFACE_ENTRY},{"PHASE_HG",SF_VOLUME_LIGHT|SF_SUBSURFACE_ENTRY},{"VP_MATH",SF_VOLUME_PHASES|SF_SUBSURFACE|SF_SUBSURFACE_ENTRY|SF_VOLUME_LIGHT|SF_BSDF_HAIR_CHIANG},{"VP_ALLOC",SF_VOLUME_PHASES|SF_NODE_VOLUME_COEFFICIENTS|SF_NODE_PRINCIPLED_VOLUME},{"RAYTRACE_ANY",SF_NODE_RAYTRACE|SF_SUBSURFACE_ENTRY|SF_SUBSURFACE_DISK_ENTRY},{"RAYTRACE_LOCAL",SF_NODE_BEVEL|SF_NODE_RAYCAST|SF_SUBSURFACE_ENTRY|SF_SUBSURFACE_DISK_ENTRY},{"DEBUG_OUTPUT",SF_DEBUG_OUTPUT},{"DRIVER_CACHE_SALT",SF_DRIVER_CACHE_SALT},
    {"SHADOW_TERMINATOR_GEOMETRY",SF_TERMINATOR_GEOMETRY},{"SHADOW_TERMINATOR_SHADING",SF_TERMINATOR_SHADING},
    {"SVM_NODE_TEX_NOISE",SF_TEX_NOISE},{"SVM_NODE_TEX_WHITE_NOISE",SF_TEX_WHITE_NOISE},{"SVM_NODE_TEX_WAVE",SF_TEX_WAVE},
    {"SVM_NODE_TEX_MAGIC",SF_TEX_MAGIC},{"SVM_NODE_TEX_BRICK",SF_TEX_BRICK},{"SVM_NODE_MAPPING",SF_MAPPING},
    {"SVM_NODE_MAP_RANGE",SF_MAP_RANGE},{"SVM_NODE_MIX",SF_MIX},{"SVM_NODE_HSV",SF_HSV},{"SVM_NODE_COLOR_SEPCOMB",SF_COLOR_SEPCOMB},
    {"SVM_NODE_RAMP",SF_RAMP},{"SVM_NODE_FRESNEL",SF_FRESNEL},{"SVM_NODE_VECTOR_ROTATE",SF_VECTOR_ROTATE},
    {"SVM_NODE_BLACKBODY",SF_BLACKBODY},{"SVM_NODE_NORMAL",SF_NORMAL},{"SVM_NODE_SHADING_INFO",SF_SHADING_INFO},{"SVM_NODE_LIGHT_PATH",SF_LIGHT_PATH},
    {"SVM_NODE_TEX_VORONOI",SF_TEX_VORONOI},{"SVM_NODE_WAVELENGTH",SF_WAVELENGTH},{"SVM_NODE_WIREFRAME",SF_WIREFRAME},
    {"SVM_NODE_TEX_GABOR",SF_TEX_GABOR},{"SVM_NODE_AO",SF_NODE_AO},{"AO_ANY",SF_NODE_AO|SF_AO_SHADOW},{"SVM_NODE_RAYTRACE",SF_NODE_RAYTRACE},{"AO_SHADOW",SF_AO_SHADOW},
    {"SVM_NODE_BUMP",SF_BUMP},{"SVM_NODE_BUMP_STATE",SF_BUMP_STATE},{"SVM_NODE_GEOMETRY_DERIVATIVE",SF_GEOMETRY_DERIVATIVE},
    {"SVM_NODE_TEX_COORD_DERIVATIVE",SF_TEX_COORD_DERIVATIVE},{"SVM_NODE_VALUE_DERIVATIVE",SF_VALUE_DERIVATIVE},
    {"SVM_NODE_VECTOR_MATH_DERIVATIVE",SF_VECTOR_MATH_DERIVATIVE},{"SVM_NODE_MAPPING_DERIVATIVE",SF_MAPPING_DERIVATIVE},
    // Dual-number helpers shared by the derivative variants (wavefront_svm_derivatives.wgsl).
    {"SVM_DERIVATIVE_ANY",SF_GEOMETRY_DERIVATIVE|SF_TEX_COORD_DERIVATIVE|SF_VALUE_DERIVATIVE|SF_VECTOR_MATH_DERIVATIVE|SF_MAPPING_DERIVATIVE},
    // Helper regions shared by several node families (wavefront_svm_nodes.wgsl).
    {"SVM_NODES_ANY",SF_LIGHT_FALLOFF|SF_NODE_IES|SF_TEX_SKY|SF_TEX_SKY_SCATTERING|SF_TEX_NOISE|SF_TEX_WHITE_NOISE|SF_TEX_WAVE|SF_TEX_MAGIC|SF_TEX_BRICK|SF_MAPPING|SF_MAP_RANGE|SF_MIX|SF_HSV|SF_COLOR_SEPCOMB|SF_RAMP|
      SF_FRESNEL|SF_VECTOR_ROTATE|SF_BLACKBODY|SF_NORMAL|SF_SHADING_INFO|SF_LIGHT_PATH|SF_TEX_VORONOI|SF_WAVELENGTH|SF_WIREFRAME|SF_MAPPING_DERIVATIVE|SF_TEX_GABOR},
    {"SVM_NODES_DOT",SF_TEX_SKY|SF_TEX_SKY_SCATTERING|SF_MAPPING|SF_FRESNEL|SF_VECTOR_ROTATE|SF_BLACKBODY|SF_NORMAL|SF_SHADING_INFO|SF_WAVELENGTH|SF_WIREFRAME|SF_MAPPING_DERIVATIVE},
    {"SVM_NODES_HASH",SF_TEX_NOISE|SF_TEX_WHITE_NOISE|SF_TEX_WAVE|SF_TEX_BRICK|SF_TEX_VORONOI|SF_TEX_GABOR},
    {"SVM_NODES_FMOD",SF_TEX_NOISE|SF_TEX_WAVE|SF_TEX_MAGIC},{"SVM_NODES_NOISE",SF_TEX_NOISE|SF_TEX_WAVE},
    {"SVM_NODES_TRANSFORM",SF_MAPPING|SF_VECTOR_ROTATE|SF_MAPPING_DERIVATIVE},{"SVM_NODES_HSV",SF_MIX|SF_HSV|SF_COLOR_SEPCOMB},
    // Region kept when any GGX microfacet family is reachable (the Metallic BSDF makes GGX records).
    {"SVM_BSDF_MICROFACET",SF_BSDF_GLOSSY|SF_BSDF_GLASS|SF_BSDF_PRINCIPLED|SF_BSDF_METALLIC},
    {"SVM_BSDF_PRINCIPLED",SF_BSDF_PRINCIPLED},{"SVM_BSDF_THIN_FILM",SF_BSDF_THIN_FILM},{"SVM_BSDF_THIN_WALL",SF_BSDF_THIN_WALL},
    // Remaining SVM BSDF families (pipelines.h, bits 128 onwards); FAMILIES keeps the shared
    // closure classification, FRESNEL_MODELS the microfacet Fresnel models beyond the Glass node's.
    {"SVM_BSDF_METALLIC",SF_BSDF_METALLIC},{"SVM_BSDF_BECKMANN",SF_BSDF_BECKMANN},{"SVM_BSDF_ASHIKHMIN_SHIRLEY",SF_BSDF_ASHIKHMIN_SHIRLEY},
    {"SVM_BSDF_VELVET",SF_BSDF_VELVET},{"SVM_BSDF_TOON",SF_BSDF_TOON},{"SVM_BSDF_FAMILIES",SF_BSDF_FAMILIES},
    {"SVM_BSDF_FRESNEL_MODELS",SF_BSDF_PRINCIPLED|SF_BSDF_METALLIC},
    // The precise rotate_around_axis (Principled, Metallic) and its use for the Glossy BSDF node's tangent
    // in modules with Beckmann or Ashikhmin-Shirley closures.
    {"SVM_BSDF_ROTATE",SF_BSDF_PRINCIPLED|SF_BSDF_METALLIC|SF_BSDF_BECKMANN|SF_BSDF_ASHIKHMIN_SHIRLEY},
    {"SVM_BSDF_ROTATION",SF_BSDF_BECKMANN|SF_BSDF_ASHIKHMIN_SHIRLEY},
    // Bevel and Raycast nodes; RAYTRACE_NODES their shared traversal (local and scene).
    {"SVM_NODE_BEVEL",SF_NODE_BEVEL},{"SVM_NODE_RAYCAST",SF_NODE_RAYCAST},{"SVM_RAYTRACE_NODES",SF_NODE_BEVEL|SF_NODE_RAYCAST},
    // The SHADE_SURFACE_RAYTRACE queue (surf_queue): shaders with an AO, Bevel or Raycast node.
    {"SVM_RAYTRACE_QUEUE",SF_NODE_AO|SF_NODE_BEVEL|SF_NODE_RAYCAST},
    {"SURFACE_FILTER_GLOSSY",SF_FILTER_GLOSSY},{"SHADOW_TERMINATOR_WIDE",SF_TERMINATOR_WIDE},{"SHADOW_TRANSPARENT_MOTION",SF_SHADOW_MOTION},{"MNEE_MOTION",SF_MNEE_MOTION|SF_FILM_MOTION_EXACT},{"MNEE_SPOT",SF_MNEE_SPOT},{"MNEE_AREA",SF_MNEE_AREA},{"MNEE_LIGHT_STAGES",SF_MNEE_SPOT|SF_MNEE_AREA},{"MNEE_BACKGROUND",SF_MNEE_BACKGROUND},{"MNEE_BECKMANN",SF_MNEE_BECKMANN},{"MNEE_VOLUME",SF_MNEE_VOLUME},{"MNEE_HUANG",SF_MNEE_HUANG},{"DATA_PASS_BOOKKEEPING",SF_SHADOW_CATCHER|SF_VOLUME_SHADOW},{"SVM_NODE_DISPLACEMENT",SF_NODE_DISPLACEMENT},{"SHADER_EVAL_DISPLACE",SF_EVAL_DISPLACE},{"SVM_BUMP_UNDISPLACED",SF_BUMP_UNDISPLACED},{"SVM_NODE_RADIAL_TILING",SF_NODE_RADIAL_TILING},
    {"FILM_DATA_PASSES",SF_FILM_DATA_PASSES},{"SVM_NODE_AOV",SF_NODE_AOV},{"FILM_CRYPTOMATTE",SF_FILM_CRYPTOMATTE},{"FILM_DENOISING",SF_FILM_DENOISING},{"FILM_MOTION",SF_FILM_MOTION},
    // The shade_surface_passes entry and its shared helpers (data passes or denoising features).
    {"FILM_PASSES_ANY",SF_FILM_DATA_PASSES|SF_FILM_DENOISING},
    {"CURVE_RIBBON",SF_CURVE_RIBBON},{"CURVE_THICK",SF_CURVE_THICK},{"CURVE_THICK_LINEAR",SF_CURVE_THICK_LINEAR},{"CURVES",SF_CURVES},{"SVM_NODE_HAIR_INFO",SF_NODE_HAIR_INFO},{"CURVE_MOTION",SF_CURVE_MOTION},{"CURVE_SHADOW",SF_CURVE_SHADOW},{"CURVE_RAYTRACE",SF_CURVE_RAYTRACE},{"CURVE_SHADOW_CATCHER",SF_CURVE_SHADOW_CATCHER},{"CURVE_RAYCAST",SF_CURVE_RAYCAST},
    {"POINTS",SF_POINTS},{"POINTS_MOTION",SF_POINTS_MOTION},{"SVM_NODE_POINT_INFO",SF_NODE_POINT_INFO},{"SVM_NODE_PARTICLE_INFO",SF_NODE_PARTICLE_INFO},{"CURVES_OR_POINTS",SF_CURVES|SF_POINTS},
    // SVM_BSDF_HAIR_PRINCIPLED: what the Principled Hair BSDF node's models share in svm/closure.h (its record, colour parametrizations).
    {"SVM_BSDF_HAIR",SF_BSDF_HAIR},{"SVM_BSDF_HAIR_CHIANG",SF_BSDF_HAIR_CHIANG},{"SVM_BSDF_HAIR_PRINCIPLED",SF_BSDF_HAIR_CHIANG},
    // The Principled Hair BSDF node's Huang model, with its own copy of what the node's models share.
    {"SVM_BSDF_HAIR_HUANG",SF_BSDF_HAIR_HUANG},
    // The surface module's text of the traversal helpers: the MNEE traversal and the curve hits of the raytrace traversals.
    {"TRAVERSAL_HELPERS",SF_MNEE|SF_CURVE_RAYTRACE},
    // The volume integrators' random numbers with a sampling pattern other than tabulated Sobol (pipelines.h).
    {"VOLUME_SAMPLING_PATTERN",SF_VOLUME_SAMPLING_PATTERN},
    // The Light Path extrema estimate of heterogeneous media (pipelines.h).
    {"VOLUME_LIGHT_PATH",SF_VOLUME_LIGHT_PATH},
    {"VOLUME_MOTION",SF_VOLUME_MOTION},
    // Background light portals (wavefront_background_light.wgsl).
    {"BACKGROUND_PORTAL",SF_BACKGROUND_PORTAL},
    // The Ray Portal BSDF and the portal bounces (wavefront_surface.wgsl, wavefront_svm_closures.wgsl).
    {"RAY_PORTAL",SF_RAY_PORTAL},
    // Christensen-Burley subsurface scattering; SUBSURFACE_ENTRIES: what the random walk's and the disk's entries share.
    {"SUBSURFACE_DISK",SF_SUBSURFACE_DISK},{"SUBSURFACE_DISK_ENTRY",SF_SUBSURFACE_DISK_ENTRY},{"SUBSURFACE_ENTRIES",SF_SUBSURFACE_ENTRY|SF_SUBSURFACE_DISK_ENTRY},
    // A thin-walled Principled BSDF with subsurface (bsdf_thin_subsurface_setup).
    {"SVM_BSDF_THIN_SUBSURFACE",SF_BSDF_THIN_SUBSURFACE},
    // Subsurface scattering with volumes: the volume stack update of subsurface_scatter.
    {"SUBSURFACE_VOLUME",SF_SUBSURFACE_VOLUME},
    // Subsurface scattering with holdout objects: no holdout at the exit point.
    {"SUBSURFACE_HOLDOUT",SF_SUBSURFACE_HOLDOUT},
    // The local traversal of a transform-applied object passes instance leaves over (wavefront_ao.wgsl).
    {"LOCAL_INSTANCE_SKIP",SF_LOCAL_INSTANCE_SKIP},
    // Spherical stereo cameras (wavefront_camera.wgsl cp_stereo).
    {"CAMERA_STEREO",SF_CAMERA_STEREO},
    // The panoramic cameras' inverse mappings (wavefront_svm.wgsl cpi_direction_to_panorama).
    {"CAMERA_PANORAMA_INVERSE",SF_CAMERA_PANORAMA_INVERSE},
    // A moving light object's emission shader transforms (wavefront_emitters.wgsl lamp_shader_data).
    {"LIGHT_MOTION",SF_LIGHT_MOTION},
    // The Geometry node's Tangent output on triangles (wavefront_normals.wgsl svm_geometry_tangent).
    {"SVM_GEOMETRY_TANGENT",SF_GEOMETRY_TANGENT},
    // Smooth-normal derivatives on motion triangles (wavefront_svm_derivatives.wgsl svmd_smooth_normal).
    {"SVM_MOTION_NORMAL_DERIVATIVE",SF_MOTION_NORMAL_DERIVATIVE},
    // An area light with a zero axis length (wavefront_emitters.wgsl area_evaluate, wavefront_closest.wgsl intersect_point_lights).
    {"LIGHT_AREA_DEGENERATE",SF_LIGHT_AREA_DEGENERATE},
    // The bake of a deforming mesh (BAKE_DEFORM); a deforming mesh's plain triangles at the static positions, for the motion
    // pass without motion blur and for that bake (wavefront_motion_triangle.wgsl native_triangle_vertices).
    {"BAKE_DEFORM",SF_BAKE_DEFORM},{"DEFORMING_PLAIN_TRIANGLES",SF_FILM_MOTION_DEFORM|SF_BAKE_DEFORM},
    // A spot light: its attenuation in the oracle's float3 order (wavefront_emitters.wgsl spot_attenuation).
    {"LIGHT_SPOT",SF_LIGHT_SPOT},
    // Lamp samples in the oracle's float3 order: with volume direct light or a point, spot or angled sun light (native/DEVICE.md,
    // item 152).
    {"LAMP_SAMPLE_EXACT",SF_VOLUME_LIGHT|SF_LIGHT_LAMP_SAMPLE},
    // The MNEE walk in two entries, mnee_solve and mnee_project (wavefront_mnee.wgsl).
    {"MNEE_WALK_STAGES",SF_MNEE_WALK_STAGES},
    // True displacement of a deforming mesh (wavefront_surface.wgsl shader_eval_displace).
    {"DISPLACE_VERTEX_MOTION",SF_DISPLACE_VERTEX_MOTION},
    // The Light Path node's path-state outputs in shadow and volume evaluations (wavefront_svm_nodes.wgsl svm_light_path).
    {"LIGHT_PATH_STATE",SF_LIGHT_PATH_STATE},
    // A Principled Volume attribute with a non-standard name (wavefront_svm_closures.wgsl vp_attribute).
    {"PRINCIPLED_VOLUME_ATTRIBUTE",SF_PRINCIPLED_VOLUME_ATTRIBUTE}};
  return names;
}
std::string shader_feature_list(ShaderFeatures features){
  std::string out;
  for(auto &n:shader_feature_names())if(n.bit.count()==1&&(features&n.bit)){if(!out.empty())out+=',';out+=n.name;}
  return out;
}

// Mask layout invariants (pipelines.h): assigned bits keep their numbers.
static_assert(SF_GEOMETRY.test(0)&&SF_MAPPING_DERIVATIVE.test(61)&&SF_TEX_GABOR.test(63)&&!SF_ALL.test(62));
static_assert(SF_SHADOW_LINKING.test(120));
static_assert(SF_BACKGROUND_PORTAL.test(226)&&(SF_SUPERSET_SCENE_ONLY&SF_BACKGROUND_PORTAL)==SF_BACKGROUND_PORTAL);
static_assert(SF_RAY_PORTAL.test(227)&&(SF_SUPERSET_SCENE_ONLY&SF_RAY_PORTAL)==SF_RAY_PORTAL);
static_assert(SF_CURVE_RIBBON.test(186)&&SF_CURVE_THICK.test(187)&&SF_CURVE_THICK_LINEAR.test(188)&&SF_NODE_HAIR_INFO.test(189)&&(SF_SUPERSET_SCENE_ONLY&(SF_CURVES|SF_NODE_HAIR_INFO))==(SF_CURVES|SF_NODE_HAIR_INFO));
static_assert(SF_CURVE_MOTION.test(194)&&SF_CURVE_SHADOW.test(195)&&SF_CURVE_RAYTRACE.test(196)&&SF_CURVE_SHADOW_CATCHER.test(197)&&SF_CURVE_RAYCAST.test(198)&&(SF_SUPERSET_SCENE_ONLY&(SF_CURVE_MOTION|SF_CURVE_SHADOW|SF_CURVE_RAYTRACE|SF_CURVE_SHADOW_CATCHER|SF_CURVE_RAYCAST))==(SF_CURVE_MOTION|SF_CURVE_SHADOW|SF_CURVE_RAYTRACE|SF_CURVE_SHADOW_CATCHER|SF_CURVE_RAYCAST));
static_assert(SF_POINTS.test(206)&&SF_POINTS_MOTION.test(207)&&SF_NODE_POINT_INFO.test(208)&&SF_NODE_PARTICLE_INFO.test(209)&&(SF_SUPERSET_SCENE_ONLY&SF_POINTS_ALL)==SF_POINTS_ALL);
static_assert(SF_BSDF_HAIR.test(200)&&SF_BSDF_HAIR_CHIANG.test(201)&&(SF_SUPERSET_SCENE_ONLY&(SF_BSDF_HAIR|SF_BSDF_HAIR_CHIANG))==(SF_BSDF_HAIR|SF_BSDF_HAIR_CHIANG));
static_assert(SF_ALL.count()==168&&!(SF_ALL&~(SF_BACKGROUND_PORTAL|SF_RAY_PORTAL|SF_BSDF_HAIR|SF_BSDF_HAIR_CHIANG|SF_POINTS_ALL|SF_CURVES|SF_NODE_HAIR_INFO|SF_CURVE_MOTION|SF_CURVE_SHADOW|SF_CURVE_RAYTRACE|SF_CURVE_SHADOW_CATCHER|SF_CURVE_RAYCAST|ShaderFeatures::range(0,66)|ShaderFeatures::range(94,97)|SF_SHADOW_LINKING|SF_SHADOW_CATCHER|SF_NODE_AO|SF_AO_SHADOW|SF_MNEE|SF_BSDF_FAMILIES|SF_NODE_BEVEL|SF_NODE_RAYCAST|SF_NODE_RADIAL_TILING|SF_FILTER_GLOSSY|SF_TERMINATOR_WIDE|SF_SHADOW_MOTION|SF_MNEE_MOTION|SF_NODE_DISPLACEMENT|SF_EVAL_DISPLACE|SF_BUMP_UNDISPLACED|SF_FILM_DATA_PASSES|SF_NODE_AOV|SF_FILM_CRYPTOMATTE|SF_FILM_DENOISING|SF_FILM_MOTION|SF_FILM_MOTION_EXACT|SF_TEX_ENVIRONMENT|SF_TEX_IMAGE_BOX|SF_SAMPLING_PATTERN|SF_CAMERA_PROJECTIONS|SF_LIGHT_FALLOFF|SF_LIGHT_LAMP_SHADER|SF_NODE_IES|SF_BSDF_THIN_FILM_CONDUCTOR|SF_TEX_SKY|SF_BACKGROUND_SUN|SF_TEX_SKY_SCATTERING|SF_MATH_ARC|SF_VOLUME_SHADOW|SF_VOLUME_LIGHT|SF_VOLUME_PHASES|SF_NODE_VOLUME_COEFFICIENTS|SF_NODE_PRINCIPLED_VOLUME|SF_VOLUME_EMISSION|SF_SUBSURFACE|SF_FILM_MOTION_DEFORM|SF_VOLUME_PHASE_KINDS|SF_VOLUME_HETEROGENEOUS|SF_VOLUME_RAY_MARCHING|SF_VOLUME_LIGHT_SHAPES|SF_VOLUME_LIGHT_TREE|SF_VOLUME_LIGHT_PASSES|SF_VOLUME_SHADOW_CATCHER|SF_VOLUME_SAMPLING_PATTERN|SF_VOLUME_LIGHT_PATH|SF_VOLUME_MOTION|SF_SUBSURFACE_DISK|SF_BSDF_THIN_SUBSURFACE|SF_MNEE_SPOT|SF_MNEE_AREA|SF_MNEE_BACKGROUND|SF_MNEE_BECKMANN|SF_MNEE_VOLUME|SF_MNEE_HUANG|SF_SUBSURFACE_VOLUME|SF_SUBSURFACE_HOLDOUT|SF_LOCAL_INSTANCE_SKIP|SF_CAMERA_STEREO|SF_CAMERA_PANORAMA_INVERSE|SF_LIGHT_MOTION|SF_GEOMETRY_TANGENT|SF_MOTION_NORMAL_DERIVATIVE|SF_LIGHT_AREA_DEGENERATE|SF_BAKE_DEFORM|SF_LIGHT_SPOT|SF_LIGHT_LAMP_SAMPLE|SF_DISPLACE_VERTEX_MOTION|SF_LIGHT_PATH_STATE|SF_PRINCIPLED_VOLUME_ATTRIBUTE)).any()&&SF_LIGHT_BACKGROUND.test(64)&&SF_LIGHT_NEE.test(65)&&
  SF_LIGHT_TREE.test(94)&&SF_LIGHT_LINKING.test(95)&&SF_LIGHT_SELECT.test(96)&&SF_SHADOW_CATCHER.test(102)&&SF_MNEE.test(110));
static_assert(SF_NODE_AO.test(106)&&SF_NODE_RAYTRACE.test(107)&&SF_AO_SHADOW.test(108)&&!(SF_ALL&SF_NODE_RAYTRACE).any());
static_assert(SF_MNEE_ENTRIES.test(111)&&!(SF_ALL&SF_MNEE_ENTRIES).any());
static_assert(SF_BACKGROUND_SAMPLE_ENTRY.test(161)&&!(SF_ALL&SF_BACKGROUND_SAMPLE_ENTRY).any());
static_assert(SF_VOLUME_SHADOW.test(162)&&SF_VOLUME_LIGHT.test(163)&&SF_VOLUME_PHASES.test(164)&&SF_NODE_VOLUME_COEFFICIENTS.test(165)&&SF_NODE_PRINCIPLED_VOLUME.test(166)&&SF_VOLUME_EMISSION.test(167));
static_assert(SF_SUBSURFACE.test(168)&&SF_SUBSURFACE_ENTRY.test(169)&&!(SF_ALL&SF_SUBSURFACE_ENTRY).any()&&SF_FILM_MOTION_DEFORM.test(170));
static_assert(SF_SUBSURFACE_DISK.test(216)&&SF_SUBSURFACE_DISK_ENTRY.test(217)&&!(SF_ALL&SF_SUBSURFACE_DISK_ENTRY).any()&&(SF_SUPERSET_SCENE_ONLY&SF_SUBSURFACE_DISK).any());
static_assert(SF_BSDF_THIN_SUBSURFACE.test(218)&&(SF_SUPERSET_SCENE_ONLY&SF_BSDF_THIN_SUBSURFACE).any());
static_assert(SF_SUBSURFACE_VOLUME.test(232)&&(SF_SUPERSET_SCENE_ONLY&SF_SUBSURFACE_VOLUME).any());
static_assert(SF_SUBSURFACE_HOLDOUT.test(233)&&(SF_SUPERSET_SCENE_ONLY&SF_SUBSURFACE_HOLDOUT).any());
static_assert(SF_LOCAL_INSTANCE_SKIP.test(247)&&(SF_SUPERSET_SCENE_ONLY&SF_LOCAL_INSTANCE_SKIP).any());
static_assert(SF_CAMERA_STEREO.test(248)&&(SF_SUPERSET_SCENE_ONLY&SF_CAMERA_STEREO).any());
static_assert(SF_CAMERA_PANORAMA_INVERSE.test(249)&&(SF_SUPERSET_SCENE_ONLY&SF_CAMERA_PANORAMA_INVERSE).any());
static_assert(SF_LIGHT_MOTION.test(250)&&(SF_SUPERSET_SCENE_ONLY&SF_LIGHT_MOTION).any());
static_assert(SF_GEOMETRY_TANGENT.test(251)&&(SF_SUPERSET_SCENE_ONLY&SF_GEOMETRY_TANGENT).any());
static_assert(SF_MOTION_NORMAL_DERIVATIVE.test(252)&&(SF_SUPERSET_SCENE_ONLY&SF_MOTION_NORMAL_DERIVATIVE).any());
static_assert(SF_LIGHT_AREA_DEGENERATE.test(244)&&(SF_SUPERSET_SCENE_ONLY&SF_LIGHT_AREA_DEGENERATE).any());
static_assert(SF_BAKE_DEFORM.test(245)&&(SF_SUPERSET_SCENE_ONLY&SF_BAKE_DEFORM).any());
static_assert(SF_LIGHT_SPOT.test(246)&&(SF_SUPERSET_SCENE_ONLY&SF_LIGHT_SPOT).any());
static_assert(SF_LIGHT_LAMP_SAMPLE.test(253)&&(SF_SUPERSET_SCENE_ONLY&SF_LIGHT_LAMP_SAMPLE).any());
static_assert(SF_PHASE_FOURNIER_FORAND.test(171)&&SF_PHASE_RAYLEIGH.test(172)&&SF_PHASE_DRAINE.test(173)&&SF_PHASE_MIE.test(174)&&SF_VOLUME_HETEROGENEOUS.test(175)&&SF_VOLUME_RAY_MARCHING.test(176)&&SF_VOLUME_LIGHT_SHAPES.test(177)&&SF_VOLUME_LIGHT_TREE.test(178)&&SF_VOLUME_LIGHT_PASSES.test(179)&&SF_VOLUME_SHADOW_CATCHER.test(180));
static_assert(ShaderFeatures::bit(64).words[1]==1&&ShaderFeatures::bit(shader_feature_bits-1).test(shader_feature_bits-1));
static_assert(SF_DEBUG_OUTPUT.test(116)&&!(SF_ALL&SF_DEBUG_OUTPUT).any());
static_assert(SF_DRIVER_CACHE_SALT.test(117)&&!(SF_ALL&SF_DRIVER_CACHE_SALT).any());
static_assert(SF_FILM_DATA_PASSES.test(143)&&SF_NODE_AOV.test(144)&&SF_FILM_CRYPTOMATTE.test(145)&&SF_FILM_DENOISING.test(146)&&SF_FILM_MOTION.test(147)&&SF_FILM_MOTION_EXACT.test(148)&&(SF_SUPERSET_SCENE_ONLY&SF_FILM_MOTION_EXACT).any()&&(SF_SUPERSET_SCENE_ONLY&(SF_FILM_DATA_PASSES|SF_NODE_AOV|SF_FILM_CRYPTOMATTE|SF_FILM_DENOISING|SF_FILM_MOTION))==(SF_FILM_DATA_PASSES|SF_NODE_AOV|SF_FILM_CRYPTOMATTE|SF_FILM_DENOISING|SF_FILM_MOTION));
static_assert(SF_TEX_ENVIRONMENT.test(149)&&SF_TEX_IMAGE_BOX.test(150)&&SF_SAMPLING_PATTERN.test(151)&&SF_CAMERA_PROJECTIONS.test(152)&&SF_LIGHT_FALLOFF.test(153)&&SF_LIGHT_LAMP_SHADER.test(154)&&SF_NODE_IES.test(155)&&SF_BSDF_THIN_FILM_CONDUCTOR.test(156)&&SF_TEX_SKY.test(157)&&SF_BACKGROUND_SUN.test(158)&&SF_TEX_SKY_SCATTERING.test(159)&&SF_MATH_ARC.test(160)&&(SF_SUPERSET_SCENE_ONLY&SF_MATH_ARC).any()&&
  (SF_SUPERSET_SCENE_ONLY&(SF_TEX_ENVIRONMENT|SF_TEX_IMAGE_BOX|SF_SAMPLING_PATTERN|SF_CAMERA_PROJECTIONS|SF_LIGHT_FALLOFF|SF_LIGHT_LAMP_SHADER|SF_NODE_IES|SF_BSDF_THIN_FILM_CONDUCTOR|SF_TEX_SKY|SF_BACKGROUND_SUN|SF_TEX_SKY_SCATTERING))==(SF_TEX_ENVIRONMENT|SF_TEX_IMAGE_BOX|SF_SAMPLING_PATTERN|SF_CAMERA_PROJECTIONS|SF_LIGHT_FALLOFF|SF_LIGHT_LAMP_SHADER|SF_NODE_IES|SF_BSDF_THIN_FILM_CONDUCTOR|SF_TEX_SKY|SF_BACKGROUND_SUN|SF_TEX_SKY_SCATTERING));
static_assert(SF_BSDF_METALLIC.test(128)&&SF_BSDF_BECKMANN.test(129)&&SF_BSDF_ASHIKHMIN_SHIRLEY.test(130)&&SF_BSDF_VELVET.test(131)&&SF_BSDF_TOON.test(132)&&SF_NODE_BEVEL.test(133)&&SF_NODE_RAYCAST.test(134)&&SF_NODE_RADIAL_TILING.test(135)&&(SF_SUPERSET_SCENE_ONLY&SF_NODE_RAYCAST).any()&&(SF_SUPERSET_SCENE_ONLY&SF_BSDF_FAMILIES)==SF_BSDF_FAMILIES);

namespace {
// Record size and handler feature for every opcode the WGSL size switch accepts.
// Zero size: unknown to the GPU interpreter (it reports error 2 there).
struct OpInfo {uint32_t bytes;ShaderFeatures feature;};
OpInfo op_info(uint32_t op){
#define OP(name,type,feature) case name:return {uint32_t(sizeof(SVMNode##type)),feature};
  switch(op){
    OP(NODE_SHADER_JUMP,ShaderJump,SF_NONE) OP(NODE_JUMP_IF_ZERO,JumpIfZero,SF_NONE) OP(NODE_JUMP_IF_ONE,JumpIfOne,SF_NONE)
    OP(NODE_MATH,Math,SF_MATH) OP(NODE_CLAMP,Clamp,SF_CLAMP) OP(NODE_VALUE_F,ValueF,SF_NONE) OP(NODE_VALUE_V,ValueV,SF_NONE)
    OP(NODE_CONVERT,Convert,SF_CONVERT) OP(NODE_GEOMETRY,Geometry,SF_GEOMETRY) OP(NODE_TEX_COORD,TexCoord,SF_TEX_COORD)
    OP(NODE_TEX_IMAGE,TexImage,SF_IMAGE) OP(NODE_TEX_IMAGE_DERIVATIVE,TexImage,SF_IMAGE)
    OP(NODE_TEX_IMAGE_BOX,TexImageBox,SF_IMAGE|SF_TEX_IMAGE_BOX) OP(NODE_TEX_IMAGE_BOX_DERIVATIVE,TexImageBox,SF_IMAGE|SF_TEX_IMAGE_BOX)
    OP(NODE_TEX_ENVIRONMENT,TexEnvironment,SF_IMAGE|SF_TEX_ENVIRONMENT) OP(NODE_TEX_ENVIRONMENT_DERIVATIVE,TexEnvironment,SF_IMAGE|SF_TEX_ENVIRONMENT)
    OP(NODE_ATTR,Attr,SF_ATTR) OP(NODE_ATTR_DERIVATIVE,Attr,SF_ATTR) OP(NODE_NORMAL_MAP,NormalMap,SF_NORMAL_MAP)
    OP(NODE_TANGENT,Tangent,SF_TANGENT) OP(NODE_TANGENT_DERIVATIVE,Tangent,SF_TANGENT)
    OP(NODE_VERTEX_COLOR,VertexColor,SF_VERTEX_COLOR) OP(NODE_VERTEX_COLOR_DERIVATIVE,VertexColor,SF_VERTEX_COLOR)
    OP(NODE_VECTOR_MATH,VectorMath,SF_VECTOR_MATH) OP(NODE_SEPARATE_VECTOR,SeparateVector,SF_SEPARATE_VECTOR)
    OP(NODE_COMBINE_VECTOR,CombineVector,SF_COMBINE_VECTOR) OP(NODE_CLOSURE_VOLUME,ClosureVolume,SF_CLOSURE_VOLUME)
    OP(NODE_VOLUME_COEFFICIENTS,VolumeCoefficients,SF_NODE_VOLUME_COEFFICIENTS) OP(NODE_PRINCIPLED_VOLUME,PrincipledVolume,SF_NODE_PRINCIPLED_VOLUME)
    OP(NODE_CLOSURE_BSDF,ClosureBsdf,SF_NONE) OP(NODE_CLOSURE_HOLDOUT,ClosureHoldout,SF_CLOSURE_HOLDOUT)
    OP(NODE_CLOSURE_SET_NORMAL,ClosureSetNormal,SF_CLOSURE_SET_NORMAL) OP(NODE_CLOSURE_SET_WEIGHT,ClosureSetWeight,SF_NONE)
    OP(NODE_CLOSURE_WEIGHT,ClosureWeight,SF_NONE) OP(NODE_CLOSURE_BACKGROUND,ClosureBackground,SF_NONE)
    OP(NODE_CLOSURE_EMISSION,ClosureEmission,SF_NONE) OP(NODE_EMISSION_WEIGHT,EmissionWeight,SF_NONE) OP(NODE_MIX_CLOSURE,MixClosure,SF_NONE)
    OP(NODE_TEX_CHECKER,TexChecker,SF_TEX_CHECKER) OP(NODE_TEX_GRADIENT,TexGradient,SF_TEX_GRADIENT)
    OP(NODE_GAMMA,Gamma,SF_GAMMA) OP(NODE_INVERT,Invert,SF_INVERT) OP(NODE_BRIGHTCONTRAST,BrightContrast,SF_BRIGHTCONTRAST)
    OP(NODE_TEX_NOISE,TexNoise,SF_TEX_NOISE) OP(NODE_TEX_VORONOI,TexVoronoi,SF_TEX_VORONOI) OP(NODE_TEX_WHITE_NOISE,TexWhiteNoise,SF_TEX_WHITE_NOISE)
    OP(NODE_TEX_WAVE,TexWave,SF_TEX_WAVE) OP(NODE_TEX_MAGIC,TexMagic,SF_TEX_MAGIC) OP(NODE_TEX_BRICK,TexBrick,SF_TEX_BRICK)
    OP(NODE_MAPPING,Mapping,SF_MAPPING) OP(NODE_TEXTURE_MAPPING,TextureMapping,SF_MAPPING) OP(NODE_MIN_MAX,MinMax,SF_MAPPING)
    OP(NODE_MAP_RANGE,MapRange,SF_MAP_RANGE) OP(NODE_VECTOR_MAP_RANGE,VectorMapRange,SF_MAP_RANGE)
    OP(NODE_MIX,Mix,SF_MIX) OP(NODE_MIX_COLOR,MixColor,SF_MIX) OP(NODE_MIX_FLOAT,MixFloat,SF_MIX)
    OP(NODE_MIX_VECTOR,MixVector,SF_MIX) OP(NODE_MIX_VECTOR_NON_UNIFORM,MixVectorNonUniform,SF_MIX)
    OP(NODE_HSV,HSV,SF_HSV) OP(NODE_SEPARATE_COLOR,SeparateColor,SF_COLOR_SEPCOMB) OP(NODE_COMBINE_COLOR,CombineColor,SF_COLOR_SEPCOMB)
    OP(NODE_RGB_RAMP,RGBRamp,SF_RAMP) OP(NODE_CURVES,Curves,SF_RAMP) OP(NODE_FLOAT_CURVE,FloatCurve,SF_RAMP)
    OP(NODE_FRESNEL,Fresnel,SF_FRESNEL) OP(NODE_LAYER_WEIGHT,LayerWeight,SF_FRESNEL) OP(NODE_VECTOR_ROTATE,VectorRotate,SF_VECTOR_ROTATE)
    OP(NODE_BLACKBODY,Blackbody,SF_BLACKBODY) OP(NODE_NORMAL,Normal,SF_NORMAL)
    OP(NODE_OBJECT_INFO,ObjectInfo,SF_SHADING_INFO) OP(NODE_CAMERA,Camera,SF_SHADING_INFO) OP(NODE_SCENE_TIME,SceneTime,SF_SHADING_INFO)
    OP(NODE_VECTOR_TRANSFORM,VectorTransform,SF_SHADING_INFO) OP(NODE_LIGHT_PATH,LightPath,SF_LIGHT_PATH) OP(NODE_LIGHT_FALLOFF,LightFalloff,SF_LIGHT_FALLOFF) OP(NODE_IES,IES,SF_NODE_IES) OP(NODE_TEX_SKY,TexSky,ShaderFeatures{})
    OP(NODE_WAVELENGTH,Wavelength,SF_WAVELENGTH) OP(NODE_WIREFRAME,Wireframe,SF_WIREFRAME) OP(NODE_HAIR_INFO,HairInfo,SF_NODE_HAIR_INFO) OP(NODE_POINT_INFO,PointInfo,SF_NODE_POINT_INFO) OP(NODE_PARTICLE_INFO,ParticleInfo,SF_NODE_PARTICLE_INFO) OP(NODE_TEX_GABOR,TexGabor,SF_TEX_GABOR)
    OP(NODE_AMBIENT_OCCLUSION,AmbientOcclusion,SF_NODE_AO) OP(NODE_BEVEL,Bevel,SF_NODE_BEVEL) OP(NODE_RAYCAST,Raycast,SF_NODE_RAYCAST|SF_ATTR)
    OP(NODE_RADIAL_TILING,RadialTiling,SF_NODE_RADIAL_TILING) OP(NODE_AOV_COLOR,AOVColor,SF_NODE_AOV) OP(NODE_AOV_VALUE,AOVValue,SF_NODE_AOV)
    OP(NODE_SET_DISPLACEMENT,SetDisplacement,SF_NODE_DISPLACEMENT) OP(NODE_DISPLACEMENT,Displacement,SF_NODE_DISPLACEMENT)
    OP(NODE_VECTOR_DISPLACEMENT,VectorDisplacement,SF_NODE_DISPLACEMENT|SF_ATTR)
    OP(NODE_SET_BUMP,SetBump,SF_BUMP) OP(NODE_ENTER_BUMP_EVAL,EnterBumpEval,SF_BUMP_STATE) OP(NODE_LEAVE_BUMP_EVAL,LeaveBumpEval,SF_BUMP_STATE)
    OP(NODE_GEOMETRY_DERIVATIVE,Geometry,SF_GEOMETRY_DERIVATIVE) OP(NODE_TEX_COORD_DERIVATIVE,TexCoord,SF_TEX_COORD|SF_TEX_COORD_DERIVATIVE)
    OP(NODE_VALUE_F_DERIVATIVE,ValueF,SF_VALUE_DERIVATIVE) OP(NODE_VALUE_V_DERIVATIVE,ValueV,SF_VALUE_DERIVATIVE)
    OP(NODE_CONVERT_DERIVATIVE,Convert,SF_VALUE_DERIVATIVE) OP(NODE_SEPARATE_VECTOR_DERIVATIVE,SeparateVector,SF_VALUE_DERIVATIVE)
    OP(NODE_COMBINE_VECTOR_DERIVATIVE,CombineVector,SF_VALUE_DERIVATIVE) OP(NODE_VECTOR_MATH_DERIVATIVE,VectorMath,SF_VECTOR_MATH_DERIVATIVE)
    OP(NODE_MAPPING_DERIVATIVE,Mapping,SF_MAPPING_DERIVATIVE) OP(NODE_TEXTURE_MAPPING_DERIVATIVE,TextureMapping,SF_MAPPING_DERIVATIVE)
    default:return {0,SF_NONE};
  }
#undef OP
}
// Data record and feature for each closure svm_bsdf accepts (wavefront_svm_closures.wgsl).
OpInfo bsdf_info(uint32_t kind){
  switch(kind){
    case CLOSURE_BSDF_DIFFUSE_ID:return {uint32_t(sizeof(SVMNodeDiffuseBsdfData)),SF_BSDF_DIFFUSE};
    case CLOSURE_BSDF_TRANSLUCENT_ID:return {uint32_t(sizeof(SVMNodeSimpleBsdfData)),SF_BSDF_TRANSLUCENT};
    case CLOSURE_BSDF_TRANSPARENT_ID:return {uint32_t(sizeof(SVMNodeSimpleBsdfData)),SF_BSDF_TRANSPARENT};
    case CLOSURE_BSDF_MICROFACET_GGX_REFRACTION_ID:return {uint32_t(sizeof(SVMNodeRefractionBsdfData)),SF_BSDF_GLASS};
    case CLOSURE_BSDF_MICROFACET_GGX_GLASS_ID:return {uint32_t(sizeof(SVMNodeGlassBsdfData)),SF_BSDF_GLASS};
    case CLOSURE_BSDF_MICROFACET_GGX_ID:case CLOSURE_BSDF_MICROFACET_MULTI_GGX_ID:return {uint32_t(sizeof(SVMNodeGlossyBsdfData)),SF_BSDF_GLOSSY};
    case CLOSURE_BSDF_PRINCIPLED_ID:return {uint32_t(sizeof(SVMNodePrincipledBsdfData)),SF_BSDF_PRINCIPLED};
    case CLOSURE_BSDF_PHYSICAL_CONDUCTOR:case CLOSURE_BSDF_F82_CONDUCTOR:return {uint32_t(sizeof(SVMNodeMetallicBsdfData)),SF_BSDF_METALLIC};
    case CLOSURE_BSDF_MICROFACET_BECKMANN_ID:return {uint32_t(sizeof(SVMNodeGlossyBsdfData)),SF_BSDF_BECKMANN};
    case CLOSURE_BSDF_MICROFACET_BECKMANN_REFRACTION_ID:return {uint32_t(sizeof(SVMNodeRefractionBsdfData)),SF_BSDF_BECKMANN};
    case CLOSURE_BSDF_MICROFACET_BECKMANN_GLASS_ID:return {uint32_t(sizeof(SVMNodeGlassBsdfData)),SF_BSDF_BECKMANN};
    case CLOSURE_BSDF_ASHIKHMIN_SHIRLEY_ID:return {uint32_t(sizeof(SVMNodeGlossyBsdfData)),SF_BSDF_ASHIKHMIN_SHIRLEY};
    case CLOSURE_BSDF_ASHIKHMIN_VELVET_ID:return {uint32_t(sizeof(SVMNodeSimpleBsdfData)),SF_BSDF_VELVET};
    case CLOSURE_BSDF_DIFFUSE_TOON_ID:case CLOSURE_BSDF_GLOSSY_TOON_ID:return {uint32_t(sizeof(SVMNodeToonBsdfData)),SF_BSDF_TOON};
    case CLOSURE_BSDF_HAIR_REFLECTION_ID:case CLOSURE_BSDF_HAIR_TRANSMISSION_ID:return {uint32_t(sizeof(SVMNodeHairBsdfData)),SF_BSDF_HAIR};
    case CLOSURE_BSDF_HAIR_CHIANG_ID:return {uint32_t(sizeof(SVMNodePrincipledHairBsdfData)),SF_BSDF_HAIR_CHIANG};
    case CLOSURE_BSDF_HAIR_HUANG_ID:return {uint32_t(sizeof(SVMNodePrincipledHairBsdfData)),SF_BSDF_HAIR_HUANG|SF_ATTR};
    case CLOSURE_BSDF_RAY_PORTAL_ID:return {uint32_t(sizeof(SVMNodeRayPortalBsdfData)),SF_RAY_PORTAL};
    case CLOSURE_BSSRDF_BURLEY_ID:case CLOSURE_BSSRDF_RANDOM_WALK_ID:case CLOSURE_BSSRDF_RANDOM_WALK_LEGACY_ID:case CLOSURE_BSSRDF_RANDOM_WALK_SKIN_ID:
      return {uint32_t(sizeof(SVMNodeBssrdfData)),SF_SUBSURFACE};
    default:return {0,SF_NONE};
  }
}
uint32_t byte_at(const std::vector<uint32_t> &w,size_t word,size_t offset){
  const size_t index=word+offset/4;return index<w.size()?(w[index]>>(8*(offset%4)))&255u:0u;
}
// Principled record census: optional thin-film/thin-wall handlers and the
// subsurface lobe (a BSSRDF closure, or with a thin wall the diffuse and translucent
// closures of bsdf_thin_subsurface_setup). A linked input may enable a feature at
// run time, so it counts like an enabling constant.
struct PrincipledCensus {ShaderFeatures features;const char *unsupported=nullptr;};
PrincipledCensus principled_census(const std::vector<uint32_t> &w,size_t data){
  PrincipledCensus out;auto field=[&](size_t offset){return w[data+offset/4];};
  auto linked=[](uint32_t bits){return (bits>>8)==(SVM_INPUT_STACK_OFFSET_MASK>>8);};
  auto above=[&](size_t offset,float cutoff,bool saturate){const uint32_t bits=field(offset);if(linked(bits))return true;float v;std::memcpy(&v,&bits,4);
    if(saturate)v=std::min(std::max(v,0.f),1.f);
    return v>cutoff;};
  if(above(offsetof(SVMNodePrincipledBsdfData,thin_film_thickness),THINFILM_THICKNESS_CUTOFF,false))out.features|=SF_BSDF_THIN_FILM;
  const size_t wall=data+offsetof(SVMNodePrincipledBsdfData,thin_wall)/4;
  if((w[wall]!=0||byte_at(w,wall,offsetof(SVMInputInt,offset))!=SVM_STACK_INVALID)&&above(offsetof(SVMNodePrincipledBsdfData,transmission_weight),CLOSURE_WEIGHT_CUTOFF,true))
    out.features|=SF_BSDF_THIN_WALL;
  if(above(offsetof(SVMNodePrincipledBsdfData,subsurface_weight),CLOSURE_WEIGHT_CUTOFF,true)){
    // A thin wall (constant or linked): bsdf_thin_subsurface_setup's diffuse and translucent closures. No thin wall
    // (constant 0 or linked): the BSSRDF closure, with the Christensen-Burley BSSRDF's disk branch and disk entry
    // (integrator/subsurface_disk.h).
    const bool wall_linked=byte_at(w,wall,offsetof(SVMInputInt,offset))!=SVM_STACK_INVALID;
    if(w[wall]!=0||wall_linked)out.features|=SF_BSDF_THIN_SUBSURFACE;
    if(w[wall]==0||wall_linked){out.features|=SF_SUBSURFACE;
      if(field(offsetof(SVMNodePrincipledBsdfData,subsurface_method))==CLOSURE_BSSRDF_BURLEY_ID)out.features|=SF_SUBSURFACE_DISK;}
  }
  return out;
}
// Metallic BSDF record census (svm/closure.h CLOSURE_BSDF_PHYSICAL_CONDUCTOR /
// CLOSURE_BSDF_F82_CONDUCTOR): the Beckmann distribution (its region) and the thin film
// (thickness max(input, 1e-5) above 0.1, or linked). Both Fresnel types use the
// Principled thin-film handler; the physical conductor adds its polarized bottom
// interface (BSDF_THIN_FILM_CONDUCTOR).
PrincipledCensus metallic_census(const std::vector<uint32_t> &w,size_t data,uint32_t kind){
  PrincipledCensus out;const uint32_t bits=w[data+offsetof(SVMNodeMetallicBsdfData,thin_film_thickness)/4];
  float thickness;std::memcpy(&thickness,&bits,4);
  const bool film=(bits>>8)==(SVM_INPUT_STACK_OFFSET_MASK>>8)||std::max(thickness,1e-5f)>THINFILM_THICKNESS_CUTOFF;
  if(w[data+offsetof(SVMNodeMetallicBsdfData,distribution)/4]==CLOSURE_BSDF_MICROFACET_BECKMANN_ID)out.features|=SF_BSDF_BECKMANN;
  if(film)out.features|=SF_BSDF_THIN_FILM|(kind==CLOSURE_BSDF_PHYSICAL_CONDUCTOR?SF_BSDF_THIN_FILM_CONDUCTOR:ShaderFeatures{});
  return out;
}
}

SvmCensus svm_census(const std::vector<uint32_t> &w,size_t shader_count,const std::vector<uint32_t> *roots,bool displacement){
  SvmCensus out;const uint64_t n=w.size();
  constexpr uint32_t header=1+sizeof(SVMNodeShaderJump)/4;
  std::vector<uint32_t> work;std::unordered_set<uint32_t> started,decoded;
  for(size_t shader=0;shader<shader_count;shader++)if(!roots||std::find(roots->begin(),roots->end(),uint32_t(shader))!=roots->end())work.push_back(uint32_t(shader*header));
  // svm_take(bytes): fails when pc>count or count(bytes)>count-pc (word units).
  auto take=[&](uint64_t &pc,uint32_t bytes)->bool{const uint64_t words=bytes/4;if(pc>n||words>n-pc)return false;pc+=words;return true;};
  while(!work.empty()){
    uint64_t pc=work.back();work.pop_back();
    if(!started.insert(uint32_t(pc)).second)continue;
    for(;;){
      const uint64_t opcode_at=pc;if(!take(pc,4)){out.stopped_paths++;break;}
      const uint32_t op=w[opcode_at];if(op==NODE_END)break;
      // NODE_AOV_START has no record: it ends the program unless the evaluation writes AOVs.
      if(op==NODE_AOV_START){if(decoded.insert(uint32_t(opcode_at)).second)out.decoded_nodes++;out.features|=SF_NODE_AOV;continue;}
      const OpInfo info=op_info(op);if(!info.bytes){out.stopped_paths++;break;}
      const uint64_t node=pc;if(!take(pc,info.bytes)){out.stopped_paths++;break;}
      if(decoded.insert(uint32_t(opcode_at)).second)out.decoded_nodes++;
      out.features|=info.feature;
      if(op==NODE_SHADER_JUMP){
        // displacement: the displacement program alone (SHADER_TYPE_DISPLACEMENT, SHADER_EVAL_DISPLACE).
        if(displacement){work.push_back(w[node+offsetof(SVMNodeShaderJump,offset_displacement)/4]);break;}
        work.push_back(w[node+offsetof(SVMNodeShaderJump,offset_surface)/4]);
        work.push_back(w[node+offsetof(SVMNodeShaderJump,offset_volume)/4]);break;
      }
      if(op==NODE_JUMP_IF_ZERO||op==NODE_JUMP_IF_ONE){
        const size_t field=op==NODE_JUMP_IF_ZERO?offsetof(SVMNodeJumpIfZero,jump_offset):offsetof(SVMNodeJumpIfOne,jump_offset);
        work.push_back(uint32_t(pc)+w[node+field/4]);  // u32 wrap, as in WGSL.
      }
      // Volume phase functions beyond Henyey-Greenstein, and volume programs that can emit (VOLUME_EMISSION is
      // kept only in scenes with volumes, shader_features).
      if(op==NODE_CLOSURE_VOLUME||op==NODE_VOLUME_COEFFICIENTS){
        const uint32_t kind=op==NODE_CLOSURE_VOLUME?w[node+offsetof(SVMNodeClosureVolume,closure_type)/4]:w[node+offsetof(SVMNodeVolumeCoefficients,closure_type)/4];
        // The phase functions other than Henyey-Greenstein (VOLUME_PHASES regions plus one bit per family): their phase
        // scatter runs in the volume module's shade_volume_phase entry (compile envelope; native/DEVICE.md,
        // "Shader-defined volumes").
        if(kind==CLOSURE_VOLUME_FOURNIER_FORAND_ID||kind==CLOSURE_VOLUME_RAYLEIGH_ID||kind==CLOSURE_VOLUME_DRAINE_ID||kind==CLOSURE_VOLUME_MIE_ID){
          out.features|=SF_VOLUME_PHASES;
          out.features|=kind==CLOSURE_VOLUME_FOURNIER_FORAND_ID?SF_PHASE_FOURNIER_FORAND:kind==CLOSURE_VOLUME_RAYLEIGH_ID?SF_PHASE_RAYLEIGH:
                        kind==CLOSURE_VOLUME_DRAINE_ID?SF_PHASE_DRAINE:SF_PHASE_MIE|SF_PHASE_DRAINE;
        }
      }
      if(op==NODE_CLOSURE_EMISSION||op==NODE_VOLUME_COEFFICIENTS||op==NODE_PRINCIPLED_VOLUME)out.features|=SF_VOLUME_EMISSION;
      // Local traversals (SvmCensus::local_traversal): the byte fields of the node record.
      {auto byte_at=[&](size_t offset){return (w[node+offset/4]>>(8*(offset%4)))&0xffu;};
        if(op==NODE_BEVEL||(op==NODE_AMBIENT_OCCLUSION&&(byte_at(offsetof(SVMNodeAmbientOcclusion,flags))&NODE_AO_ONLY_LOCAL))||
           (op==NODE_RAYCAST&&byte_at(offsetof(SVMNodeRaycast,only_local))))out.local_traversal=true;
        // The Geometry node's Tangent output (SVM_GEOMETRY_TANGENT).
        if((op==NODE_GEOMETRY||op==NODE_GEOMETRY_DERIVATIVE)&&byte_at(offsetof(SVMNodeGeometry,geom_type))==NODE_GEOM_T)out.features|=SF_GEOMETRY_TANGENT;}
      if(op==NODE_CLOSURE_BSDF){
        const uint32_t kind=w[node+offsetof(SVMNodeClosureBsdf,closure_type)/4];const uint64_t data_at=pc;
        const OpInfo data=bsdf_info(kind);
        if(!data.bytes||!take(pc,data.bytes)){out.stopped_paths++;break;}
        out.features|=data.feature;
        if(kind==CLOSURE_BSSRDF_BURLEY_ID)out.features|=SF_SUBSURFACE_DISK;
        if(kind==CLOSURE_BSDF_PRINCIPLED_ID){const auto principled=principled_census(w,data_at);out.features|=principled.features;
          if(principled.unsupported&&out.unsupported.empty())out.unsupported=principled.unsupported;}
        if(kind==CLOSURE_BSDF_PHYSICAL_CONDUCTOR||kind==CLOSURE_BSDF_F82_CONDUCTOR){const auto metallic=metallic_census(w,data_at,kind);out.features|=metallic.features;
          if(metallic.unsupported&&out.unsupported.empty())out.unsupported=metallic.unsupported;}
      }
      // Radial Tiling with two sides and a roundness strictly between 0 and 1: the CPU kernel's
      // outputs there are binary32 rounding residue (tangent and secant of the binary32 pi/2,
      // cancelling terms near 1e14, subnormal intermediates) that the device does not
      // reproduce. Constant inputs are refused here; linked ones report 16 at run time.
      if(op==NODE_RADIAL_TILING&&out.unsupported.empty()){
        const uint32_t sb=w[node+offsetof(SVMNodeRadialTiling,r_gon_sides)/4],rb=w[node+offsetof(SVMNodeRadialTiling,r_gon_roundness)/4];
        const auto linked_input=[](uint32_t bits){return (bits>>8)==(SVM_INPUT_STACK_OFFSET_MASK>>8);};
        float sides,roundness;std::memcpy(&sides,&sb,4);std::memcpy(&roundness,&rb,4);
        if(!linked_input(sb)&&!linked_input(rb)&&std::max(sides,2.0f)==2.0f&&std::clamp(roundness,0.0f,1.0f)>0.0f&&std::clamp(roundness,0.0f,1.0f)<1.0f)
          out.unsupported="Unsupported feature: Radial Tiling node with two sides (r_gon_sides at most 2) and a roundness strictly between 0 and 1 is not implemented by the WebGPU device "
                          "(the Cycles CPU outputs there are binary32 rounding residue); the scene is rejected, not approximated";
      }
      // A Light Path output that reads path state or the shading point (Backfacing, Ray Length, the depths): LIGHT_PATH_STATE,
      // kept by shader_features only in scenes with transparent shadows or volumes.
      if(op==NODE_LIGHT_PATH&&w[node+offsetof(SVMNodeLightPath,path_type)/4]>=uint32_t(NODE_LP_backfacing))out.features|=SF_LIGHT_PATH_STATE;
      // A Principled Volume attribute name that is not a standard attribute's (PRINCIPLED_VOLUME_ATTRIBUTE).
      if(op==NODE_PRINCIPLED_VOLUME)for(size_t field:{offsetof(SVMNodePrincipledVolume,attr_density),offsetof(SVMNodePrincipledVolume,attr_color),offsetof(SVMNodePrincipledVolume,attr_temperature)})
        if(w[node+field/4]>=uint32_t(ATTR_STD_NUM)&&w[node+field/4]!=uint32_t(ATTR_STD_NOT_FOUND))out.features|=SF_PRINCIPLED_VOLUME_ATTRIBUTE;
      // The Raycast node's attribute records (NODE_ATTR opcode + SVMNodeAttr each) follow its record.
      if(op==NODE_RAYCAST){
        out.raycast_records.push_back(uint32_t(node));
        const uint64_t count=(w[node+offsetof(SVMNodeRaycast,num_attributes)/4]>>(8*(offsetof(SVMNodeRaycast,num_attributes)%4)))&0xffffu;
        bool ok=true;for(uint64_t i=0;i<count&&ok;i++){const uint64_t at=pc;ok=take(pc,4)&&w[at]==NODE_ATTR&&take(pc,sizeof(SVMNodeAttr));}
        if(!ok){out.stopped_paths++;break;}
      }
      // Ramp and curve tables follow their record (svm_ramp_node: size>=2, sizes it decodes).
      if(op==NODE_RGB_RAMP||op==NODE_CURVES||op==NODE_FLOAT_CURVE){
        const size_t field=op==NODE_RGB_RAMP?offsetof(SVMNodeRGBRamp,table_size):op==NODE_CURVES?offsetof(SVMNodeCurves,table_size):offsetof(SVMNodeFloatCurve,table_size);
        const uint32_t size=w[node+field/4];const uint64_t words=uint64_t(size)*(op==NODE_FLOAT_CURVE?1:4);
        if(size<2||size>0x0fffffffu||!take(pc,uint32_t(words*4))){out.stopped_paths++;break;}
      }
      // The Sky Texture node's model data follows its record (svm_node_tex_sky); the scattering models
      // read their precomputed sky image.
      if(op==NODE_TEX_SKY){
        const uint32_t type=byte_at(w,node,offsetof(SVMNodeTexSky,sky_type));
        const bool analytic=type==NODE_SKY_PREETHAM||type==NODE_SKY_HOSEK;
        const uint64_t data_at=pc;
        if(!take(pc,analytic?sizeof(SVMNodeTexSkyPreethamData):sizeof(SVMNodeTexSkyNishitaData))){out.stopped_paths++;break;}
        // The sun disc (angular diameter >= 0) is what the light manager guides the background light towards.
        out.features|=analytic?SF_TEX_SKY:SF_TEX_SKY_SCATTERING;
        if(!analytic){out.features|=SF_IMAGE;float angular;std::memcpy(&angular,&w[data_at+offsetof(SVMNodeTexSkyNishitaData,angular_diameter)/4],4);
          if(angular>=0.0f)out.features|=SF_BACKGROUND_SUN;}
      }
      // The Math node's arcsine and arccosine (SVM_MATH_ARC): the correctly rounded adapter only when a reachable node uses them.
      if(op==NODE_MATH){const uint32_t type=w[node+offsetof(SVMNodeMath,math_type)/4];if(type==NODE_MATH_ARCSINE||type==NODE_MATH_ARCCOSINE)out.features|=SF_MATH_ARC;}
      if((op==NODE_TEX_COORD||op==NODE_TEX_COORD_DERIVATIVE)&&byte_at(w,node,offsetof(SVMNodeTexCoord,texco_type))==NODE_TEXCO_OBJECT_WITH_TRANSFORM){
        if(!take(pc,sizeof(PackedTransform))){out.stopped_paths++;break;}
      }
    }
  }
  return out;
}

std::string specialize_wgsl(const std::string &source,ShaderFeatures enabled){
  std::istringstream in(source);std::ostringstream out;std::string line;
  struct Region {bool keep;std::string fallback;};std::vector<Region> stack;
  auto active=[&]{for(auto &r:stack)if(!r.keep)return false;return true;};
  while(std::getline(in,line)){
    if(line.rfind("//#if ",0)==0){
      const size_t space=line.find(' ',6);const std::string name=line.substr(6,space==std::string::npos?std::string::npos:space-6);
      std::string fallback;
      if(space!=std::string::npos){if(line.compare(space,6," else ")!=0)throw std::runtime_error("Malformed WGSL feature marker: "+line);fallback=line.substr(space+6);}
      // "!NAME" keeps the region when NAME is not reached (the alternative of a NAME region).
      const bool negated=name.size()>1&&name[0]=='!';const std::string base=negated?name.substr(1):name;
      ShaderFeatures bit;for(auto &n:shader_feature_names())if(base==n.name)bit=n.bit;
      if(!bit)throw std::runtime_error("Unknown WGSL feature marker: "+name);
      const bool outer=active();stack.push_back({(enabled&bit).any()!=negated,fallback});
      if(outer&&!stack.back().keep&&!fallback.empty())out<<fallback<<'\n';
      continue;
    }
    if(line.rfind("//#endif",0)==0){if(stack.empty())throw std::runtime_error("Unbalanced WGSL feature marker");stack.pop_back();continue;}
    if(active())out<<line<<'\n';
  }
  if(!stack.empty())throw std::runtime_error("Unterminated WGSL feature marker");
  return out.str();
}

void svm_canonical_padding(std::vector<uint32_t> &w){
  // The shader headers come first (scene/svm.cpp): NODE_SHADER_JUMP records whose program offsets all lie past the last header.
  constexpr uint32_t header=1+sizeof(SVMNodeShaderJump)/4;size_t shaders=0;uint64_t first_program=w.size();
  while((shaders+1)*header<=w.size()&&shaders*header<first_program&&w[shaders*header]==NODE_SHADER_JUMP){
    for(const size_t field:{offsetof(SVMNodeShaderJump,offset_surface),offsetof(SVMNodeShaderJump,offset_volume),offsetof(SVMNodeShaderJump,offset_displacement)})
      first_program=std::min<uint64_t>(first_program,w[shaders*header+1+field/4]);
    shaders++;}
  // The padding: one byte after only_local and the tail after the last stack offset (the record is a whole number of words).
  static_assert(offsetof(SVMNodeRaycast,num_attributes)==offsetof(SVMNodeRaycast,only_local)+2,"SVMNodeRaycast: one padding byte after only_local");
  constexpr size_t tail=offsetof(SVMNodeRaycast,hit_normal_offset)+sizeof(SVMStackOffset);
  static_assert(tail<=sizeof(SVMNodeRaycast)&&sizeof(SVMNodeRaycast)%4==0,"SVMNodeRaycast layout");
  auto clear=[&](uint32_t node,size_t byte){if(node+byte/4<w.size())w[node+byte/4]&=~(0xffu<<(8*(byte%4)));};
  for(const bool displacement:{false,true})for(const uint32_t node:svm_census(w,shaders,nullptr,displacement).raycast_records){
    clear(node,offsetof(SVMNodeRaycast,only_local)+1);for(size_t byte=tail;byte<sizeof(SVMNodeRaycast);byte++)clear(node,byte);}
}
const char *specialization_mode_name(SpecializationMode mode){
  return mode==SpecializationMode::Scene?"scene":mode==SpecializationMode::Superset?"superset":"off";
}
SpecializationMode specialization_mode_from_environment(unsigned *superset_group){
  const char *mode=std::getenv("CODEBRIX_CYCLES_SHADER_SPECIALIZATION");if(superset_group)*superset_group=0;
  if(!mode||!*mode||std::strcmp(mode,"1")==0)return SpecializationMode::Scene;
  if(std::strcmp(mode,"superset")==0)return SpecializationMode::Superset;
  if(std::strncmp(mode,"superset:",9)==0&&mode[9]>='0'&&mode[9]<='9'){
    char *end=nullptr;const unsigned long group=std::strtoul(mode+9,&end,10);
    if(end&&!*end&&group<1000){if(superset_group)*superset_group=unsigned(group);return SpecializationMode::Superset;}
  }
  if(std::strcmp(mode,"0")==0)return SpecializationMode::Off;
  throw std::runtime_error(std::string("CODEBRIX_CYCLES_SHADER_SPECIALIZATION must be 1, superset or 0, not '")+mode+"'");
}

std::vector<size_t> wgsl_inlined_bytes(const std::string &source,const std::vector<std::string> &entries){
  // Function bodies without line comments; each call to a defined function adds
  // that function's inlined size (recursion is impossible in WGSL).
  std::string text;text.reserve(source.size());
  for(size_t i=0;i<source.size();){
    if(source.compare(i,2,"//")==0){while(i<source.size()&&source[i]!='\n')i++;continue;}
    text+=source[i++];
  }
  auto ident=[](char c){return std::isalnum(static_cast<unsigned char>(c))||c=='_';};
  std::unordered_map<std::string,std::pair<size_t,size_t>> bodies; // name -> [start of "fn", end of body]
  for(size_t at=text.find("fn ");at!=std::string::npos;at=text.find("fn ",at+3)){
    if(at>0&&ident(text[at-1]))continue;
    size_t n=at+3;while(n<text.size()&&text[n]==' ')n++;size_t e=n;while(e<text.size()&&ident(text[e]))e++;
    if(e==n||e>=text.size()||text[e]!='(')continue;
    const size_t open=text.find('{',e);if(open==std::string::npos)break;
    int depth=0;size_t j=open;for(;j<text.size();j++){if(text[j]=='{')depth++;else if(text[j]=='}'&&--depth==0)break;}
    bodies[text.substr(n,e-n)]={at,j+1};
  }
  std::unordered_map<std::string,size_t> memo;
  std::function<size_t(const std::string&,unsigned)> size=[&](const std::string &name,unsigned depth)->size_t{
    if(auto m=memo.find(name);m!=memo.end())return m->second;
    const auto [start,stop]=bodies.at(name);size_t total=stop-start;
    if(depth<64){const size_t open=text.find('{',start);
      for(size_t i=open;i<stop;){
        if(!ident(text[i])||(i>open&&ident(text[i-1]))){i++;continue;}
        size_t e=i;while(e<stop&&ident(text[e]))e++;size_t k=e;while(k<stop&&text[k]==' ')k++;
        if(k<stop&&text[k]=='('){const std::string call=text.substr(i,e-i);if(call!=name&&bodies.count(call))total+=size(call,depth+1);}
        i=e;
      }}
    memo[name]=total;return total;
  };
  std::vector<size_t> out;out.reserve(entries.size());
  for(const std::string &entry:entries)out.push_back(bodies.count(entry)?size(entry,0):0);
  return out;
}
size_t wgsl_inlined_bytes(const std::string &source,const std::string &entry){return wgsl_inlined_bytes(source,std::vector<std::string>{entry})[0];}
std::vector<size_t> wgsl_inlined_copies(const std::string &source,const std::vector<std::string> &entries,const std::string &callee){
  // As wgsl_inlined_bytes: function bodies without line comments; a call to a defined function inlines its copies.
  std::string text;text.reserve(source.size());
  for(size_t i=0;i<source.size();){
    if(source.compare(i,2,"//")==0){while(i<source.size()&&source[i]!='\n')i++;continue;}
    text+=source[i++];
  }
  auto ident=[](char c){return std::isalnum(static_cast<unsigned char>(c))||c=='_';};
  std::unordered_map<std::string,std::pair<size_t,size_t>> bodies;
  for(size_t at=text.find("fn ");at!=std::string::npos;at=text.find("fn ",at+3)){
    if(at>0&&ident(text[at-1]))continue;
    size_t n=at+3;while(n<text.size()&&text[n]==' ')n++;size_t e=n;while(e<text.size()&&ident(text[e]))e++;
    if(e==n||e>=text.size()||text[e]!='(')continue;
    const size_t open=text.find('{',e);if(open==std::string::npos)break;
    int depth=0;size_t j=open;for(;j<text.size();j++){if(text[j]=='{')depth++;else if(text[j]=='}'&&--depth==0)break;}
    bodies[text.substr(n,e-n)]={at,j+1};
  }
  std::unordered_map<std::string,size_t> memo;
  std::function<size_t(const std::string&,unsigned)> copies=[&](const std::string &name,unsigned depth)->size_t{
    if(name==callee)return 1;
    if(auto m=memo.find(name);m!=memo.end())return m->second;
    const auto [start,stop]=bodies.at(name);size_t total=0;
    if(depth<64){const size_t open=text.find('{',start);
      for(size_t i=open;i<stop;){
        if(!ident(text[i])||(i>open&&ident(text[i-1]))){i++;continue;}
        size_t e=i;while(e<stop&&ident(text[e]))e++;size_t k=e;while(k<stop&&text[k]==' ')k++;
        if(k<stop&&text[k]=='('){const std::string call=text.substr(i,e-i);if(call!=name&&bodies.count(call))total+=copies(call,depth+1);}
        i=e;
      }}
    memo[name]=total;return total;
  };
  std::vector<size_t> out;out.reserve(entries.size());
  for(const std::string &entry:entries)out.push_back(bodies.count(entry)?copies(entry,0):0);
  return out;
}
size_t pipeline_size_warning_bytes(){
  if(const char *value=std::getenv("CODEBRIX_CYCLES_PIPELINE_SIZE_WARN_BYTES");value&&*value)return size_t(std::strtoull(value,nullptr,10));
  return pipeline_size_warning_default_bytes;
}
std::string portal_sampler_copies_error(const char *module,const char *entry,size_t copies){
  return std::string("WebGPU entry ")+entry+" of module "+module+" inlines the portal light sampler "+std::to_string(copies)+
    " times; at most one copy per entry is allowed (two copies compute wrong light samples on Intel graphics)";
}
std::string pipeline_size_warning(const char *module,const char *entry,size_t inlined_bytes,size_t threshold){
  char text[512];
  std::snprintf(text,sizeof(text),"pipeline '%s' (%s module) inlines to %zu bytes of WGSL, at or above the %zu-byte size guard: "
    "about %.0f backend instructions estimated (%.0f to %.0f); Intel Mesa drivers without upstream fix b7e1ac830984 can miscompile "
    "or crash above 65,535; the compiled variant and results are unchanged",entry,module,inlined_bytes,threshold,
    double(inlined_bytes)/pipeline_size_bytes_per_instruction_central,double(inlined_bytes)/pipeline_size_bytes_per_instruction_high,
    double(inlined_bytes)/pipeline_size_bytes_per_instruction_low);
  return text;
}
static size_t max_entry_bytes(const std::vector<std::string> &sources,ShaderFeatures features){
  size_t best=0;
  for(const std::string &source:sources){
    const std::string text=specialize_wgsl(source,features);
    for(size_t at=text.find("@compute");at!=std::string::npos;at=text.find("@compute",at+8)){
      const size_t fn=text.find(" fn ",at);if(fn==std::string::npos)break;const size_t paren=text.find('(',fn+4);
      best=std::max(best,wgsl_inlined_bytes(text,text.substr(fn+4,paren-fn-4)));
    }
  }
  return best;
}
SupersetGroups superset_groups(const std::vector<std::string> &sources,ShaderFeatures scene,ShaderFeatures candidates,size_t budget){
  SupersetGroups out;out.scene_bytes=max_entry_bytes(sources,scene);const size_t limit=std::max(budget,out.scene_bytes);
  // Candidate bits in ascending bit order across every mask word.
  std::vector<ShaderFeatures> todo;const ShaderFeatures open=candidates&~scene;
  for(unsigned b=0;b<shader_feature_bits;b++)if(open.test(b))todo.push_back(ShaderFeatures::bit(b));
  while(!todo.empty()){
    ShaderFeatures current=scene,placed;size_t bytes=out.scene_bytes;std::vector<ShaderFeatures> rest;
    for(const ShaderFeatures &bit:todo){const size_t trial=max_entry_bytes(sources,current|bit);
      if(trial<=limit){current|=bit;placed|=bit;bytes=trial;}else rest.push_back(bit);}
    if(!placed){for(const ShaderFeatures &bit:rest)out.unfit|=bit;break;}
    out.groups.push_back(current);out.group_bytes.push_back(bytes);todo.swap(rest);
  }
  return out;
}

std::string conservative_wgsl(const std::string &source){
  static const std::string from="@workgroup_size(64)",to="@workgroup_size(1)";
  std::string out=source;for(size_t at=out.find(from);at!=std::string::npos;at=out.find(from,at+to.size()))out.replace(at,from.size(),to);
  return out;
}
uint32_t declared_workgroup_size(const std::string &source,const char *entry){
  const std::string needle=std::string(" fn ")+entry+"(";const size_t at=source.find(needle);if(at==std::string::npos)return 0;
  const size_t open=source.rfind("@workgroup_size(",at);if(open==std::string::npos||source.find(" fn ",open)!=at)return 0;
  return uint32_t(std::strtoul(source.c_str()+open+16,nullptr,10));
}
}

#include "device.h"
#include "wavefront.h"
#include "../oracle/sha256.h"
#include "fingerprint.h"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
namespace ccl {
// Adapter descriptions can contain newlines (Adreno driver build strings).
static std::string json_text(const std::string &text){
  std::string out;for(char c:text){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(uint8_t(c)<0x20){out+=' ';}else out+=c;}return out;
}
ShaderFeatures WebGPUDevice::shader_features(){
  // Unimplemented Principled features are refused for every module, with or
  // without specialization, before any pipeline or dispatch uses the scene.
  // Principled handlers count only from shaders the scene references (triangles,
  // lights, world): Cycles' default surface shader is itself a Principled BSDF
  // and would otherwise compile Principled into every scene.
  if(unsupported_generation!=svm_generation){
    std::vector<uint32_t> roots;for(uint32_t s:tri_shader_words)roots.push_back(s&SHADER_MASK);
    for(const KernelCurve &curve:curve_records)roots.push_back(uint32_t(curve.shader_id)&SHADER_MASK);
    for(const uint32_t word:point_shader_words)roots.push_back(word&SHADER_MASK);
    for(const KernelLight &light:lamp_records())roots.push_back(uint32_t(light.shader_id)&SHADER_MASK);
    roots.push_back(uint32_t(data.background.surface_shader)&SHADER_MASK);roots.push_back(uint32_t(data.background.volume_shader)&SHADER_MASK);
    std::sort(roots.begin(),roots.end());roots.erase(std::unique(roots.begin(),roots.end()),roots.end());
    const SvmCensus referenced=svm_census(svm_node_words,shader_records.size(),&roots);
    unsupported_feature=referenced.unsupported;principled_features=referenced.features&SF_PRINCIPLED_ALL;unsupported_generation=svm_generation;
    // True displacement (displacement method True or Both): the displacement programs of the referenced shaders
    // run in shader_eval_displace (SHADER_EVAL_DISPLACE) during scene preparation, without integrator state.
    // Nodes whose pinned handlers need a path or the scene's BVH there are refused, never approximated.
    displacement_features=SF_NONE;displacement_program_features=SF_NONE;
    {std::vector<uint32_t> displaced;for(uint32_t s:roots)if(s<shader_records.size()&&(shader_records[s].flags&SD_HAS_DISPLACEMENT))displaced.push_back(s);
     if(!displaced.empty()){const SvmCensus program=svm_census(svm_node_words,shader_records.size(),&displaced,true);
       displacement_features=program.features|SF_EVAL_DISPLACE;displacement_program_features=program.features;
       for(uint32_t s:displaced)if(shader_records[s].flags&SD_HAS_BUMP_FROM_DISPLACEMENT)displacement_features|=SF_BUMP_UNDISPLACED;
       for(const uint flags:object_flag_records)if(flags&SD_OBJECT_HAS_VERTEX_MOTION)displacement_features|=SF_DISPLACE_VERTEX_MOTION;
       if(unsupported_feature.empty())unsupported_feature=program.unsupported;
       // Every opcode the SVM compiler emits has a record size (op_info), so a stopped path is a malformed program.
       if(unsupported_feature.empty()&&program.stopped_paths)unsupported_feature="Invalid displacement shader program";
       // The displacement program runs under KERNEL_FEATURE_NODE_MASK_DISPLACEMENT without path state: the Light Path node
       // reads its ray types, ray length and depths as zero, Ambient Occlusion is 1, Bevel returns N, Raycast misses, and
       // Wireframe works from the triangle; the handlers do the same outside prepare_surface_raytrace (svm_displace_program).
       // A closure (Principled BSDF) cannot feed the displacement output, so it never reaches the program.
       const ShaderFeatures stateful=program.features&SF_PRINCIPLED_ALL;
       if(unsupported_feature.empty()&&stateful.any())
         unsupported_feature="Invalid displacement shader program: closure features ("+shader_feature_list(stateful)+") in a displacement program";}}
    // Adapter-class refusal from the known-adapter table (device_tier.cpp), for
    // example texture, colour and utility nodes on the Adreno 650.
    const KnownAdapter *known=tier_policy.known_adapter();
    // AO shadow rays come from KernelData, not from the SVM census.
    ShaderFeatures scene_regions=referenced.features;if(data.kernel_features&KERNEL_FEATURE_AO)scene_regions|=SF_AO_SHADOW;
    for(const auto &[region_name,text]:{std::pair{known?known->refused_region:nullptr,known?known->refused_text:nullptr},
                                         std::pair{known?known->also_refused_region:nullptr,known?known->also_refused_text:nullptr}}){
      if(!unsupported_feature.empty()||!region_name)continue;
      ShaderFeatures region=SF_NONE;for(auto &n:shader_feature_names())if(std::strcmp(n.name,region_name)==0)region=n.bit;
      if(const ShaderFeatures refused=scene_regions&region)
        unsupported_feature=std::string("WebGPU: ")+text+" are not supported on "+known->device+" GPUs (this scene uses "+
          shader_feature_list(refused)+"): the shader compiler of this GPU class miscompiles or fails to link their shader programs; render the scene on another GPU or on the CPU device";
    }
    // Adreno 650 (not a reference device): a shadow catcher scene with transparent
    // shadow rays (direct light) fails there with SVM stack errors in the transparent shadow stage
    // (native/DEVICE.md, shadow catcher section); refused before any pipeline compiles.
    if(unsupported_feature.empty()&&known&&std::strcmp(known->device,"Adreno (TM) 650")==0&&data.integrator.has_shadow_catcher&&data.integrator.transparent_shadows&&data.integrator.use_direct_light)
      unsupported_feature=std::string("WebGPU: shadow catchers with transparent shadows are not supported on ")+known->device+
        " GPUs: the transparent shadow stage of such scenes fails on this GPU class; render the scene on another GPU or on the CPU device";
    // Adreno 650 (not a reference device): manifold next-event estimation renders
    // caustic scenes outside the practical tolerance on this GPU class (native/DEVICE.md,
    // manifold next-event estimation section); refused before any pipeline compiles.
    if(unsupported_feature.empty()&&known&&std::strcmp(known->device,"Adreno (TM) 650")==0&&data.integrator.use_caustics)
      unsupported_feature=std::string("WebGPU: caustics (manifold next-event estimation) are not supported on ")+known->device+
        " GPUs: caustic scenes render incorrectly on this GPU class; render the scene on another GPU or on the CPU device";
    }
  if(!unsupported_feature.empty())throw std::runtime_error(unsupported_feature);
  if(specialization_mode==SpecializationMode::Off)return SF_ALL;
  if(census_generation!=svm_generation){
    census=svm_census(svm_node_words,shader_records.size());census_generation=svm_generation;
    enabled_features|=(census.features&~SF_PRINCIPLED_ALL)|principled_features|displacement_features;
    for(const KernelLight &light:lamp_records())if(light.type==LIGHT_AREA)enabled_features|=SF_LIGHT_AREA;
    // Shader-defined volumes: shadow paths through media, and the volume direct light (volume.cpp).
    if(data.kernel_features&KERNEL_FEATURE_VOLUME){enabled_features|=SF_VOLUME_SHADOW;if(data.integrator.use_direct_light)enabled_features|=SF_VOLUME_LIGHT;
      if(data.kernel_features&KERNEL_FEATURE_LIGHT_PASSES)enabled_features|=SF_VOLUME_LIGHT_PASSES;
      if(data.film.pass_emission!=PASS_UNUSED||data.film.pass_lightgroup!=PASS_UNUSED)enabled_features|=SF_VOLUME_LIGHT_PASSES;
      if(data.integrator.has_shadow_catcher)enabled_features|=SF_VOLUME_SHADOW_CATCHER;
      if(data.integrator.use_direct_light){for(const KernelLight &light:lamp_records())if(light.type==LIGHT_SPOT||light.type==LIGHT_AREA)enabled_features|=SF_VOLUME_LIGHT_SHAPES;
        for(size_t i=0;i+1<light_distribution_records.size();i++)if(light_distribution_records[i].prim>=0)enabled_features|=SF_VOLUME_LIGHT_SHAPES;}
      for(auto &shader:shader_records)if((shader.flags&SD_HAS_VOLUME)&&(shader.flags&SD_HETEROGENEOUS_VOLUME)){enabled_features|=SF_VOLUME_HETEROGENEOUS;break;}
      for(auto &shader:shader_records)if((shader.flags&SD_HAS_VOLUME)&&(shader.flags&SD_HAS_LIGHT_PATH_NODE)){enabled_features|=SF_VOLUME_LIGHT_PATH;break;}
      if(data.integrator.volume_ray_marching)enabled_features|=SF_VOLUME_RAY_MARCHING|SF_VOLUME_HETEROGENEOUS;}
    else enabled_features&=~SF_VOLUME_EMISSION;
    // The Light Path node's path-state outputs matter only where shadow, volume or emitter evaluations run them: an emitter
    // evaluation is the shader of a lamp, a background light or an emissive triangle that is not constant (shade_light_nee,
    // the lamp hits, the importance map).
    if((enabled_features&SF_LIGHT_PATH_STATE).any()&&!(data.kernel_features&KERNEL_FEATURE_VOLUME)&&!data.integrator.transparent_shadows){
      std::vector<uint32_t> emitters;auto add=[&](uint32_t id){if(id<shader_records.size()&&!(shader_records[id].flags&SD_HAS_CONSTANT_EMISSION))emitters.push_back(id);};
      for(const KernelLight &light:lamp_records())add(uint32_t(light.shader_id)&SHADER_MASK);
      for(size_t i=0;i+1<light_distribution_records.size();i++)if(light_distribution_records[i].prim>=0&&size_t(light_distribution_records[i].prim)<tri_shader_words.size())
        add(tri_shader_words[light_distribution_records[i].prim]&SHADER_MASK);
      std::sort(emitters.begin(),emitters.end());emitters.erase(std::unique(emitters.begin(),emitters.end()),emitters.end());
      if(emitters.empty()||!(svm_census(svm_node_words,shader_records.size(),&emitters).features&SF_LIGHT_PATH_STATE).any())enabled_features&=~SF_LIGHT_PATH_STATE;}
    // The last distribution entry is the CDF end marker (prim 0), not an emitter.
    for(size_t i=0;i+1<light_distribution_records.size();i++)if(light_distribution_records[i].prim>=0)enabled_features|=SF_LIGHT_TRIANGLE;
    for(const KernelShader &shader:shader_records)if(shader.flags&(SD_MIS_FRONT|SD_MIS_BACK))enabled_features|=SF_LIGHT_TRIANGLE;
    // Background light and non-constant emitters that light sampling can reach.
    for(const KernelLight &light:lamp_records())if(light.type==LIGHT_BACKGROUND){enabled_features|=SF_LIGHT_BACKGROUND;
      const auto id=uint(light.shader_id)&SHADER_MASK;if(id<shader_records.size()&&!(shader_records[id].flags&SD_HAS_CONSTANT_EMISSION))enabled_features|=SF_LIGHT_NEE;}
    if(data.background.use_mis)enabled_features|=SF_LIGHT_BACKGROUND;
    // Background light portals (the portal records follow the lamps in the light table).
    if(data.integrator.num_portals>0)enabled_features|=SF_BACKGROUND_PORTAL;
    // Area lights with a zero axis length (native/DEVICE.md, item 148).
    for(const KernelLight &light:lamp_records())if(light.type==LIGHT_AREA&&(light.area.len_u==0||light.area.len_v==0))enabled_features|=SF_LIGHT_AREA_DEGENERATE;
    // Spot lights (spot_attenuation in the oracle's float3 order; native/DEVICE.md, item 151).
    for(const KernelLight &light:lamp_records())if(light.type==LIGHT_SPOT)enabled_features|=SF_LIGHT_SPOT;
    // Point and spot lights, and sun lights with an angle (light_cone): lamp samples in the oracle's order (native/DEVICE.md,
    // item 152).
    for(const KernelLight &light:lamp_records())if(light.type==LIGHT_POINT||light.type==LIGHT_SPOT||(light.type==LIGHT_SUN&&light.sun.one_minus_cosangle!=0.0f))enabled_features|=SF_LIGHT_LAMP_SAMPLE;
    // Point, spot, area and sun lights whose emission shader is not constant (shade_light_nee and the hit evaluations).
    for(const KernelLight &light:lamp_records())if(light.type!=LIGHT_BACKGROUND){const auto id=uint(light.shader_id)&SHADER_MASK;
      if(id<shader_records.size()&&!(shader_records[id].flags&SD_HAS_CONSTANT_EMISSION))enabled_features|=SF_LIGHT_NEE|SF_LIGHT_LAMP_SHADER;}
    for(size_t i=0;i+1<light_distribution_records.size();i++)if(light_distribution_records[i].prim>=0&&size_t(light_distribution_records[i].prim)<tri_shader_words.size()){
      const auto id=tri_shader_words[light_distribution_records[i].prim]&SHADER_MASK;if(id<shader_records.size()&&!(shader_records[id].flags&SD_HAS_CONSTANT_EMISSION))enabled_features|=SF_LIGHT_NEE;}
    for(const uint flags:object_flag_records)if(flags&SD_OBJECT_MOTION)enabled_features|=SF_MOTION_OBJECT;
    for(size_t i=0;i<object_records.size()&&i<object_flag_records.size();i++)if((object_flag_records[i]&SD_OBJECT_MOTION)&&object_records[i].primitive_type==PRIMITIVE_LAMP)enabled_features|=SF_LIGHT_MOTION|SF_MOTION_OBJECT;
    // Light tree and light linking (lighting.cpp validate_light_tree; m2-lighttree).
    if(data.integrator.use_light_tree){enabled_features|=SF_LIGHT_TREE|SF_LIGHT_SELECT;
      if((data.kernel_features&KERNEL_FEATURE_VOLUME)&&data.integrator.use_direct_light)enabled_features|=SF_VOLUME_LIGHT_TREE;
      for(const KernelLightTreeEmitter &emitter:light_tree_emitters)if(emitter.light.id>=0&&emitter.object_id!=OBJECT_NONE){enabled_features|=SF_LIGHT_TRIANGLE;
        if((data.kernel_features&KERNEL_FEATURE_VOLUME)&&data.integrator.use_direct_light)enabled_features|=SF_VOLUME_LIGHT_SHAPES;
        if(size_t(emitter.triangle.id)<tri_shader_words.size()){const auto id=tri_shader_words[emitter.triangle.id]&SHADER_MASK;
          if(id<shader_records.size()&&!(shader_records[id].flags&SD_HAS_CONSTANT_EMISSION))enabled_features|=SF_LIGHT_NEE;}}}
    if(data.kernel_features&KERNEL_FEATURE_LIGHT_LINKING)enabled_features|=SF_LIGHT_LINKING|SF_LIGHT_SELECT;
    if(data.kernel_features&KERNEL_FEATURE_SHADOW_LINKING)enabled_features|=SF_SHADOW_LINKING;
    // The Ray Portal BSDF: the portal bounces of the path and shadow states exist with KERNEL_FEATURE_NODE_PORTAL.
    if(data.kernel_features&KERNEL_FEATURE_NODE_PORTAL)enabled_features|=SF_RAY_PORTAL;
    if(data.cam.num_motion_steps!=0)enabled_features|=SF_MOTION_CAMERA;
    // Curves: the shapes of the uploaded curve records.
    for(const KernelCurve &curve:curve_records){const int shape=curve.type&PRIMITIVE_CURVE;
      enabled_features|=shape==PRIMITIVE_CURVE_RIBBON?SF_CURVE_RIBBON:shape==PRIMITIVE_CURVE_THICK?SF_CURVE_THICK:SF_CURVE_THICK_LINEAR;
      if(curve.type&PRIMITIVE_MOTION)enabled_features|=SF_CURVE_MOTION;
      if(data.integrator.transparent_shadows)enabled_features|=SF_CURVE_SHADOW;}
    // Points: any uploaded point; their motion when an object's primitive type carries it.
    if(!point_shader_words.empty())enabled_features|=SF_POINTS;
    for(const KernelObject &object:object_records)if(object.primitive_type==PRIMITIVE_MOTION_POINT)enabled_features|=SF_POINTS_MOTION;
    // Smooth-normal derivatives of motion triangles (pipelines.h SF_MOTION_NORMAL_DERIVATIVE).
    if((enabled_features&SF_TEX_COORD_DERIVATIVE).any())for(const KernelObject &object:object_records)if(object.primitive_type==PRIMITIVE_MOTION_TRIANGLE)enabled_features|=SF_MOTION_NORMAL_DERIVATIVE;
    if(data.integrator.has_shadow_catcher)enabled_features|=SF_SHADOW_CATCHER;
    if(data.kernel_features&KERNEL_FEATURE_AO)enabled_features|=SF_AO_SHADOW;
    if(data.integrator.use_caustics)enabled_features|=SF_MNEE;
    if(data.integrator.use_caustics)for(const KernelLight &light:lamp_records())if(light.use_caustics)
      enabled_features|=light.type==LIGHT_SPOT?SF_MNEE_SPOT:light.type==LIGHT_AREA?SF_MNEE_AREA:light.type==LIGHT_BACKGROUND?SF_MNEE_BACKGROUND:ShaderFeatures{};
    if(data.integrator.use_caustics&&(enabled_features&SF_BSDF_BECKMANN).any())enabled_features|=SF_MNEE_BECKMANN;
    if(data.integrator.use_caustics&&(data.kernel_features&KERNEL_FEATURE_VOLUME))enabled_features|=SF_MNEE_VOLUME;
    if(data.integrator.use_caustics&&(enabled_features&SF_BSDF_HAIR_HUANG).any())enabled_features|=SF_MNEE_HUANG;
    if(data.integrator.filter_glossy!=FLT_MAX)enabled_features|=SF_FILTER_GLOSSY;
    if(film_surface_passes())enabled_features|=SF_FILM_DATA_PASSES;
    if(film_cryptomatte())enabled_features|=SF_FILM_CRYPTOMATTE;
    if(data.kernel_features&KERNEL_FEATURE_DENOISING)enabled_features|=SF_FILM_DENOISING;
    if(film_motion_vectors())enabled_features|=SF_FILM_MOTION;
    if(film_motion_vectors())for(const uint flags:object_flag_records)if(flags&SD_OBJECT_HAS_VERTEX_MOTION)enabled_features|=SF_FILM_MOTION_DEFORM;
    // A bake of a deforming mesh: its plain triangles at the static positions (init_from_bake.h; native/DEVICE.md, item 115).
    if(data.bake.use&&(data.kernel_features&KERNEL_FEATURE_BAKING)&&data.bake.object_index>=0&&size_t(data.bake.object_index)<object_flag_records.size()&&
       (object_flag_records[size_t(data.bake.object_index)]&SD_OBJECT_HAS_VERTEX_MOTION))enabled_features|=SF_BAKE_DEFORM;
    if(data.bvh.have_motion||data.cam.num_motion_steps!=0)enabled_features|=SF_FILM_MOTION_EXACT;
    if(data.integrator.transparent_shadows&&data.bvh.have_motion)enabled_features|=SF_SHADOW_MOTION;
    if(data.integrator.use_caustics&&data.bvh.have_motion)enabled_features|=SF_MNEE_MOTION;
    // The MNEE walk's traversal with motion and curve or point leaves (pipelines.h SF_MNEE_WALK_STAGES), from the module's own
    // features: a device that renders several scenes keeps the features of the earlier ones (the MNEE image gate).
    if((enabled_features&SF_MNEE_MOTION).any()&&((enabled_features&(SF_CURVES|SF_POINTS_ALL)).any()||!curve_records.empty()||!point_shader_words.empty()))enabled_features|=SF_MNEE_WALK_STAGES;
    for(const KernelObject &object:object_records){
      if(object.shadow_terminator_shading_offset>1.0f)enabled_features|=SF_TERMINATOR_SHADING;
      // The kernel multiplier 1/(1-offset/2) above 2 (offsets in (1, 2], infinite at 2) needs the wide cosine;
      // offsets above 2 give a negative multiplier, which the kernel ignores (frequency_multiplier > 1).
      if(object.shadow_terminator_shading_offset>2.0f)enabled_features|=SF_TERMINATOR_WIDE;
      if(object.shadow_terminator_geometry_offset>0.0f&&smooth_triangles&&(data.integrator.use_direct_light||(data.kernel_features&KERNEL_FEATURE_AO)))enabled_features|=SF_TERMINATOR_GEOMETRY;
    }
    if(!curve_records.empty()&&(enabled_features&(SF_NODE_AO|SF_NODE_BEVEL|SF_NODE_RAYCAST|SF_SUBSURFACE)).any())enabled_features|=SF_CURVE_RAYTRACE;
    if((data.kernel_features&KERNEL_FEATURE_VOLUME)&&(enabled_features&SF_SUBSURFACE).any())enabled_features|=SF_SUBSURFACE_VOLUME;
    if((enabled_features&SF_SUBSURFACE).any())for(const uint flags:object_flag_records)if(flags&SD_OBJECT_HOLDOUT_MASK){enabled_features|=SF_SUBSURFACE_HOLDOUT;break;}
    if(!curve_records.empty()&&(enabled_features&SF_NODE_RAYCAST).any())enabled_features|=SF_CURVE_RAYCAST;
    for(size_t i=0;i<object_records.size()&&i<object_flag_records.size();i++)if((object_records[i].primitive_type&PRIMITIVE_CURVE)&&(object_flag_records[i]&SD_OBJECT_SHADOW_CATCHER))enabled_features|=SF_CURVE_SHADOW_CATCHER;
  }
  // The sampling pattern comes from KernelData, not from the census (kernel/sample/pattern.h).
  if(data.integrator.sampling_pattern!=SAMPLING_PATTERN_TABULATED_SOBOL)enabled_features|=SF_SAMPLING_PATTERN;
  // The volume integrators' pattern (pipelines.h SF_VOLUME_SAMPLING_PATTERN): this call's KernelData only, and only once the
  // integrator's settings are in it (Integrator::device_update sets blue_noise_sequence_length to the sample count, at least 1,
  // for Sobol-Burley; scene preparation's zero KernelData reads as Sobol-Burley with length 0).
  enabled_features&=~SF_VOLUME_SAMPLING_PATTERN;
  if((data.kernel_features&KERNEL_FEATURE_VOLUME)&&data.integrator.sampling_pattern!=SAMPLING_PATTERN_TABULATED_SOBOL&&
     (data.integrator.sampling_pattern!=SAMPLING_PATTERN_SOBOL_BURLEY||data.integrator.blue_noise_sequence_length!=0))enabled_features|=SF_VOLUME_SAMPLING_PATTERN;
  // Volumes with motion blur (pipelines.h SF_VOLUME_MOTION): this call's KernelData only, as the pattern above.
  enabled_features&=~SF_VOLUME_MOTION;
  if((data.kernel_features&KERNEL_FEATURE_VOLUME)&&data.bvh.have_motion)enabled_features|=SF_VOLUME_MOTION;
  // A local traversal from a transform-applied object (it starts at the scene root) in a scene with instance leaves
  // (pipelines.h SF_LOCAL_INSTANCE_SKIP): the shaders of the applied objects' top-level primitives, from this call's tables,
  // kept per upload.
  if((enabled_features&(SF_NODE_AO|SF_NODE_BEVEL|SF_NODE_RAYCAST)).any()&&!(enabled_features&SF_LOCAL_INSTANCE_SKIP).any()){
    if(local_skip_key[0]!=svm_generation||local_skip_key[1]!=mnee_generation){
      local_skip_key[0]=svm_generation;local_skip_key[1]=mnee_generation;local_skip=false;
      const bool leaves=std::any_of(prim_index_words.begin(),prim_index_words.end(),[](int prim){return prim<0;});
      if(leaves){
        // BVH2::pack_instances puts the top-level primitives first and appends each instanced geometry's, whose
        // prim_object is 0 ("unused for instances"). The top-level count is the applied meshes' triangles plus one
        // reference per instance; checked against the tables, else (or with curves or points) every applied object counts
        // as tracing locally.
        size_t top=size_t(std::count_if(prim_index_words.begin(),prim_index_words.end(),[](int prim){return prim<0;}));
        bool applied_any=false;
        for(size_t i=0;i<object_records.size()&&i<object_flag_records.size();i++)if(object_flag_records[i]&SD_OBJECT_TRANSFORM_APPLIED){
          applied_any=true;if(object_records[i].primitive_type&PRIMITIVE_TRIANGLE)top+=size_t(std::max(object_records[i].numprims,0));}
        bool exact=curve_records.empty()&&point_shader_words.empty()&&prim_index_words.size()==prim_object_words.size()&&top<=prim_index_words.size();
        for(size_t i=top;exact&&i<prim_index_words.size();i++)exact=prim_index_words[i]>=0&&prim_object_words[i]==0;
        if(!exact)local_skip=applied_any;
        else{
          std::vector<uint32_t> roots;
          for(size_t i=0;i<top;i++){const int prim=prim_index_words[i],object=prim_object_words[i];
            if(prim>=0&&object>=0&&size_t(object)<object_flag_records.size()&&(object_flag_records[object]&SD_OBJECT_TRANSFORM_APPLIED)&&
               size_t(prim)<tri_shader_words.size())roots.push_back(tri_shader_words[prim]&SHADER_MASK);}
          std::sort(roots.begin(),roots.end());roots.erase(std::unique(roots.begin(),roots.end()),roots.end());
          if(!roots.empty())local_skip=svm_census(svm_node_words,shader_records.size(),&roots).local_traversal;
        }
      }
    }
    if(local_skip)enabled_features|=SF_LOCAL_INSTANCE_SKIP;}
  if(data.cam.type!=CAMERA_PERSPECTIVE||data.cam.aperturesize>0)enabled_features|=SF_CAMERA_PROJECTIONS;
  if(data.cam.interocular_offset!=0.0f)enabled_features|=SF_CAMERA_STEREO|SF_CAMERA_PROJECTIONS;
  if(data.cam.type==CAMERA_PANORAMA&&((enabled_features&SF_TEX_COORD).any()||film_motion_vectors()))enabled_features|=SF_CAMERA_PANORAMA_INVERSE;
  if(specialization_mode==SpecializationMode::Superset){
    if(superset_scene!=enabled_features){
      size_t budget=superset_default_budget_bytes;
      if(const char *value=std::getenv("CODEBRIX_CYCLES_SUPERSET_BUDGET_BYTES"))budget=size_t(std::strtoull(value,nullptr,10));
      superset=superset_groups({surface_source(),background_wgsl()},enabled_features,SF_ALL&~SF_SUPERSET_SCENE_ONLY,budget);
      superset_scene=enabled_features;
    }
    // A process can render several scenes with different group counts (the
    // world gate renders 24): a group index past this scene's last group uses its
    // last group, so every requested group still compiles a superset of the scene.
    if(superset.groups.empty())return enabled_features;
    return superset.groups[std::min<size_t>(superset_group,superset.groups.size()-1)];
  }
  return enabled_features;
}
unsigned debug_output_mode(){
  static const unsigned mode=[]{
    const char *value=cbc::diagnostic_env("CODEBRIX_CYCLES_DEBUG_OUTPUT");if(!value)return 0u;
    static const char *names[]={"shading-normal","geometric-normal","svm-normal","uv","flags"};
    for(unsigned i=0;i<5;i++)if(std::strcmp(value,names[i])==0)return i+1;
    throw std::runtime_error(std::string("CODEBRIX_CYCLES_DEBUG_OUTPUT must be shading-normal, geometric-normal, svm-normal, uv or flags, not '")+value+"'");
  }();
  return mode;
}
// The diagnostic debug output region joins every module while the mode is set.
static ShaderFeatures debug_output_features(){return debug_output_mode()?SF_DEBUG_OUTPUT:SF_NONE;}
std::string WebGPUDevice::module_wgsl(const std::string &source){return specialize_wgsl(source,shader_features()|debug_output_features());}
// Driver workarounds (pipelines.h SF_DRIVER_CACHE_SALT), keyed to the adapter and
// listed in the device report.
static bool driver_workarounds_off(){
  static const bool off=[]{
    const char *value=cbc::diagnostic_env("CODEBRIX_CYCLES_DRIVER_WORKAROUNDS");if(!value)return false;
    if(std::strcmp(value,"none")!=0)throw std::runtime_error(std::string("CODEBRIX_CYCLES_DRIVER_WORKAROUNDS must be none, not '")+value+"'");
    return true;
  }();
  return off;
}
static bool intel_d3d12_cache_salt(const cbc::Device &gpu){
  return gpu.adapter_vendor_id==0x8086&&gpu.adapter_backend==WGPUBackendType_D3D12&&!driver_workarounds_off();
}
// On Intel GPUs through Metal (osx-x64), prepare_surface (and its shader-raytrace
// sibling prepare_surface_raytrace) computes wrong,
// non-repeatable values for paths that share a SIMD group whenever its workgroup
// holds more than one invocation, in motion-blur and AO-node scenes
// (docs/TESTING-MACOS-X64.md, M2); one invocation per workgroup is exact. Metal
// reports no PCI vendor ID, so the adapter is matched by name.
static bool intel_metal_surface_workgroup(const cbc::Device &gpu){
  return gpu.adapter_backend==WGPUBackendType_Metal&&gpu.adapter_device.find("Intel")!=std::string::npos&&!driver_workarounds_off();
}
// On Intel GPUs through Metal (osx-x64), the traversal entries that intersect
// thick (cylinder) curves compute wrong results whenever a workgroup holds more
// than one invocation: intersect_closest trips the kernels' validity check
// ("invalid state/data") and intersect_shadow reports every shadow ray as
// blocked, on every curves/thick and hair/*-thick oracle scene; ribbon, linear
// and triangle curves are unaffected. One invocation per workgroup in those two
// entries alone makes curves/thick-diffuse agree with the CPU within 1.9e-6
// (docs/TESTING-MACOS-X64.md, M3). The other intersect_* entries walk the same
// curve leaves, so the workaround covers every intersect_* entry of a variant
// with thick curves; variants without them compile unchanged.
static bool intel_metal_curve_workgroup(const cbc::Device &gpu,const ShaderFeatures &features){
  return intel_metal_surface_workgroup(gpu)&&(features&SF_CURVE_THICK).any();
}
static void single_invocation_entry(std::string &code,const char *entry){
  const std::string from=std::string("@workgroup_size(64) fn ")+entry+"(";const size_t at=code.find(from);
  if(at!=std::string::npos)code.replace(at,from.size(),std::string("@workgroup_size(1) fn ")+entry+"(");
}
// A constant no object index reaches (0xF0000000 to 0xFFFFFFFE, never NONE),
// different for every compile in this process and, from its random start, in
// every process.
static void salt_driver_cache(std::string &code){
  static std::atomic<uint32_t> next{[]{std::random_device random;
    return uint32_t(random())^uint32_t(std::chrono::steady_clock::now().time_since_epoch().count());}()};
  char value[16];std::snprintf(value,sizeof(value),"0x%08xu",0xF0000000u|(next.fetch_add(1)&0x0FFFFFFEu));
  for(size_t at=code.find("DRIVER_CACHE_SALT_VALUE");at!=std::string::npos;at=code.find("DRIVER_CACHE_SALT_VALUE",at))
    code.replace(at,std::strlen("DRIVER_CACHE_SALT_VALUE"),value);
}
bool WebGPUDevice::pipeline_current(const cbc::Pipeline &pipeline,const char *entry,ShaderFeatures removed){
  if(!pipeline.value)return false;
  auto it=pipeline_features.find(entry);
  return it!=pipeline_features.end()&&!((shader_features()&~removed)&~it->second).any();
}
// The raytrace surface queue holds only paths whose shader is flagged
// SD_HAS_RAYTRACE (the Ambient Occlusion node), and prepare_surface_raytrace
// evaluates only those shaders' SVM programs: the SVM node and closure regions
// that only other shaders reach are left out of that entry (scene
// specialization only; the shared light/connect/shade entries keep every
// region). An AO node scene with, for example, a Principled floor otherwise
// carries Principled in its raytrace entry (67,787 Intel instructions, above
// the 60,000 compile envelope; 1,292,392 inlined bytes, above the size guard).
// shader_eval_displace runs the displacement programs only: the scene's other SVM handlers
// (closures, the bump nodes, families) are left out of its pipeline's module, as for
// prepare_surface_raytrace, which keeps the entry inside the pipeline size guard.
ShaderFeatures WebGPUDevice::displacement_unused_svm_features(){
  const ShaderFeatures scene=shader_features();
  if(specialization_mode!=SpecializationMode::Scene)return SF_NONE;
  constexpr ShaderFeatures svm=(ShaderFeatures::range(0,64)&~ShaderFeatures::range(26,32))|SF_TEX_ENVIRONMENT|SF_TEX_IMAGE_BOX|SF_MATH_ARC|SF_VOLUME_PHASES|SF_VOLUME_PHASE_KINDS|SF_NODE_VOLUME_COEFFICIENTS|SF_NODE_PRINCIPLED_VOLUME;
  const ShaderFeatures handlers=svm|SF_BSDF_FAMILIES|SF_NODE_BEVEL|SF_NODE_RAYCAST|SF_NODE_RADIAL_TILING|SF_NODE_AO|SF_BUMP_UNDISPLACED;
  return scene&handlers&~displacement_program_features;
}
ShaderFeatures WebGPUDevice::raytrace_unused_svm_features(){
  const ShaderFeatures scene=shader_features();
  if(specialization_mode!=SpecializationMode::Scene)return SF_NONE;
  std::vector<uint32_t> roots;for(size_t s=0;s<shader_records.size();s++)if(shader_records[s].flags&SD_HAS_RAYTRACE)roots.push_back(uint32_t(s));
  if(roots.empty())return SF_NONE;
  const SvmCensus used=svm_census(svm_node_words,shader_records.size(),&roots);
  // Census-only bits: the SVM node and closure families (lamp, motion,
  // terminator and light-sampling bits are set from the scene tables).
  constexpr ShaderFeatures svm=(ShaderFeatures::range(0,64)&~ShaderFeatures::range(26,32))|SF_TEX_ENVIRONMENT|SF_TEX_IMAGE_BOX|SF_MATH_ARC|SF_VOLUME_PHASES|SF_VOLUME_PHASE_KINDS|SF_NODE_VOLUME_COEFFICIENTS|SF_NODE_PRINCIPLED_VOLUME;
  return scene&svm&~used.features;
}
// prepare_surface evaluates the SVM programs of the hit triangles' shaders only (tri_shader): the SVM node regions
// that only other shaders reach (the world, lamps) are left out of that entry (scene specialization only), as for
// prepare_surface_raytrace. A world Environment Texture otherwise carries the equirectangular projection's arc
// adapters into the bench HDRI scene's prepare_surface (1,113,668 inlined bytes, above the size guard).
ShaderFeatures WebGPUDevice::surface_unused_svm_features(){
  const ShaderFeatures scene=shader_features();
  if(specialization_mode!=SpecializationMode::Scene)return SF_NONE;
  if(surface_census_key==svm_generation)return scene&surface_unused;
  surface_census_key=svm_generation;surface_unused=SF_NONE;
  std::vector<uint32_t> roots;for(uint32_t s:tri_shader_words)roots.push_back(s&SHADER_MASK);
  for(const KernelCurve &curve:curve_records)roots.push_back(uint32_t(curve.shader_id)&SHADER_MASK);
  for(const uint32_t word:point_shader_words)roots.push_back(word&SHADER_MASK);
  std::sort(roots.begin(),roots.end());roots.erase(std::unique(roots.begin(),roots.end()),roots.end());
  if(roots.empty())return SF_NONE;
  const SvmCensus used=svm_census(svm_node_words,shader_records.size(),&roots);
  // Census-only bits, as for prepare_surface_raytrace: the SVM node families (lamp, motion, terminator and
  // light-sampling bits are set from the scene tables and stay).
  constexpr ShaderFeatures svm=(ShaderFeatures::range(0,64)&~ShaderFeatures::range(26,32))|SF_TEX_ENVIRONMENT|SF_TEX_IMAGE_BOX|SF_MATH_ARC;
  surface_unused=svm&~used.features;
  return scene&surface_unused;
}
// The MNEE walk evaluates SVM programs only at caustic casters: mnee_eval at the caster hits of the seed's probe
// ray and mnee_contribute at the solution vertices, both on caster triangles. The SVM node and closure regions
// that no caster shader reaches are left out of the INTERSECT_MNEE module (scene specialization only), as for
// prepare_surface_raytrace; a removed handler's fallback reports error bit 2, so a missed shader is refused, never
// rendered differently. Casters are found through the top-level BVH primitives (prim_object, prim_index) of
// objects with an applied transform; a scene with an instanced caster keeps every region.
ShaderFeatures WebGPUDevice::mnee_unused_svm_features(){
  const ShaderFeatures scene=shader_features();
  if(specialization_mode!=SpecializationMode::Scene)return SF_NONE;
  if(mnee_census_key[0]==svm_generation&&mnee_census_key[1]==mnee_generation)return scene&mnee_unused;
  mnee_census_key[0]=svm_generation;mnee_census_key[1]=mnee_generation;mnee_unused=SF_NONE;
  bool casters=false,instanced=false;
  for(const uint flags:object_flag_records)if(flags&SD_OBJECT_CAUSTICS_CASTER){casters=true;instanced|=!(flags&SD_OBJECT_TRANSFORM_APPLIED);}
  if(!casters||instanced||prim_index_words.size()!=prim_object_words.size())return SF_NONE;
  std::vector<uint32_t> roots;
  for(size_t i=0;i<prim_index_words.size();i++){const int prim=prim_index_words[i],object=prim_object_words[i];
    if(prim<0||object<0||size_t(object)>=object_flag_records.size()||!(object_flag_records[object]&SD_OBJECT_CAUSTICS_CASTER))continue;
    if(size_t(prim)>=tri_shader_words.size())return SF_NONE;
    roots.push_back(tri_shader_words[prim]&SHADER_MASK);}
  if(roots.empty())return SF_NONE;
  std::sort(roots.begin(),roots.end());roots.erase(std::unique(roots.begin(),roots.end()),roots.end());
  const SvmCensus used=svm_census(svm_node_words,shader_records.size(),&roots);
  // Census-only bits: the SVM node and closure handlers (lamp, motion, terminator, light-sampling, displacement and
  // film bits are set from the scene tables and stay).
  constexpr ShaderFeatures svm=(ShaderFeatures::range(0,64)&~ShaderFeatures::range(26,32))|SF_TEX_ENVIRONMENT|SF_TEX_IMAGE_BOX|SF_MATH_ARC|
    SF_BSDF_FAMILIES|SF_NODE_BEVEL|SF_NODE_RAYCAST|SF_NODE_RADIAL_TILING|SF_NODE_AO|SF_NODE_IES|SF_LIGHT_FALLOFF|SF_BSDF_THIN_FILM_CONDUCTOR|SF_TEX_SKY|SF_TEX_SKY_SCATTERING|SF_VOLUME_PHASES|SF_VOLUME_PHASE_KINDS|SF_NODE_VOLUME_COEFFICIENTS|SF_NODE_PRINCIPLED_VOLUME;
  mnee_unused=svm&~used.features;
  return scene&mnee_unused;
}
namespace {
struct ProcessPipelines {std::mutex mutex;uint64_t count=0;double max_ms=0,total_ms=0;std::string max_entry;bool conservative=false;std::vector<std::string> warnings;std::string decision;
  // Size guard (pipelines.h): warnings kept apart from the latency warnings so "degraded" keeps its meaning.
  std::vector<std::string> size_warnings;size_t max_inlined_bytes=0;std::string max_inlined_entry;};
ProcessPipelines &process_pipelines(){static ProcessPipelines state;return state;}
}
void pipeline_process_decision(const std::string &json){
  auto &process=process_pipelines();std::lock_guard lock(process.mutex);process.decision=json;
}
void pipeline_process_warning(const std::string &message){
  auto &process=process_pipelines();std::lock_guard lock(process.mutex);process.warnings.push_back(message);
}
uint32_t WebGPUDevice::workgroup(const char *entry) const{
  auto it=pipeline_workgroups.find(entry);if(it==pipeline_workgroups.end())throw std::runtime_error(std::string("No compiled workgroup size for ")+entry);
  return it->second;
}
// Fingerprint mode (fingerprint.h): the text this entry would compile is hashed. Outside a scene-preparation kernel the entry
// gets an empty stand-in pipeline, a real object so that pipeline state stays as a compile leaves it. False: not compiled.
bool WebGPUDevice::fingerprint_module(const char *module,const char *entry,ShaderFeatures features,const std::string &code,std::vector<cbc::Pipeline> &out){
  const uint32_t size=declared_workgroup_size(code,entry);if(!size)throw std::runtime_error(std::string("No workgroup size declared for ")+entry);
  fingerprint->module(module,entry,size,code,shader_feature_list(features));
  if(fingerprint->kind==WebGPUFingerprint::Prefix){fingerprint->finish(entry);std::_Exit(0);}
  if(fingerprint->preparing)return true;
  if(!fingerprint_stub.value){
    static const char stub[]="@compute @workgroup_size(1) fn fingerprint_stub(){}\n";
    WGPUShaderSourceWGSL wgsl{};wgsl.chain.sType=WGPUSType_ShaderSourceWGSL;wgsl.code={stub,sizeof(stub)-1};
    WGPUShaderModuleDescriptor sd{};sd.nextInChain=&wgsl.chain;fingerprint_stub=cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease>(wgpuDeviceCreateShaderModule(gpu.get(),&sd));
  }
  out.push_back(gpu.pipeline(fingerprint_stub,pipeline_layout,"fingerprint_stub"));
  fingerprint->names[out.back().value]=entry;
  pipeline_records.push_back({module,entry,features,code.size(),0,0,0});pipeline_features[entry]=features;pipeline_workgroups[entry]=size;
  return false;
}
std::vector<cbc::Pipeline> WebGPUDevice::build_pipelines(const char *module,const std::string &source,std::initializer_list<const char*> entries,ShaderFeatures extra,ShaderFeatures removed){
  const ShaderFeatures features=(shader_features()&~removed)|extra|debug_output_features()|(intel_d3d12_cache_salt(gpu)?SF_DRIVER_CACHE_SALT:SF_NONE);
  const std::string specialized=specialize_wgsl(source,features);
  using clock=std::chrono::steady_clock;auto ms=[](clock::time_point a,clock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();};
  auto warn=[&](const std::string &message){
    pipeline_warnings.push_back(message);std::fprintf(stderr,"CodeBrix Cycles WebGPU warning: %s\n",message.c_str());
    auto &process=process_pipelines();std::lock_guard lock(process.mutex);process.warnings.push_back(message);
  };
  std::vector<cbc::Pipeline> out;
  // Size guard (pipelines.h): estimated from the WGSL before the driver sees it,
  // so the warning is on stderr even if the compile then crashes the process.
  const std::vector<size_t> inlined=wgsl_inlined_bytes(specialized,std::vector<std::string>(entries.begin(),entries.end()));
  // native/DEVICE.md 107: an entry with two inlined copies of the background light's portal sampler computes wrong light
  // samples on Mesa's Intel driver. Every entry is built with at most one; checked here, before any driver sees the text.
  {const std::vector<size_t> copies=wgsl_inlined_copies(specialized,std::vector<std::string>(entries.begin(),entries.end()),portal_sampler_function);
   size_t index=0;for(const char *entry:entries){const size_t n=copies[index++];
     if(n>1)throw std::runtime_error(portal_sampler_copies_error(module,entry,n));}}
  {size_t index=0;
   for(const char *entry:entries){
     const size_t bytes=inlined[index++];
     if(bytes>max_inlined_bytes){max_inlined_bytes=bytes;max_inlined_entry=entry;}
     auto &process=process_pipelines();std::lock_guard lock(process.mutex);
     if(bytes>process.max_inlined_bytes){process.max_inlined_bytes=bytes;process.max_inlined_entry=entry;}
     if(size_warning_bytes==0||bytes<size_warning_bytes)continue;
     const std::string message=pipeline_size_warning(module,entry,bytes,size_warning_bytes);
     if(std::find(size_warnings.begin(),size_warnings.end(),message)!=size_warnings.end())continue;
     size_warnings.push_back(message);std::fprintf(stderr,"CodeBrix Cycles WebGPU warning: %s\n",message.c_str());
     if(std::find(process.size_warnings.begin(),process.size_warnings.end(),message)==process.size_warnings.end())process.size_warnings.push_back(message);
   }}
  // Optional capture of every module as compiled (maintainer measurements, for
  // example native/tools/intel_compile_envelope.py): CODEBRIX_CYCLES_WGSL_DUMP=DIR
  // writes DIR/<sequence>-<module>.wgsl before the entries are created.
  if(const char *dir=std::getenv("CODEBRIX_CYCLES_WGSL_DUMP");dir&&*dir){
    static std::atomic<unsigned> sequence{0};char name[64];std::snprintf(name,sizeof(name),"/%03u-%s.wgsl",sequence++,module);
    std::ofstream(std::string(dir)+name)<<(conservative?conservative_wgsl(specialized):specialized);
    std::snprintf(name,sizeof(name),"/source-%s.wgsl",module);std::ofstream(std::string(dir)+name)<<source;
  }
  // Diagnostic WGSL override (CODEBRIX_CYCLES_WGSL_LOAD=DIR). One module name is
  // compiled in several variants (entry sets, features, tiers), so overrides are
  // keyed by the generated source: <key> is <module>-<first 16 hex digits of the
  // SHA-256 of the WGSL that would be compiled>. A variant without an override
  // writes DIR/<key>.generated.wgsl (once); copying that file to DIR/<key>.wgsl
  // and editing it makes exactly that variant compile the edited text. The
  // override's SHA-256 is noted as an active diagnostic.
  std::string override_code;
  if(const char *dir=cbc::diagnostic_env("CODEBRIX_CYCLES_WGSL_LOAD")){
    const std::string generated=conservative?conservative_wgsl(specialized):specialized;
    const std::string key=std::string(module)+"-"+oracle::sha256(generated.data(),generated.size()).substr(0,16);
    std::ifstream file(std::string(dir)+"/"+key+".wgsl",std::ios::binary);
    if(file){std::ostringstream text;text<<file.rdbuf();override_code=text.str();
      cbc::note_diagnostic("WGSL override "+key+".wgsl sha256="+oracle::sha256(override_code.data(),override_code.size()));
      warn("WGSL override loaded for "+key+" (diagnostic, not a validation run)");}
    else{const std::string path=std::string(dir)+"/"+key+".generated.wgsl";
      if(!std::ifstream(path))std::ofstream(path,std::ios::binary)<<generated;}
  }
  for(const char *entry:entries){
    const size_t entry_inlined=inlined[size_t(std::find(entries.begin(),entries.end(),entry)-entries.begin())];
    for(int attempt=0;;attempt++){
      std::string code=!override_code.empty()?override_code:conservative?conservative_wgsl(specialized):specialized;
      if((features&SF_DRIVER_CACHE_SALT).any())salt_driver_cache(code);
      if(override_code.empty()&&(std::strcmp(entry,"prepare_surface")==0||std::strcmp(entry,"prepare_surface_raytrace")==0)&&intel_metal_surface_workgroup(gpu))
        single_invocation_entry(code,entry);
      if(override_code.empty()&&std::strncmp(entry,"intersect_",10)==0&&intel_metal_curve_workgroup(gpu,features))
        single_invocation_entry(code,entry);
      // Heterogeneous volumes on Intel GPUs through Metal: shade_volume_heterogeneous
      // trips the kernels' validity check ("invalid state/data") with more than one
      // invocation per workgroup; one invocation is exact (volumes/heterogeneous-point
      // within 1.3e-7 of the CPU; docs/TESTING-MACOS-X64.md, M4).
      // The integration's shader evaluations run in shade_volume_heterogeneous_eval (native/DEVICE.md 52), so it is built so too.
      if(override_code.empty()&&(std::strcmp(entry,"shade_volume_heterogeneous")==0||std::strcmp(entry,"shade_volume_heterogeneous_eval")==0)&&intel_metal_surface_workgroup(gpu))
        single_invocation_entry(code,entry);
      if(fingerprint&&!fingerprint_module(module,entry,features,code,out))break;
      const auto t0=clock::now();
      WGPUShaderSourceWGSL wgsl{};wgsl.chain.sType=WGPUSType_ShaderSourceWGSL;wgsl.code={code.data(),code.size()};
      WGPUShaderModuleDescriptor sd{};sd.nextInChain=&wgsl.chain;
      // Pending breadcrumb for the persisted device record (device_tier.h).
      tier_policy.begin_pipeline(module,entry,conservative);gpu.push_scope();
      cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> shader(wgpuDeviceCreateShaderModule(gpu.get(),&sd));
      try{gpu.pop_scope();}catch(const std::exception &e){tier_policy.abandon_pipeline();throw std::runtime_error(std::string("WebGPU shader module '")+module+"' rejected: "+e.what());}
      const double module_ms=ms(t0,clock::now());const auto t1=clock::now();
      PipelineDiskCache::Use cached(pipeline_disk_cache,code,entry);
      try{out.push_back(gpu.pipeline(shader,pipeline_layout,entry,nullptr,tier_policy.pending_descriptor(),cached.cache(),cached.file()));}
      catch(const std::exception &e){
        // A lost device cannot compile a retry; other driver failures (for
        // example out-of-memory reported through an error scope) retry once in
        // the conservative tier. The stage name is always reported, and the
        // failure is recorded so later runs start conservative.
        const std::string message=e.what();tier_policy.end_pipeline(false,ms(t1,clock::now()),message);
        if(conservative||attempt>0||message.find("lost")!=std::string::npos){
          pipeline_process_decision(tier_policy.report_json());
          throw std::runtime_error("WebGPU "+std::string(module)+" pipeline '"+entry+"' could not be created ("+(conservative?"conservative":"full")+" tier): "+message);
        }
        conservative=true;tier_policy.switch_conservative("failure",std::string("pipeline '")+entry+"' failed in the full tier: "+message);
        warn(std::string("pipeline '")+entry+"' failed in the full tier ("+message+"); retrying in the conservative tier (workgroup size 1; results may differ in the last bits but must meet the practical criterion)");
        pipeline_process_decision(tier_policy.report_json());
        continue;
      }
      const double pipeline_ms=ms(t1,clock::now());tier_policy.end_pipeline(true,pipeline_ms,"");cached.compiled();
      pipeline_records.push_back({module,entry,features,code.size(),entry_inlined,module_ms,pipeline_ms});pipeline_features[entry]=features;
      if(fingerprint){fingerprint->names[out.back().value]=entry;fingerprint->compiled++;}
      const uint32_t size=declared_workgroup_size(code,entry);if(!size)throw std::runtime_error(std::string("No workgroup size declared for ")+entry);
      pipeline_workgroups[entry]=size;
      {auto &process=process_pipelines();std::lock_guard lock(process.mutex);process.count++;process.total_ms+=pipeline_ms;
       if(pipeline_ms>process.max_ms){process.max_ms=pipeline_ms;process.max_entry=entry;}process.conservative|=conservative;}
      // Optional JSON-lines log for measurements and gates (one object per pipeline).
      if(const char *path=std::getenv("CODEBRIX_CYCLES_PIPELINE_LOG")){
        std::ofstream log(path,std::ios::app);const auto &r=pipeline_records.back();
        log<<"{\"module\":\""<<r.module<<"\",\"entry\":\""<<r.entry<<"\",\"specialized\":"<<(specialization_enabled?"true":"false")
           <<",\"specializationMode\":\""<<specialization_mode_name(specialization_mode)<<"\""<<superset_json()
           <<",\"tier\":\""<<(conservative?"conservative":"full")<<"\",\"tierReason\":\""<<json_text(tier_policy.reason)<<"\",\"workgroupSize\":"<<size
           <<",\"features\":\""<<shader_feature_list(r.features)<<"\",\"wgslBytes\":"<<r.wgsl_bytes<<",\"inlinedBytes\":"<<r.inlined_bytes<<",\"moduleMs\":"<<r.module_ms
           <<",\"pipelineMs\":"<<r.pipeline_ms<<",\"cacheHit\":"<<(cached.hit?"true":"false")<<",\"adapter\":\""<<json_text(gpu.adapter_name)<<"\"}\n";
      }
      if(pipeline_budget_ms>0&&pipeline_ms>pipeline_budget_ms){
        char text[256];std::snprintf(text,sizeof(text),"pipeline '%s' took %.0f ms, over the %.0f ms compile budget",entry,pipeline_ms,pipeline_budget_ms);
        // By default latency only: the variant never switches on timing, so
        // results cannot depend on machine load. An old-hardware deployment may
        // opt in (budget_conservative) to compile later pipelines conservatively.
        if(budget_conservative&&!conservative){conservative=true;tier_policy.switch_conservative("budget-opt-in",text);pipeline_process_decision(tier_policy.report_json());
          warn(std::string(text)+"; later pipelines use the conservative tier (workgroup size 1; results may differ in the last bits but must meet the practical criterion)");}
        else warn(std::string(text)+" ("+(conservative?"conservative":"full")+" tier); first-use latency is degraded on this driver");
      }
      break;
    }
  }
  return out;
}
static std::string json_list(const std::vector<std::string> &items){
  std::string out="[";for(size_t i=0;i<items.size();i++){if(i)out+=',';out+='"'+json_text(items[i])+'"';}return out+"]";
}
static std::vector<std::string> all_warnings(const std::vector<std::string> &latency,const std::vector<std::string> &size){
  std::vector<std::string> out=latency;out.insert(out.end(),size.begin(),size.end());return out;
}
// "sizeGuard": threshold (0 = disabled), the largest inlined entry compiled so far and the warning count.
static std::string size_guard_json(size_t threshold,size_t largest,const std::string &entry,size_t warnings){
  std::ostringstream o;o<<"{\"thresholdBytes\":"<<threshold<<",\"largestEntryBytes\":"<<largest<<",\"largestEntry\":\""<<json_text(entry)
    <<"\",\"estimatedInstructions\":"<<uint64_t(double(largest)/pipeline_size_bytes_per_instruction_central+0.5)<<",\"warnings\":"<<warnings<<'}';
  return o.str();
}
std::string WebGPUDevice::superset_json() const{
  if(specialization_mode!=SpecializationMode::Superset||superset_scene==~SF_NONE)return "";
  const size_t used=superset.groups.empty()?0:std::min<size_t>(superset_group,superset.groups.size()-1);
  std::ostringstream o;o<<",\"supersetGroup\":"<<superset_group<<",\"supersetGroups\":"<<superset.groups.size()
    <<",\"supersetGroupUsed\":"<<used<<",\"supersetSceneBytes\":"<<superset.scene_bytes<<",\"supersetGroupBytes\":"<<(used<superset.group_bytes.size()?superset.group_bytes[used]:superset.scene_bytes)
    <<",\"supersetGroupAdded\":\""<<shader_feature_list(used<superset.groups.size()?superset.groups[used]&~superset_scene:SF_NONE)
    <<"\",\"supersetUnfit\":\""<<shader_feature_list(superset.unfit)<<'"';
  return o.str();
}
std::string WebGPUDevice::pipeline_report_json() const{
  std::ostringstream o;o<<"{\"specialization\":"<<(specialization_enabled?"true":"false")<<",\"specializationMode\":\""<<specialization_mode_name(specialization_mode)<<"\""<<superset_json()<<",\"tier\":\""<<(conservative?"conservative":"full")
    <<"\",\"degraded\":"<<(pipeline_warnings.empty()?"false":"true")<<",\"budgetMs\":"<<pipeline_budget_ms<<",\"warnings\":"<<json_list(all_warnings(pipeline_warnings,size_warnings))
    <<",\"sizeGuard\":"<<size_guard_json(size_warning_bytes,max_inlined_bytes,max_inlined_entry,size_warnings.size())
    <<",\"enabledFeatures\":\""<<shader_feature_list(enabled_features)<<"\",\"svmDecodedNodes\":"<<census.decoded_nodes
    <<",\"svmStoppedPaths\":"<<census.stopped_paths<<",\"budgetSource\":\""<<json_text(tier_policy.budget_source)<<"\",\"decision\":"<<tier_policy.report_json()<<",\"pipelines\":[";
  for(size_t i=0;i<pipeline_records.size();i++){const auto &r=pipeline_records[i];if(i)o<<',';
    o<<"{\"module\":\""<<r.module<<"\",\"entry\":\""<<r.entry<<"\",\"wgslBytes\":"<<r.wgsl_bytes<<",\"inlinedBytes\":"<<r.inlined_bytes<<",\"moduleMs\":"<<r.module_ms<<",\"pipelineMs\":"<<r.pipeline_ms<<'}';}
  o<<"]}";return o.str();
}
// The WebGPU adapter and backend the device runs on (webgpu.h WGPUBackendType),
// the runtime library loaded and, on Windows Direct3D 12, the DXC compiler.
std::string WebGPUDevice::adapter_report_json() const{
  static const char *backends[]={"Undefined","Null","WebGPU","D3D11","D3D12","Metal","Vulkan","OpenGL","OpenGLES"};
  const uint32_t b=gpu.adapter_backend;std::ostringstream o;
  o<<"{\"backendType\":"<<b<<",\"backendName\":\""<<(b<9?backends[b]:"unknown")<<"\",\"adapterType\":"<<gpu.adapter_type
   <<",\"device\":\""<<json_text(gpu.adapter_device)<<"\",\"vendorId\":"<<gpu.adapter_vendor_id<<",\"deviceId\":"<<gpu.adapter_device_id
   <<",\"runtimeLibrary\":\""<<json_text(gpu.library_path)<<"\",\"dxcPath\":"<<(gpu.dxc_path.empty()?std::string("null"):"\""+json_text(gpu.dxc_path)+"\"")
   // A layered adapter (Vulkan on Direct3D 12 through Mesa Dozen) is never native evidence.
   <<",\"layeredOn\":"<<(gpu.layered_on.empty()?std::string("null"):"\""+json_text(gpu.layered_on)+"\"")
   <<",\"metalPreciseMath\":"<<(gpu.metal_precise_math?"true":"false")
   <<",\"driverWorkarounds\":["<<(intel_d3d12_cache_salt(gpu)?"\"intel-d3d12-shader-cache\"":"")
   // intel-metal-curve-workgroup applies only to variants with thick curves.
   <<(intel_metal_surface_workgroup(gpu)?"\"intel-metal-surface-workgroup\",\"intel-metal-curve-workgroup\",\"intel-metal-volume-workgroup\"":"")<<"],\"adapters\":[";
  for(size_t i=0;i<gpu.adapters_seen.size();i++)o<<(i?",":"")<<gpu.adapters_seen[i];
  // Opt-in diagnostics active in this process (webgpu_device.h): a run with any
  // listed is diagnostic, never validation.
  const std::vector<std::string> active=cbc::diagnostics();o<<"],\"diagnostic\":"<<(active.empty()?"false":"true")<<",\"diagnostics\":[";
  for(size_t i=0;i<active.size();i++)o<<(i?",":"")<<'"'<<json_text(active[i])<<'"';
  o<<"]}";
  return o.str();
}
std::string pipeline_process_report_json(){
  auto &process=process_pipelines();std::lock_guard lock(process.mutex);std::ostringstream o;
  o<<"{\"tier\":\""<<(process.conservative?"conservative":"full")<<"\",\"degraded\":"<<(process.warnings.empty()?"false":"true")
   <<",\"pipelines\":"<<process.count<<",\"totalPipelineMs\":"<<process.total_ms<<",\"maxPipelineMs\":"<<process.max_ms
   <<",\"maxPipelineEntry\":\""<<process.max_entry<<"\",\"warnings\":"<<json_list(all_warnings(process.warnings,process.size_warnings))
   <<",\"sizeGuard\":"<<size_guard_json(pipeline_size_warning_bytes(),process.max_inlined_bytes,process.max_inlined_entry,process.size_warnings.size())
   <<",\"decision\":"<<(process.decision.empty()?std::string("null"):process.decision)<<'}';
  return o.str();
}
}
