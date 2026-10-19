// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Original Cycles camera, sampler, complete megakernel and film accumulation.
#include "device/device.h"
#include "kernel/globals.h"
#include "kernel/device/cpu/image.h"
#include "kernel/integrator/state.h"
#include "kernel/integrator/state_flow.h"
#include "kernel/integrator/state_util.h"
#include "kernel/integrator/megakernel.h"
#include "kernel/integrator/init_from_camera.h"
#include "path_reference.h"
#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>
#include <vector>
#include "camera_reference.h"
#include "light_reference.h"

void render_path_reference(ccl::Device *device,const std::string &directory,
                           unsigned width,unsigned height,unsigned samples,unsigned bounces,bool lighting) {
  using namespace ccl;
  auto globals=device->acquire_cpu_kernel_thread_globals();
  struct Release { Device *device; ~Release() { device->release_cpu_kernel_thread_globals(); } } release{device};
  if(globals->empty()) throw std::runtime_error("Missing CPU kernel globals");
  KernelGlobals kg=&(*globals)[0];
  export_camera_reference((*globals)[0],directory,width,height,samples,bounces);
  if(lighting) export_light_reference(kg,directory);
  const auto &data=kg->data;
  if(data.film.pass_sample_count!=PASS_UNUSED || data.film.pass_adaptive_aux_buffer!=PASS_UNUSED ||
     data.background.use_mis || (!lighting && data.integrator.num_lights) || data.cam.is_inside_volume ||
     data.integrator.has_shadow_catcher ||
     data.integrator.max_bounce!=int(bounces) || data.integrator.min_bounce<int(bounces))
    throw std::runtime_error("Unsupported reference configuration");
  const unsigned paths=width*height*samples, input_rows=3+bounces;
  const unsigned shadow_base=3+bounces, forward_row=shadow_base+2*bounces;
  const unsigned output_rows=3+bounces+(lighting?2*bounces+1:0);
  std::vector<float> inputs(size_t(paths)*input_rows*4),expected(size_t(paths)*output_rows*4);
  std::vector<float> film(size_t(width)*height*data.film.pass_stride), stepped(film.size());
  std::vector<float> image(size_t(width)*height*4);
  std::vector<unsigned> histogram(bounces+1), hit_histogram(bounces+1);
  unsigned shadow_rays=0,shadow_blocked=0,forward_hits=0,indirect_forward_hits=0;
  double shadow_energy=0,forward_energy=0;
  const unsigned stride=data.film.pass_stride,combined=data.film.pass_combined;
  for(unsigned s=0;s<samples;s++) for(unsigned y=0;y<height;y++) for(unsigned x=0;x<width;x++) {
    const size_t index=(size_t(s)*height+y)*width+x;
    auto *in=inputs.data()+index*input_rows*4; auto *out=expected.data()+index*output_rows*4;
    std::fill_n(film.data(),stride,0.f); std::fill_n(stepped.data(),stride,0.f);
    KernelWorkTile tile{}; tile.w=width; tile.h=height; tile.stride=width; tile.num_samples=samples;
    // Match the original CPU worker's two-slot allocation. Shadow catchers
    // remain explicitly rejected, but compiled kernels can address state+1.
    std::array<IntegratorStateCPU,2> states{};
    auto &state=states[0];
    if(!integrator_init_from_camera(kg,&state,&tile,film.data(),x,y,s) || !state.path.queued_kernel)
      throw std::runtime_error("Unexpected camera rejection");
    // Each path receives its own raw film record. Camera coordinates/sampler are unchanged.
    state.path.render_pixel_index=0;
    Ray ray; integrator_state_read_ray(&state,&ray);
    const auto T=state.path.throughput;
    const float initial[]={ray.P.x,ray.P.y,ray.P.z,ray.tmin,ray.D.x,ray.D.y,ray.D.z,ray.tmax,T.x,T.y,T.z,0};
    std::copy_n(initial,12,in);
    RNGState rng; path_state_rng_load(&state,&rng);
    for(unsigned bounce=0;bounce<bounces;bounce++) {
      auto random=path_state_rng_3D(kg,&rng,PRNG_SURFACE_BSDF);
      in[12+bounce*4]=random.x; in[13+bounce*4]=random.y; in[14+bounce*4]=random.z;
      rng.rng_offset+=PRNG_BOUNCE_NUM;
    }
    auto trace_states=states;
    auto &trace_state=trace_states[0];
    integrator_megakernel(kg,&state,film.data());
    if(states[1].path.queued_kernel) throw std::runtime_error("Unexpected split reference path");
    if(state.path.bounce>int(bounces)) throw std::runtime_error("Reference exceeded bounce limit");
    histogram[state.path.bounce]++;
    for(unsigned b=0;b<=bounces;b++) out[8+b*4+3]=-2;
    if(lighting) for(unsigned b=0;b<bounces;b++) {
      out[(shadow_base+2*b+1)*4]=-1; out[(shadow_base+2*b+1)*4+1]=-1;
    }
    // A second execution instruments the same original kernels. Compare its film
    // byte-for-byte with the untouched megakernel, so instrumentation is checked.
    unsigned steps=0;
    while(trace_state.path.queued_kernel || trace_state.shadow.shadow_path.queued_kernel) {
      if(++steps>16*(bounces+2) || (!lighting && trace_state.shadow.shadow_path.queued_kernel) ||
         trace_state.ao.shadow_path.queued_kernel || trace_states[1].path.queued_kernel)
        throw std::runtime_error("Unexpected path queue in limited reference");
      if(trace_state.shadow.shadow_path.queued_kernel) {
        const unsigned sb=trace_state.shadow.shadow_path.bounce;
        if(sb>=bounces) throw std::runtime_error("Unexpected shadow bounce");
        auto *record=out+(shadow_base+2*sb+1)*4;
        switch(trace_state.shadow.shadow_path.queued_kernel) {
          case DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW:
            integrator_intersect_shadow(kg,&trace_state.shadow);
            record[1]=trace_state.shadow.shadow_path.queued_kernel?0.f:1.f;
            shadow_blocked+=record[1]==1.f; break;
          case DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW: {
            const float before=stepped[combined]+stepped[combined+1]+stepped[combined+2];
            integrator_shade_shadow(kg,&trace_state.shadow,stepped.data());
            shadow_energy+=stepped[combined]+stepped[combined+1]+stepped[combined+2]-before; break;
          }
          default: throw std::runtime_error("Unexpected shadow queue");
        }
        continue;
      }
      const int bounce=trace_state.path.bounce;
      switch(trace_state.path.queued_kernel) {
        case DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST: {
          integrator_intersect_closest(kg,&trace_state,stepped.data());
          const auto &hit=trace_state.isect;
          if(lighting && hit.type==PRIMITIVE_LAMP && hit.prim!=PRIM_NONE) break;
          bool miss=hit.prim==PRIM_NONE;
          // Misses need not initialize barycentrics in the original kernel.
          if(!miss) { out[8+bounce*4]=hit.t; out[9+bounce*4]=hit.u; out[10+bounce*4]=hit.v; }
          out[11+bounce*4]=miss?-1.f:float(hit.prim);
          if(!miss) hit_histogram[bounce]++;
          break;
        }
        case DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE: {
          integrator_shade_surface(kg,&trace_state,stepped.data());
          if(trace_state.shadow.shadow_path.queued_kernel) {
            if(!lighting || unsigned(bounce)>=bounces) throw std::runtime_error("Unexpected shadow launch");
            Ray shadow{}; integrator_state_read_shadow_ray(&trace_state.shadow,&shadow);
            integrator_state_read_shadow_ray_self(&trace_state.shadow,&shadow);
            const auto T=trace_state.shadow.shadow_path.throughput;
            auto *record=out+(shadow_base+2*bounce)*4;
            record[0]=T.x; record[1]=T.y; record[2]=T.z; record[3]=shadow.tmax;
            record[4]=float(shadow.self.light_prim); shadow_rays++;
          }
          break;
        }
        case DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND: integrator_shade_background(kg,&trace_state,stepped.data()); break;
        case DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD: {
          if(!lighting) throw std::runtime_error("Unexpected forward light");
          const auto before=std::array<float,3>{stepped[combined],stepped[combined+1],stepped[combined+2]};
          integrator_shade_light_forward(kg,&trace_state,stepped.data());
          for(unsigned c=0;c<3;c++) {
            const float value=stepped[combined+c]-before[c];
            out[forward_row*4+c]+=value; forward_energy+=value;
          }
          out[forward_row*4+3]++; forward_hits++; indirect_forward_hits+=bounce>0; break;
        }
        default: throw std::runtime_error("Unimplemented diagnostic queue");
      }
    }
    if(!std::equal(film.begin(),film.begin()+stride,stepped.begin()))
      throw std::runtime_error("Instrumented reference differs from original megakernel");
    out[0]=film[combined]; out[1]=film[combined+1]; out[2]=film[combined+2]; out[3]=1;
    out[4]=float(state.path.bounce); out[5]=0;
    const size_t p=(size_t(y)*width+x)*4;
    for(unsigned c=0;c<4;c++) image[p+c]+=out[c]/samples;
  }
  auto write=[&](const char *name,const std::vector<float> &v) {
    std::ofstream file(directory+'/'+name,std::ios::binary);
    file.write(reinterpret_cast<const char*>(v.data()),v.size()*4);
    if(!file) throw std::runtime_error("Cannot write reference data");
  };
  if(hit_histogram[0]==0 || hit_histogram[1]==0 || histogram[bounces]==0)
    throw std::runtime_error("Reference fixture did not exercise primary, indirect and maximum-depth paths");
  if(lighting && (!shadow_blocked || shadow_rays<=shadow_blocked || !indirect_forward_hits ||
                  shadow_energy<=0 || forward_energy<=0))
    throw std::runtime_error("Lighting fixture did not exercise NEE, occlusion and forward MIS");
  write("pathtrace.input.bin",inputs); write("pathtrace.expected.bin",expected);
  std::fill(expected.begin(),expected.end(),0); write("pathtrace.output.bin",expected);
  write("reference.rgba.bin",image);
  std::ofstream report(directory+"/reference.json");
  report<<"{\"originalCyclesCamera\":true,\"originalCyclesSampler\":true,\"originalCyclesMegakernel\":true,"
        <<"\"instrumentedFilmMatchesExactly\":true,\"paths\":"<<paths<<",\"passStride\":"<<stride
        <<",\"combinedOffset\":"<<combined<<",\"finalBounceHistogram\":[";
  for(size_t i=0;i<histogram.size();i++) report<<(i?",":"")<<histogram[i];
  report<<"],\"hitsPerBounce\":[";
  for(size_t i=0;i<hit_histogram.size();i++) report<<(i?",":"")<<hit_histogram[i];
  report<<"],\"shadowRays\":"<<shadow_rays<<",\"blockedShadowRays\":"<<shadow_blocked
        <<",\"forwardLightHits\":"<<forward_hits<<",\"indirectForwardLightHits\":"<<indirect_forward_hits
        <<",\"shadowRgbEnergy\":"<<shadow_energy<<",\"forwardRgbEnergy\":"<<forward_energy<<"}\n";
}
