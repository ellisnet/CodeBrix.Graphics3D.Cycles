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

void render_path_reference(ccl::Device *device,const std::string &directory,
                           unsigned width,unsigned height,unsigned samples,unsigned bounces) {
  using namespace ccl;
  auto globals=device->acquire_cpu_kernel_thread_globals();
  struct Release { Device *device; ~Release() { device->release_cpu_kernel_thread_globals(); } } release{device};
  if(globals->empty()) throw std::runtime_error("Missing CPU kernel globals");
  KernelGlobals kg=&(*globals)[0];
  const auto &data=kg->data;
  if(data.film.pass_sample_count!=PASS_UNUSED || data.film.pass_adaptive_aux_buffer!=PASS_UNUSED ||
     data.background.use_mis || data.integrator.num_lights || data.cam.is_inside_volume ||
     data.integrator.max_bounce!=int(bounces) || data.integrator.min_bounce<int(bounces))
    throw std::runtime_error("Unsupported reference configuration");
  const unsigned paths=width*height*samples, input_rows=3+bounces, output_rows=3+bounces;
  std::vector<float> inputs(size_t(paths)*input_rows*4),expected(size_t(paths)*output_rows*4);
  std::vector<float> film(size_t(width)*height*data.film.pass_stride), stepped(film.size());
  std::vector<float> image(size_t(width)*height*4);
  std::vector<unsigned> histogram(bounces+1), hit_histogram(bounces+1);
  const unsigned stride=data.film.pass_stride,combined=data.film.pass_combined;
  for(unsigned s=0;s<samples;s++) for(unsigned y=0;y<height;y++) for(unsigned x=0;x<width;x++) {
    const size_t index=(size_t(s)*height+y)*width+x;
    auto *in=inputs.data()+index*input_rows*4; auto *out=expected.data()+index*output_rows*4;
    std::fill_n(film.data(),stride,0.f); std::fill_n(stepped.data(),stride,0.f);
    KernelWorkTile tile{}; tile.w=width; tile.h=height; tile.stride=width; tile.num_samples=samples;
    IntegratorStateCPU state{};
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
    auto trace_state=state;
    integrator_megakernel(kg,&state,film.data());
    if(state.path.bounce>int(bounces)) throw std::runtime_error("Reference exceeded bounce limit");
    histogram[state.path.bounce]++;
    for(unsigned b=0;b<=bounces;b++) out[8+b*4+3]=-2;
    // A second execution instruments the same original kernels. Compare its film
    // byte-for-byte with the untouched megakernel, so instrumentation is checked.
    unsigned steps=0;
    while(trace_state.path.queued_kernel) {
      if(++steps>3*(bounces+2) || trace_state.shadow.shadow_path.queued_kernel || trace_state.ao.shadow_path.queued_kernel)
        throw std::runtime_error("Unexpected path queue in limited reference");
      const int bounce=trace_state.path.bounce;
      switch(trace_state.path.queued_kernel) {
        case DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST: {
          integrator_intersect_closest(kg,&trace_state,stepped.data());
          const auto &hit=trace_state.isect;
          bool miss=hit.prim==PRIM_NONE;
          // Misses need not initialize barycentrics in the original kernel.
          if(!miss) { out[8+bounce*4]=hit.t; out[9+bounce*4]=hit.u; out[10+bounce*4]=hit.v; }
          out[11+bounce*4]=miss?-1.f:float(hit.prim);
          if(!miss) hit_histogram[bounce]++;
          break;
        }
        case DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE: integrator_shade_surface(kg,&trace_state,stepped.data()); break;
        case DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND: integrator_shade_background(kg,&trace_state,stepped.data()); break;
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
  report<<"]}\n";
}
