// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <string>
namespace ccl {std::string wavefront_layout();std::string svm_layout(bool surface=false);
std::string shader_data_layout();std::string shader_data_source();std::string svm_surface_source();}
// A descriptor contains a native virtual address and an element count. The
// original arrays are never repacked or converted to synthetic scene records.
#define CBC_INTERSECTION_TABLES(X) \
  X(bvh_nodes,int4) X(bvh_leaf_nodes,int4) X(object_node,int) \
  X(prim_index,int) X(prim_object,int) X(prim_visibility,uint) \
  X(tri_vindex,packed_uint3) X(tri_verts,packed_float3) X(tri_shader,uint) \
  X(objects,KernelObject) X(object_flag,uint) X(shaders,KernelShader)
#define CBC_SCENE_TABLES(X) CBC_INTERSECTION_TABLES(X) X(attributes_normal,packed_normal)
