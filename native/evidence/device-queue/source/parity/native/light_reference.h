// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "kernel/light/sample.h"
#include <array>
#include <bit>
#include <fstream>
#include <stdexcept>
#include <vector>

inline void export_light_reference(ccl::KernelGlobals kg,const std::string &directory) {
  using namespace ccl; using Row=std::array<float,4>;
  const auto &data=kg->data; const unsigned count=data.integrator.num_lights;
  const unsigned distributions=data.integrator.num_distribution, base=3, dist_base=base+count*4;
  if(!count || count!=distributions || data.integrator.use_light_tree || data.integrator.light_inv_rr_threshold!=0)
    throw std::runtime_error("Unsupported light distribution in WGSL gate");
  std::vector<Row> packed(dist_base+distributions+1);
  auto u=[&](unsigned row,unsigned a,unsigned b,unsigned c,unsigned d) {
    packed[row]={std::bit_cast<float>(a),std::bit_cast<float>(b),std::bit_cast<float>(c),std::bit_cast<float>(d)};
  };
  u(0,count,distributions,base,dist_base);
  packed[1]={data.integrator.distribution_pdf_lights,0,float(data.integrator.transparent_max_bounce),0};
  u(2,PRNG_LIGHT,0,0,0);
  for(unsigned i=0;i<count;i++) {
    const auto &l=kg->lights.fetch(i); Spectrum emission;
    if(l.type!=LIGHT_POINT || !l.spot.is_sphere || !(l.shader_id&SHADER_USE_MIS) || l.use_caustics ||
       !(l.shader_id&SHADER_CAST_SHADOW) ||
       !light_sample_shader_eval_nee_constant(kg,l.shader_id,i,true,emission))
      throw std::runtime_error("Only constant spherical point lights with MIS/shadows are supported here");
    packed[base+i*4]={l.co.x,l.co.y,l.co.z,l.spot.radius};
    packed[base+i*4+1]={emission.x,emission.y,emission.z,l.spot.eval_fac};
    u(base+i*4+2,1,l.max_bounces,1,1); u(base+i*4+3,l.object_id,l.shader_id,0,0);
  }
  for(unsigned i=0;i<=distributions;i++) {
    const auto &d=kg->light_distribution.fetch(i);
    if(i<distributions && (d.prim>=0 || unsigned(~d.prim)>=count || d.visibility_flag))
      throw std::runtime_error("Unsupported light CDF entry");
    u(dist_base+i, std::bit_cast<unsigned>(d.totarea),unsigned(~d.prim),d.object_id,d.visibility_flag);
  }
  auto write=[&](const char *name,const std::vector<Row> &v) {
    std::ofstream file(directory+'/'+name,std::ios::binary);
    file.write(reinterpret_cast<const char*>(v.data()),v.size()*16);
    if(!file) throw std::runtime_error("Cannot write light reference");
  };
  write("lights.scene.bin",packed);
  constexpr unsigned cases=4096;
  std::vector<Row> input(cases*3),expected(cases*5);
  for(unsigned i=0;i<cases;i++) {
    const auto &light=kg->lights.fetch(i%count);
    const float rx=hash_wang_seeded_float(i,47),ry=hash_wang_seeded_float(i,61);
    const float rz=hash_wang_seeded_float(i,97);
    float3 P=make_float3((rx-.5f)*12,(ry-.5f)*12,rz*6);
    if(i%8<2) P=light.co+make_float3(.1f,0,.05f); // Inside a finite emitter.
    const float3 N=normalize(make_float3(rx-.5f,ry-.5f,1));
    const bool transmit=i%2;
    float3 random=make_float3(hash_wang_seeded_float(i,113),hash_wang_seeded_float(i,127),rz);
    if(i%8<2) random.z=(float(i%count)+.5f)/count;
    if(i<16) { random.x=float(i%2); random.y=float((i/2)%2); }
    input[i*3]={P.x,P.y,P.z,float(transmit)}; input[i*3+1]={N.x,N.y,N.z,.3f};
    input[i*3+2]={random.x,random.y,random.z,0};
    LightSample ls{};
    if(!light_sample_from_position(kg,random,.5f,P,N,0,transmit?SR_BSDF_HAS_TRANSMISSION:0,0,0,&ls))
      throw std::runtime_error("Unexpected light sample rejection");
    expected[i*5]={ls.P.x,ls.P.y,ls.P.z,ls.t};
    expected[i*5+1]={ls.D.x,ls.D.y,ls.D.z,ls.pdf};
    expected[i*5+2]={ls.Ng.x,ls.Ng.y,ls.Ng.z,ls.eval_fac};
    const auto &l=kg->lights.fetch(ls.prim);
    Ray ray{}; ray.P=P; ray.D=ls.D; ray.tmax=FLT_MAX; float t=0;
    const bool intersect=point_light_intersect(&l,&ray,&t);
    const float forward_pdf=sphere_light_pdf(len_squared(P-l.co),sqr(l.spot.radius),N,ls.D,
                                          transmit?PATH_RAY_MIS_HAD_TRANSMISSION:0)*ls.pdf_selection;
    expected[i*5+3]={float(ls.prim),float(intersect),intersect?t:0,forward_pdf};
    expected[i*5+4]={light_sample_mis_weight_nee(kg,ls.pdf,.3f),
                    light_sample_mis_weight_forward(kg,.3f,forward_pdf),ls.pdf_selection,0};
  }
  write("light_sample.input.bin",input); write("light_sample.expected.bin",expected);
  std::fill(expected.begin(),expected.end(),Row{}); write("light_sample.output.bin",expected);
  std::ofstream report(directory+"/lights.json");
  report<<"{\"lights\":"<<count<<",\"distributionEntries\":"<<distributions
        <<",\"samplerCases\":"<<cases<<",\"lightTypes\":[\"finite spherical point\"],"
        <<"\"constantEmission\":true,\"mis\":true,\"opaqueShadows\":true}\n";
}
