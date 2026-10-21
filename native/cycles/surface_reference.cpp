// SPDX-FileCopyrightText: 2009-2010 Sony Pictures Imageworks Inc., et al. All Rights Reserved.
// SPDX-FileCopyrightText: 2011-2022 Blender Foundation
// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0 AND BSD-3-Clause
// Optional diagnostic intermediates follow pinned bsdf_microfacet.h; original
// integrator bodies remain untouched. See native/THIRD-PARTY-NOTICES.txt and
// parity/licenses/BSD-3-Clause.txt for the complete retained notices/license.
#include "device/device.h"
#include "image_reference.h"
#include "kernel/globals.h"
#include "kernel/device/cpu/image.h"
#include "kernel/integrator/state.h"
#include "kernel/integrator/state_flow.h"
#include "kernel/integrator/state_util.h"
#include "kernel/integrator/shade_surface.h"
#include "scene/scene.h"
#include "wavefront.h"
#include "surface_reference.h"
#if defined(CBC_MICROFACET_FIXTURE) || defined(CBC_GLASS_FIXTURE) || defined(CBC_PRINCIPLED_FIXTURE)
#include <cstdlib>
#include <fstream>
#include <iomanip>
#endif
void original_surface(ccl::DeviceScene &scene,const std::vector<int> &indices,
    std::vector<ccl::IntegratorStateCPU> &states,std::vector<float> &film){
  using namespace ccl;KernelGlobalsCPU base;base.data=scene.data;
  auto images=original_image_info(scene);base.image_info.data=images.data();base.image_info.width=images.size();
#define TABLE(name,type) base.name.data=reinterpret_cast<decltype(base.name.data)>(scene.name.data());base.name.width=int(scene.name.size());
  CBC_SCENE_TABLES(TABLE)
  TABLE(svm_nodes,uint) TABLE(lookup_table,float) TABLE(sample_pattern_lut,float)
#undef TABLE
  Profiler profiler;ThreadKernelGlobalsCPU thread(base,nullptr,profiler,0);KernelGlobals kg=&thread;
#if defined(CBC_MICROFACET_FIXTURE) || defined(CBC_GLASS_FIXTURE) || defined(CBC_PRINCIPLED_FIXTURE)
  const char *debug_index=std::getenv("CODEBRIX_MICROFACET_DIAGNOSTIC_INDEX");
  const int diagnostic_index=debug_index?std::atoi(debug_index):162;
#endif
  for(int i:indices){
#if defined(CBC_MICROFACET_FIXTURE) || defined(CBC_GLASS_FIXTURE) || defined(CBC_PRINCIPLED_FIXTURE)
    if(i==diagnostic_index)if(const char *path=std::getenv("CODEBRIX_MICROFACET_DIAGNOSTIC")){
      auto state=states[i];Ray ray{};Intersection isect{};ShaderData sd{};
      integrator_state_read_ray(&state,&ray);integrator_state_read_isect(&state,&isect);shader_setup_from_ray(kg,&sd,&ray,&isect);
      surface_shader_eval<KERNEL_FEATURE_NODE_MASK_SURFACE>(kg,&state,&sd,nullptr,PathRayVisibility(state.path.visibility),state.path.flag);
      RNGState rng;path_state_rng_load(&state,&rng);auto rand=path_state_rng_3D(kg,&rng,PRNG_SURFACE_BSDF);
      if(sd.num_closure){auto sc=surface_shader_bsdf_bssrdf_pick(&sd,&rand);
        std::ofstream out(path);out<<std::setprecision(10)<<"{\"kind\":"<<sc->type<<",\"count\":"<<sd.num_closure<<",\"runtime_flag\":"<<sd.runtime_flag;
        auto v=[&](const char *name,float3 x){out<<",\""<<name<<"\":["<<x.x<<','<<x.y<<','<<x.z<<']';};
        v("N",sc->N);v("Ng",sd.Ng);v("shader_N",sd.N);v("wi",sd.wi);v("random",rand);v("weight",sc->weight);
        if(CLOSURE_IS_BSDF(sc->type)){
          Spectrum eval=zero_spectrum();float3 wo=zero_float3();float pdf=0,eta=0;float2 roughness=make_float2(0,0);
          int label=bsdf_sample(kg,&sd,sc,rand,&eval,&wo,&pdf,&roughness,&eta);
          out<<",\"sample_label\":"<<label<<",\"sample_pdf\":"<<pdf;v("sample_wo",wo);v("sample_eval",eval);
        }
        if(CLOSURE_IS_BSDF_MICROFACET(sc->type)){auto &c=*reinterpret_cast<const MicrofacetBsdf*>(sc);float3 X,Y;
          if(c.alpha_x==c.alpha_y)make_orthonormals(c.N,&X,&Y);else make_orthonormals_tangent(c.N,c.T,&X,&Y);
          auto I=make_float3(dot(X,sd.wi),dot(Y,sd.wi),dot(c.N,sd.wi));
          out<<",\"ior\":"<<c.ior<<",\"alpha\":["<<c.alpha_x<<','<<c.alpha_y<<']';
          v("T",c.T);v("X",X);v("Y",Y);v("I",I);
          v("H",microfacet_ggx_sample_vndf(I,c.alpha_x,c.alpha_y,make_float2(rand)));
          auto V=normalize(make_float3(c.alpha_x*I.x,c.alpha_y*I.y,I.z));
          const float lensq=V.x*V.x+V.y*V.y;auto X1=make_float3(1,0,0),Y1=make_float3(0,1,0);if(lensq>1e-7f){X1=make_float3(-V.y,V.x,0)*inversesqrtf(lensq);Y1=cross(V,X1);}
          auto disk=sample_uniform_disk(make_float2(rand));auto t=disk;t.y=mix(safe_sqrtf(1-t.x*t.x),t.y,.5f*(1+V.z));
          auto h=to_global(disk_to_hemisphere(t),X1,Y1,V);auto scaled=make_float3(c.alpha_x*h.x,c.alpha_y*h.y,max(0.f,h.z));
          v("V",V);v("X1",X1);v("Y1",Y1);v("disk",make_float3(disk.x,disk.y,0));v("t",make_float3(t.x,t.y,0));v("h",h);v("scaled",scaled);v("norm",make_float3(len(scaled),dot(scaled,scaled),0));
        }
        out<<"}\n";
      }
    }
#endif
    integrator_shade_surface(kg,&states[i],film.data());
  }
}
