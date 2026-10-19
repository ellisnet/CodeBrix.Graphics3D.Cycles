// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "demo_camera.h"
#include "codebrix_wgpu.h"
#include <dlfcn.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>
static void require(bool ok,const char *m) { if(!ok) throw std::runtime_error(m); }
int main(int argc,char **argv) {
  try {
    require(argc==3,"Usage: render_driver SCENE OUTPUT_PREFIX");
    std::ifstream in(argv[1],std::ios::binary|std::ios::ate); require(bool(in),"Scene open failed");
    auto bytes=in.tellg(); require(bytes>0&&bytes<=64*1024*1024,"Invalid scene size");
    std::vector<char> scene(static_cast<size_t>(bytes)); in.seekg(0); in.read(scene.data(),bytes); require(bool(in),"Scene read failed");
    constexpr unsigned width=256,height=192,samples=8;
    CBCCamera camera=demo_camera(width,height); std::vector<float> pixels(width*height*4),first;
    char error[4096]{},adapter[512]{}; CBCContext *raw=nullptr;
    using Clock=std::chrono::steady_clock;
    auto start=Clock::now(); require(cbc_abi_version()==1,"ABI mismatch");
    require(cbc_create(&raw,error,sizeof error)==0,error);
    std::unique_ptr<CBCContext,decltype(&cbc_destroy)> context(raw,cbc_destroy);
    Dl_info loaded{};
    require(dladdr(reinterpret_cast<void*>(&wgpuCodebrixGetApiVersion),&loaded)!=0,
            "Cannot identify loaded WebGPU library");
    double init=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
    require(cbc_adapter(raw,adapter,sizeof adapter)==0,"Adapter query failed");
    auto render=[&](unsigned w=width) { return cbc_render(raw,&camera,w,height,samples,pixels.data(),pixels.size(),error,sizeof error); };
    require(render()!=0,"Missing scene accepted");
    require(cbc_set_scene(raw,scene.data(),scene.size(),error,sizeof error)==0,error);
    auto invalid=scene; float nan=std::numeric_limits<float>::quiet_NaN(); std::memcpy(invalid.data(),&nan,4);
    require(cbc_set_scene(raw,invalid.data(),invalid.size(),error,sizeof error)!=0,"NaN scene accepted");
    invalid=scene; int root=1; std::memcpy(invalid.data()+16+8,&root,4);
    require(cbc_set_scene(raw,invalid.data(),invalid.size(),error,sizeof error)!=0,"Cyclic BVH accepted");
    require(render(513)!=0,"Oversized image accepted");
    auto saved=camera; camera.forward[0]=nan; require(render()!=0,"NaN camera accepted"); camera=saved;
    std::array<double,3> ms{};
    for(int i=0;i<3;i++) {
      start=Clock::now(); require(render()==0,error);
      ms[i]=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
      if(i==0) first=pixels; else require(pixels==first,"Repeated render changed output");
    }
    // A valid scene replacement must actually change shading; invalid updates
    // above must have preserved the last valid scene.
    invalid=scene; float header[4]; std::memcpy(header,scene.data(),16);
    size_t colors=(size_t(header[3])+3*size_t(header[0]))*16;
    std::fill(invalid.begin()+colors,invalid.end(),0);
    require(cbc_set_scene(raw,invalid.data(),invalid.size(),error,sizeof error)==0,error);
    require(render()==0,error); require(pixels!=first,"Replacement scene did not affect output");
    require(cbc_set_scene(raw,scene.data(),scene.size(),error,sizeof error)==0,error);
    std::fill(scene.begin(),scene.end(),0); // The library must own its uploaded data.
    require(render()==0,error); require(pixels==first,"Restored/copied scene changed output");
    std::string prefix=argv[2]; std::ofstream rawout(prefix+".rgba.bin",std::ios::binary);
    rawout.write(reinterpret_cast<const char*>(pixels.data()),pixels.size()*4); require(bool(rawout),"Output write failed");
    std::ofstream ppm(prefix+".ppm",std::ios::binary); ppm<<"P6\n"<<width<<' '<<height<<"\n255\n";
    for(size_t i=0;i<pixels.size();i++) if(i%4!=3) {
      float x=std::clamp(pixels[i],0.f,1.f); x=x<=.0031308f?12.92f*x:1.055f*std::pow(x,1.f/2.4f)-.055f;
      ppm.put(static_cast<unsigned char>(std::lround(x*255)));
    }
    require(bool(ppm),"Preview write failed");
    // Adapter names here are also emitted as plain text for easy inspection.
    std::cerr<<"Adapter: "<<adapter<<'\n';
    std::ofstream adapterout(prefix+".adapter.txt"); adapterout<<adapter<<'\n';
    std::ofstream libraryout(prefix+".library.txt"); libraryout<<loaded.dli_fname<<'\n';
    std::cout<<"{\"width\":"<<width<<",\"height\":"<<height<<",\"samples\":"<<samples
      <<",\"initializeMilliseconds\":"<<init<<",\"renderMilliseconds\":["<<ms[0]<<','<<ms[1]<<','<<ms[2]
      <<"],\"abiChecksPassed\":true,\"deterministicRepeats\":true,\"sceneReplacementPassed\":true}\n";
  } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
