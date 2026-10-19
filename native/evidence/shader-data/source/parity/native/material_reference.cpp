// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Invoke the complete original Cycles SVM interpreter and original BSDFs.
#include "device/device.h"
#include "kernel/globals.h"
#include "kernel/device/cpu/image.h"
#include "kernel/integrator/state.h"
#include "kernel/integrator/state_flow.h"
#include "kernel/integrator/state_util.h"
#include "kernel/integrator/megakernel.h"
#include "material_reference.h"
#include <stdexcept>
void evaluate_material_reference(ccl::Device *device,const float *inputs,float *outputs,int count) {
  using namespace ccl;
  auto globals=device->acquire_cpu_kernel_thread_globals();
  if(globals->empty()) throw std::runtime_error("No CPU kernel globals");
  KernelGlobals kg=&(*globals)[0];
  for(int i=0;i<count;i++) {
    const float *in=inputs+20*i; float *out=outputs+24*i;
    ShaderData sd{}; sd.N=make_float3(in[0],in[1],in[2]); sd.Ng=make_float3(in[4],in[5],in[6]);
    sd.wi=make_float3(in[8],in[9],in[10]); sd.shader=int(in[3]); sd.num_closure_left=MAX_CLOSURE;
    sd.object=OBJECT_NONE; sd.prim=PRIM_NONE;
    IntegratorStateCPU state{};
    svm_eval_nodes<KERNEL_FEATURE_NODE_MASK_SURFACE,SHADER_TYPE_SURFACE>(kg,&state,&sd,nullptr,PATH_RAY_VISIBILITY_CAMERA,0);
    out[20]=float(sd.num_closure);
    if(sd.num_closure==0) continue;
    if(sd.num_closure!=1) throw std::runtime_error("Single-closure gate received a mixture");
    auto *sc=&sd.closure[0];
    out[0]=sc->weight.x; out[1]=sc->weight.y; out[2]=sc->weight.z; out[3]=sc->sample_weight;
    out[4]=sc->N.x; out[5]=sc->N.y; out[6]=sc->N.z; out[7]=float(sc->type);
    auto random=make_float2(in[12],in[13]); auto direction=make_float3(in[16],in[17],in[18]);
    Spectrum eval=zero_spectrum(),value=zero_spectrum(); float3 wo=zero_float3(); float pdf=0,eval_pdf=0;
    int label=0;
    switch(sc->type) {
      case CLOSURE_BSDF_DIFFUSE_ID:
        label=bsdf_diffuse_sample(sc,sd.Ng,sd.wi,random,&eval,&wo,&pdf);
        value=bsdf_diffuse_eval(sc,sd.wi,direction,&eval_pdf); break;
      case CLOSURE_BSDF_TRANSLUCENT_ID:
        label=bsdf_translucent_sample(sc,sd.Ng,sd.wi,random,&eval,&wo,&pdf);
        value=bsdf_translucent_eval(sc,sd.wi,direction,&eval_pdf); break;
      case CLOSURE_BSDF_OREN_NAYAR_ID: {
        float2 roughness; float eta;
        label=bsdf_oren_nayar_sample(sc,sd.Ng,sd.wi,random,&eval,&wo,&pdf,&roughness,&eta);
        value=bsdf_oren_nayar_eval(sc,sd.wi,direction,&eval_pdf); break;
      }
      default: throw std::runtime_error("Unsupported reference closure");
    }
    out[8]=wo.x; out[9]=wo.y; out[10]=wo.z; out[11]=pdf;
    out[12]=eval.x; out[13]=eval.y; out[14]=eval.z; out[15]=float(label);
    out[16]=value.x; out[17]=value.y; out[18]=value.z; out[19]=eval_pdf;
  }
  device->release_cpu_kernel_thread_globals();
}
