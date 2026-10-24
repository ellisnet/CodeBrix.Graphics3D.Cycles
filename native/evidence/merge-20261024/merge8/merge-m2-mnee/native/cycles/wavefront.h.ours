// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string>
namespace ccl {inline constexpr unsigned surface_data_cache_bytes=60*4;
std::string volume_source();std::string volume_stack_source();std::string wavefront_layout();std::string svm_layout(bool surface=false);
std::string shadow_source();std::string shadow_catcher_layout();std::string shadow_catcher_split_wgsl();int shadow_catcher_pass_mask();
std::string shadow_layout();std::string lighting_layout();
// Generated arithmetic-adapter constants (CAMERA_NATIVE_VECTOR_DIVIDE,
// NATIVE_FLOAT3_RECIPROCAL_NORMALIZE, NATIVE_TRIANGLE_ARITHMETIC,
// GGX_NATIVE_RECIPROCAL_NORMALIZE, TRIANGLE_LIGHT_NATIVE_SSE) make the WGSL
// reproduce the rounding of the CPU oracle the GPU is judged against: true
// selects the x86 SSE4.2 oracle. An SSE build always does; an Android scalar
// (ARM64) build does when CODEBRIX_CYCLES_REFERENCE_ORACLE=linux-x64-sse4.2,
// which the gates judged only against the correctly rounded reference computed
// on Linux x64 from the device's own scene set (reference_policy.h
// use_reference_oracle_arithmetic; native/DEVICE.md "Validation reference
// policy"). Otherwise it keeps its own scalar oracle (local CPU checks).
bool reference_oracle_sse();
std::string shader_data_layout();std::string shader_data_source();std::string svm_surface_source();std::string background_layout();std::string surface_source();std::string camera_wgsl();std::string closest_wgsl();std::string background_wgsl();}
// A descriptor contains a native virtual address and an element count. The
// original arrays are never repacked or converted to synthetic scene records.
#define CBC_INTERSECTION_TABLES(X) \
  X(bvh_nodes,int4) X(bvh_leaf_nodes,int4) X(object_node,int) \
  X(prim_index,int) X(prim_object,int) X(prim_visibility,uint) \
  X(tri_vindex,packed_uint3) X(tri_verts,packed_float3) X(tri_shader,uint) \
  X(objects,KernelObject) X(object_flag,uint) X(shaders,KernelShader)
#define CBC_SCENE_CORE_TABLES(X) CBC_INTERSECTION_TABLES(X) X(attributes_normal,packed_normal) \
  X(attributes_map,AttributeMap) X(attributes_float,float) X(attributes_float2,float2) \
  X(attributes_float3,packed_float3) X(attributes_float4,float4) X(attributes_uchar4,uchar4) \
  X(image_textures,KernelImageTexture) X(image_texture_udims,KernelImageUDIM) X(lookup_table,float) \
  X(lights,KernelLight) X(light_distribution,KernelLightDistribution) X(camera_motion,DecomposedTransform) X(object_motion,DecomposedTransform) \
  X(light_background_marginal_cdf,float2) X(light_background_conditional_cdf,float2)
// Light tree (integrator.use_light_tree): the unmodified LightManager::device_update_tree
// arrays plus object_prim_offset (forward triangle MIS). Their GPU descriptors follow
// CBC_DEVICE_TABLES, so every earlier table index is unchanged; CPU references and the
// scene exchange see them as scene tables.
#define CBC_LIGHT_TREE_TABLES(X) X(light_tree_nodes,KernelLightTreeNode) X(light_tree_emitters,KernelLightTreeEmitter) \
  X(light_to_tree,uint) X(object_lookup_offset,uint) X(triangle_to_tree,uint) X(object_prim_offset,uint)
#define CBC_SCENE_TABLES(X) CBC_SCENE_CORE_TABLES(X) CBC_LIGHT_TREE_TABLES(X)

#define CBC_DEVICE_TABLES(X) X(image_info,KernelImageInfo) X(image_bounds,uint2)
