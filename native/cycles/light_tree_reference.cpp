// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Original CPU oracle for the light-tree gate: the unmodified pinned
// light_tree_sample<false>, light_distribution_sample, light_sample<false> and
// light_tree_pdf<false> on the device-prepared scene tables.
#include "device/device.h"
#include "image_reference.h"
#include "kernel/globals.h"
#include "kernel/device/cpu/image.h"
#include "kernel/integrator/state.h"
#include "kernel/integrator/state_flow.h"
#include "kernel/integrator/state_util.h"
#include "kernel/integrator/path_state.h"
#include "kernel/light/sample.h"
#include "scene/scene.h"
#include "light_tree_reference.h"
#include "wavefront.h"
std::vector<float> original_light_tree_samples(ccl::DeviceScene &scene,const std::vector<ccl::IntegratorStateCPU> &states,const std::vector<LightTreeProbeInput> &inputs){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
  auto images=original_image_info(scene);base.image_info.data=images.data();base.image_info.width=images.size();
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
  TABLE(svm_nodes,uint) TABLE(sample_pattern_lut,float)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  std::vector<float> out(states.size()*light_tree_probe_record_floats,0.0f);
  for(size_t i=0;i<states.size();i++){
    auto state=states[i];const auto &in=inputs[i];RNGState rng;path_state_rng_load(&state,&rng);const float3 rand=path_state_rng_3D(kg,&rng,PRNG_LIGHT);
    const int runtime=(state.path.flag&PATH_RAY_MIS_HAD_TRANSMISSION)?SR_BSDF_HAS_TRANSMISSION:0;
    const int receiver=(kernel_data.kernel_features&KERNEL_FEATURE_LIGHT_LINKING)?in.receiver:OBJECT_NONE;
    float *p=out.data()+i*light_tree_probe_record_floats;LightSample ls{};
    // light_sample_from_position, split to expose the selection.
    bool selected;
    if(kernel_data.integrator.use_light_tree)selected=light_tree_sample<false>(kg,rand.z,state.ray.P,state.ray.D,0.0f,receiver,runtime,&ls);
    else selected=light_distribution_sample(kg,rand.z,&ls);
    p[0]=selected;
    if(selected){p[1]=float(ls.emitter_id);p[2]=ls.pdf_selection;
      const bool valid=light_sample<false>(kg,rand,state.ray.time,state.ray.P,state.ray.D,receiver,runtime,state.path.bounce,state.path.flag,&ls)&&ls.pdf>0;
      p[3]=valid;if(valid){p[4]=ls.P.x;p[5]=ls.P.y;p[6]=ls.P.z;p[7]=ls.D.x;p[8]=ls.D.y;p[9]=ls.D.z;p[10]=ls.t;p[11]=ls.pdf;p[12]=ls.eval_fac;p[13]=float(ls.prim);p[14]=float(ls.object);}}
    if(kernel_data.integrator.use_light_tree&&in.target_emitter>=0){
      const int forward=(kernel_data.kernel_features&KERNEL_FEATURE_LIGHT_LINKING)?in.forward_receiver:OBJECT_NONE;
      p[15]=light_tree_pdf<false>(kg,state.ray.P,state.ray.D,0.0f,state.path.flag,in.target_object,uint(in.target_emitter),forward);
    }
  }
  return out;
}
std::vector<float> original_light_tree_importance(ccl::DeviceScene &scene,const std::vector<ccl::IntegratorStateCPU> &states){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
  const size_t nodes=scene.light_tree_nodes.size(),emitters=scene.light_tree_emitters.size();std::vector<float> out;
  for(const auto &state:states){const bool transmission=state.path.flag&PATH_RAY_MIS_HAD_TRANSMISSION;
    for(size_t n=0;n<nodes;n++){float lp=-1;const auto &kn=scene.light_tree_nodes[n];
      if(kn.type==LIGHT_TREE_INNER&&!get_left_probability<false>(kg,state.ray.P,state.ray.D,0.0f,transmission,kn.inner.left_child,kn.inner.right_child,lp))lp=-2;
      out.push_back(lp);}
    for(size_t n=0;n<nodes;n++){float mx=0,mn=0;light_tree_node_importance<false>(state.ray.P,state.ray.D,0.0f,transmission,&scene.light_tree_nodes[n],mx,mn);out.push_back(mx);out.push_back(mn);}
    for(size_t e=0;e<emitters;e++){float mx=0,mn=0;light_tree_emitter_importance<false>(kg,state.ray.P,state.ray.D,0.0f,transmission,int(e),mx,mn);out.push_back(mx);out.push_back(mn);}}
  return out;
}
