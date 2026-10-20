// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string>
namespace ccl {inline constexpr unsigned surface_data_cache_bytes=60*4;
std::string volume_source();std::string volume_stack_source();std::string wavefront_layout();std::string svm_layout(bool surface=false);
std::string shadow_source();
std::string shadow_layout();std::string lighting_layout();
std::string shader_data_layout();std::string shader_data_source();std::string svm_surface_source();std::string background_layout();std::string surface_source();std::string camera_wgsl();std::string closest_wgsl();std::string background_wgsl();}
// A descriptor contains a native virtual address and an element count. The
// original arrays are never repacked or converted to synthetic scene records.
#define CBC_INTERSECTION_TABLES(X) \
  X(bvh_nodes,int4) X(bvh_leaf_nodes,int4) X(object_node,int) \
  X(prim_index,int) X(prim_object,int) X(prim_visibility,uint) \
  X(tri_vindex,packed_uint3) X(tri_verts,packed_float3) X(tri_shader,uint) \
  X(objects,KernelObject) X(object_flag,uint) X(shaders,KernelShader)
#define CBC_SCENE_TABLES(X) CBC_INTERSECTION_TABLES(X) X(attributes_normal,packed_normal) \
  X(attributes_map,AttributeMap) X(attributes_float,float) X(attributes_float2,float2) \
  X(attributes_float3,packed_float3) X(attributes_float4,float4) X(attributes_uchar4,uchar4) \
  X(image_textures,KernelImageTexture) X(image_texture_udims,KernelImageUDIM) X(lookup_table,float) \
  X(lights,KernelLight) X(light_distribution,KernelLightDistribution) X(camera_motion,DecomposedTransform) X(object_motion,DecomposedTransform)

#define CBC_DEVICE_TABLES(X) X(image_info,KernelImageInfo) X(image_bounds,uint2)
