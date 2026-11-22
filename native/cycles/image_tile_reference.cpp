// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "image_reference.h"
#include "image_tile_reference.h"
#include "kernel/globals.h"
#include "kernel/util/image.h"
#include "kernel/svm/image.h"
// Runs lookup(kg) with the scene's tables, the given tile descriptors and access state, and a tile loader that throws.
template<class Lookup> static void with_tile_globals(ccl::DeviceScene &scene,std::vector<ccl::KernelTileDescriptor> &descriptors,
                                                     std::vector<uint8_t> &access,const Lookup &lookup){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;auto info=original_image_info(scene);
  base.image_info.data=info.data();base.image_info.width=info.size();
  base.image_textures.data=scene.image_textures.data();base.image_textures.width=scene.image_textures.size();
  base.image_texture_udims.data=scene.image_texture_udims.data();base.image_texture_udims.width=scene.image_texture_udims.size();
  base.image_texture_tile_descriptors.data=descriptors.data();base.image_texture_tile_descriptors.width=descriptors.size();
  base.image_texture_tile_access_state.data=access.data();base.image_texture_tile_access_state.width=access.size();
  base.image_load_requested_cpu=[](size_t image,int level,int x,int y,KernelTileDescriptor &){
    throw std::runtime_error("The CPU reference asked for a tile the device did not load: image "+std::to_string(image)+" level "+std::to_string(level)+
                             " x "+std::to_string(x)+" y "+std::to_string(y));};
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);lookup(KernelGlobals(&thread));
}
std::vector<ImageTileResult> original_image_tile_samples(ccl::DeviceScene &scene,const std::vector<ImageTileProbe> &probes,
                                                         std::vector<ccl::KernelTileDescriptor> &descriptors,std::vector<uint8_t> &access){
  using namespace ccl;std::vector<ImageTileResult> out;
  with_tile_globals(scene,descriptors,access,[&](KernelGlobals kg){
    for(auto &probe:probes){
      ShaderData sd{};sd.lcg_state=probe.lcg;dual2 uv(make_float2(probe.u,probe.v));uv.dx=make_float2(probe.dudx,probe.dvdx);uv.dy=make_float2(probe.dudy,probe.dvdy);
      const float4 rgba=svm_image_texture(kg,&sd,probe.id,uv,probe.flags,IMAGE_MISSING_RGBA);out.push_back({rgba,sd.lcg_state});
    }});
  return out;
}
std::vector<ImageProjectionResult> original_image_projection_samples(ccl::DeviceScene &scene,const std::vector<ImageProjectionProbe> &probes,
                                                                     std::vector<ccl::KernelTileDescriptor> &descriptors,std::vector<uint8_t> &access){
  using namespace ccl;std::vector<ImageProjectionResult> out;
  with_tile_globals(scene,descriptors,access,[&](KernelGlobals kg){
    for(auto &probe:probes){
      dual3 co(make_float3(probe.co[0],probe.co[1],probe.co[2]));co.dx=make_float3(probe.dx[0],probe.dx[1],probe.dx[2]);co.dy=make_float3(probe.dy[0],probe.dy[1],probe.dy[2]);
      const dual2 uv=probe.kind<4?dual2(svm_node_tex_image_mapping(co,probe.kind)):dual2(svm_node_tex_environment_projection(co,probe.kind-4));
      ShaderData sd{};sd.lcg_state=probe.lcg;const float4 rgba=svm_image_texture(kg,&sd,probe.id,uv,probe.flags,IMAGE_MISSING_RGBA);
      out.push_back({rgba,sd.lcg_state,{uv.val.x,uv.val.y,uv.dx.x,uv.dx.y,uv.dy.x,uv.dy.y}});
    }});
  return out;
}
