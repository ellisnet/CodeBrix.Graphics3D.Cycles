// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// CPU oracle: original Cycles triangle intersection and original BVH traversal.
// Camera and deliberately limited Lambert lighting match the prototype contract.
#include "demo_camera.h"
#include "../../research/native/bvh_reference.cpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <vector>
int main(int argc,char **argv) {
  try {
    if(argc!=3 && argc!=5) throw std::runtime_error("Usage: render_reference SCENE OUTPUT [PIXEL DIAGNOSTIC_PREFIX]");
    std::ifstream in(argv[1],std::ios::binary|std::ios::ate);
    if(!in) throw std::runtime_error("Cannot open scene");
    size_t bytes=in.tellg(); std::vector<float> data(bytes/4); in.seekg(0);
    in.read(reinterpret_cast<char*>(data.data()),bytes);
    if(!in||bytes%16) throw std::runtime_error("Invalid scene file");
    using namespace ccl;
    constexpr unsigned width=256,height=192,samples=8;
    int diagnostic_pixel=argc==5?std::stoi(argv[3]):-1;
    if(diagnostic_pixel>=int(width*height)||diagnostic_pixel< -1) throw std::runtime_error("Invalid diagnostic pixel");
    std::vector<float> diagnostic_input,diagnostic_output;
    auto cam=demo_camera(width,height);
    auto v=[](const float *p) { return make_float3(p[0],p[1],p[2]); };
    auto origin=v(cam.origin),forward=v(cam.forward),right=v(cam.right),up=v(cam.up),light=v(cam.light_position);
    scene=data.data();
    auto trace=[&](float3 p,float3 d,float near,float far,int skip) {
      Ray ray{p,d,near,far,{}}; Intersection hit{}; reference_skip_primitive=skip;
      BVH_bvh_intersect(0,&ray,&hit,0); return hit;
    };
    auto vertex=[&](int row) { auto p=load4(row); return make_float3(p.x,p.y,p.z); };
    std::vector<float> pixels(width*height*4,0); size_t hits=0,shadows=0;
    auto start=std::chrono::steady_clock::now();
    for(unsigned s=0;s<samples;s++) for(unsigned y=0;y<height;y++) for(unsigned x=0;x<width;x++) {
      float jx=(float((s&1)*4+((s>>2)&3))+.5f)/8;
      float jy=(float(((s>>1)&1)*4+((s>>4)&3))+.5f)/8;
      auto direction=normalize(forward+right*((2*((x+jx)/width)-1)*cam.aspect*cam.tan_half_fov)
        +up*((1-2*((y+jy)/height))*cam.tan_half_fov));
      auto hit=trace(origin,direction,.0001f,10000,-1); auto color=v(cam.background);
      auto normal=zero_float3(); float cosine=0,distance_squared=0; int blocker_id=-2;
      if(hit.prim>=0) {
        hits++; int base=int(scene[3])+3*hit.prim;
        auto a=vertex(base),b=vertex(base+1),c=vertex(base+2);
        normal=normalize(cross(b-a,c-a)); if(dot(normal,direction)>0) normal=-normal;
        auto p=origin+direction*hit.t,delta=light-p;
        distance_squared=std::max(dot(delta,delta),.0001f); float distance=std::sqrt(distance_squared);
        auto ld=delta/distance; cosine=std::max(dot(normal,ld),0.f); color=zero_float3();
        if(cosine>0&&distance>.0002f) {
          auto blocker=trace(p+normal*.0001f,ld,.0001f,distance-.0001f,hit.prim);
          blocker_id=blocker.prim;
          if(blocker.prim<0) color=vertex(int(scene[3])+3*int(scene[0])+hit.prim)*(cosine*.3183098861837907f)*(cam.light_power/distance_squared);
          else shadows++;
        }
      }
      if(int(y*width+x)==diagnostic_pixel) {
        diagnostic_input.insert(diagnostic_input.end(),{origin.x,origin.y,origin.z,.0001f,
          forward.x,forward.y,forward.z,cam.tan_half_fov,right.x,right.y,right.z,cam.aspect,
          up.x,up.y,up.z,10000,light.x,light.y,light.z,cam.light_power,
          cam.background[0],cam.background[1],cam.background[2],0,
          float(width),float(height),float(s),float(samples),float(diagnostic_pixel),direction.x,direction.y,direction.z});
        diagnostic_output.insert(diagnostic_output.end(),{direction.x,direction.y,direction.z,hit.t,
          normal.x,normal.y,normal.z,float(hit.prim),cosine,distance_squared,float(blocker_id),0,
          color.x,color.y,color.z,1});
      }
      size_t i=(y*width+x)*4; pixels[i]+=color.x/samples; pixels[i+1]+=color.y/samples;
      pixels[i+2]+=color.z/samples; pixels[i+3]+=1.f/samples;
    }
    double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::ofstream out(argv[2],std::ios::binary); out.write(reinterpret_cast<const char*>(pixels.data()),pixels.size()*4);
    if(!out) throw std::runtime_error("Cannot write reference image");
    if(argc==5) {
      auto write=[&](const char *suffix,const std::vector<float>& values) {
        std::ofstream file(std::string(argv[4])+suffix,std::ios::binary);
        file.write(reinterpret_cast<const char*>(values.data()),values.size()*4);
        if(!file) throw std::runtime_error("Cannot write ray diagnostic");
      };
      write(".input.bin",diagnostic_input); write(".expected.bin",diagnostic_output);
    }
    std::cout<<"{\"originalCyclesTraversal\":true,\"primaryHits\":"<<hits<<",\"shadowedSamples\":"<<shadows<<",\"milliseconds\":"<<ms<<"}\n";
  } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; return 1; }
}
