// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "kernel/integrator/state.h"
#include <vector>
// Probe output only. Inputs remain Cycles' original native scene/state tables.
#define CBC_SHADER_FLOAT_FIELDS(F) \
 F(packed_float3,P,P) F(packed_float3,N,N) F(packed_float3,Ng,Ng) F(packed_float3,wi,wi) \
 F(packed_float3,dPdu,dPdu) F(packed_float3,dPdv,dPdv) F(float2,du,du) F(float2,dv,dv) \
 F(float,u,u) F(float,v,v) F(float,time,time) F(float,ray_length,ray_length) F(float,dP,dP) F(float,dI,dI)
#define CBC_SHADER_INT_FIELDS(F) \
 F(uint,object,object_id) F(uint,prim,prim) F(uint,type,primitive_type) F(uint,shader,shader) \
 F(uint,runtime_flag,runtime_flag) F(uint,shader_flag,shader_flag) F(uint,object_flag,object_flag)
namespace ccl {class DeviceScene;
struct ShaderDataRecord {
#define FIELD(type,name,wgsl) type name;
  CBC_SHADER_FLOAT_FIELDS(FIELD)
  CBC_SHADER_INT_FIELDS(FIELD)
#undef FIELD
};
}
std::vector<ccl::ShaderDataRecord> original_shader_data(ccl::DeviceScene&,
    const std::vector<int>&,const std::vector<ccl::IntegratorStateCPU>&);
