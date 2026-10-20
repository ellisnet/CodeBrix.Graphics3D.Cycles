// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Exports scene-wide camera/sampling data and original Cycles sampler answers.
#pragma once
#include "kernel/globals.h"
#include "kernel/sample/pattern.h"
#include <bit>
#include <fstream>
#include <stdexcept>
#include <vector>

inline void export_camera_reference(ccl::ThreadKernelGlobalsCPU &globals,
                                    const std::string &directory, unsigned width,
                                    unsigned height, unsigned samples, unsigned bounces) {
  using namespace ccl;
  KernelGlobals kg=&globals;
  const auto &cam=globals.data.cam;
  auto &integrator=globals.data.integrator;
  if(cam.type!=CAMERA_PERSPECTIVE || cam.aperturesize!=0 || cam.num_motion_steps ||
     cam.have_perspective_motion || cam.interocular_offset!=0 || cam.shuttertime!=-1 ||
     integrator.sampling_pattern!=SAMPLING_PATTERN_TABULATED_SOBOL ||
     integrator.pixel_jitter.x!=FLT_MAX || NUM_TAB_SOBOL_DIMENSIONS!=4)
    throw std::runtime_error("Unsupported camera/sampler configuration for WGSL gate");
  const unsigned lut_base=13*4, lut_size=globals.sample_pattern_lut.width;
  const unsigned filter_base=lut_base+lut_size;
  if(lut_size!=integrator.tabulated_sobol_sequence_size*NUM_TAB_SOBOL_PATTERNS*4 ||
     globals.data.tables.filter_table_offset<0 ||
     globals.data.tables.filter_table_offset+FILTER_TABLE_SIZE>globals.lookup_table.width)
    throw std::runtime_error("Invalid prepared sampler/filter table");
  std::vector<uint32_t> words(filter_base+FILTER_TABLE_SIZE);
  auto u=[&](unsigned row,unsigned a,unsigned b,unsigned c,unsigned d) {
    words[row*4]=a; words[row*4+1]=b; words[row*4+2]=c; words[row*4+3]=d;
  };
  auto f=[&](unsigned row,float a,float b,float c,float d) {
    u(row,std::bit_cast<uint32_t>(a),std::bit_cast<uint32_t>(b),
          std::bit_cast<uint32_t>(c),std::bit_cast<uint32_t>(d));
  };
  u(0,width,height,samples,bounces);
  u(1,integrator.seed,integrator.tabulated_sobol_sequence_size,NUM_TAB_SOBOL_PATTERNS,4);
  f(2,integrator.scrambling_distance,cam.nearclip,cam.cliplength,0);
  u(3,PRNG_FILTER,PRNG_BOUNCE_NUM,PRNG_SURFACE_BSDF,FILTER_TABLE_SIZE);
  u(4,lut_base,filter_base,lut_size,FILTER_TABLE_SIZE);
  unsigned row=5;
  for(const auto &v:{cam.rastertocamera.x,cam.rastertocamera.y,cam.rastertocamera.z,
                    cam.rastertocamera.w,cam.cameratoworld.x,cam.cameratoworld.y,cam.cameratoworld.z})
    f(row++,v.x,v.y,v.z,v.w);
  f(12,integrator.pixel_jitter.x,integrator.pixel_jitter.y,0,0);
  for(unsigned i=0;i<lut_size;i++) words[lut_base+i]=std::bit_cast<uint32_t>(globals.sample_pattern_lut.fetch(i));
  for(unsigned i=0;i<FILTER_TABLE_SIZE;i++)
    words[filter_base+i]=std::bit_cast<uint32_t>(globals.lookup_table.fetch(globals.data.tables.filter_table_offset+i));
  auto write=[&](const char *name,const std::vector<uint32_t> &v) {
    std::ofstream file(directory+'/'+name,std::ios::binary);
    file.write(reinterpret_cast<const char*>(v.data()),v.size()*4);
    if(!file) throw std::runtime_error("Cannot write camera/sampler data");
  };
  write("camera.scene.bin",words);
  constexpr unsigned cases=16384;
  std::vector<uint32_t> input(cases*8), expected(cases*8);
  const auto original_integrator=integrator;
  struct Restore { KernelIntegrator &value; KernelIntegrator saved; ~Restore(){value=saved;} } restore{integrator,original_integrator};
  const unsigned edge_samples[]={0,1,15,255,256,65535,(1u<<24)-1,0xffffffff};
  const float distances[]={0,.125f,.75f,1};
  for(unsigned i=0;i<cases;i++) {
    const unsigned x=hash_wang_seeded_uint(i,137), y=hash_wang_seeded_uint(i,509);
    const unsigned sample=(i%2)?edge_samples[(i/2)%8]:hash_wang_seeded_uint(i,31);
    const unsigned dimension=(i%3)?i*17:hash_wang_seeded_uint(i,97);
    integrator.seed=i%7==0?0:(i%7==1?0xffffffff:hash_wang_seeded_uint(i,73));
    integrator.scrambling_distance=distances[(i/16)%4];
    const unsigned pixel=path_rng_pixel_init(kg,sample,x,y);
    const unsigned seed=integrator.scrambling_distance<1?integrator.seed:pixel;
    auto values=path_rng_4D(kg,pixel,sample,dimension);
    const auto two=path_rng_2D(kg,pixel,sample,dimension);
    const auto three=path_rng_3D(kg,pixel,sample,dimension);
    if(values.x!=path_rng_1D(kg,pixel,sample,dimension) || values.x!=two.x || values.y!=two.y ||
       values.x!=three.x || values.y!=three.y || values.z!=three.z)
      throw std::runtime_error("Unexpected tabulated Sobol dimension relationship");
    auto *in=input.data()+i*8; auto *out=expected.data()+i*8;
    in[0]=x; in[1]=y; in[2]=sample; in[3]=dimension;
    in[4]=integrator.seed; in[5]=std::bit_cast<uint32_t>(integrator.scrambling_distance);
    out[0]=pixel; out[1]=tabulated_sobol_shuffled_sample_index(kg,sample,dimension,seed);
    out[4]=std::bit_cast<uint32_t>(values.x); out[5]=std::bit_cast<uint32_t>(values.y);
    out[6]=std::bit_cast<uint32_t>(values.z); out[7]=std::bit_cast<uint32_t>(values.w);
  }
  write("sampler.input.bin",input); write("sampler.expected.bin",expected);
  std::fill(expected.begin(),expected.end(),0); write("sampler.output.bin",expected);
  std::ofstream report(directory+"/camera.json");
  report<<"{\"camera\":\"perspective, static, pinhole\",\"sampler\":\"tabulated Sobol\","
        <<"\"sequenceSize\":"<<original_integrator.tabulated_sobol_sequence_size
        <<",\"patterns\":"<<NUM_TAB_SOBOL_PATTERNS<<",\"samplerCases\":"<<cases
        <<",\"sceneBytes\":"<<words.size()*4<<",\"perPathCpuInputs\":false,\"rayDifferentials\":false}\n";
}
