// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "scene/scene.h"
#include "scene/shader.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "device/device.h"
#include "util/progress.h"
#include "util/profiling.h"
#include "util/stats.h"
#include "util/task.h"
#include "material_reference.h"
#include "kernel/svm/node_types.h"
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <array>
static void write(const std::string &path,const void *data,size_t bytes) {
  std::ofstream out(path,std::ios::binary); out.write(static_cast<const char*>(data),bytes);
  if(!out) throw std::runtime_error("Cannot write "+path);
}
int main(int argc,char **argv) {
  if(argc!=2) { std::cerr<<"Usage: material_host OUTPUT_DIRECTORY\n"; return 2; }
  ccl::TaskScheduler::init(1); int code=0;
  try {
    using namespace ccl;
    static_assert(sizeof(SVMNodeClosureBsdf)==8 && sizeof(SVMNodeDiffuseBsdfData)==20);
    static_assert(sizeof(SVMNodeGeometry)==8 && sizeof(SVMNodeSimpleBsdfData)==8);
    static_assert(offsetof(SVMNodeGeometry,out_offset)==3 && offsetof(SVMNodeDiffuseBsdfData,normal_offset)==16);
    Stats stats; Profiler profiler; DeviceInfo info; info.type=DEVICE_CPU;
    info.id="CodeBrix original Cycles material reference"; info.description=info.id; info.cpu_threads=1;
    auto device=Device::create(info,stats,profiler,true);
    SceneParams params; params.use_texture_cache=false;
    Scene scene(params,device.get());
    std::vector<Shader*> shaders;
    for(int i=0;i<7;i++) {
      auto graph=make_unique<ShaderGraph>(); BsdfNode *node;
      if(i==6) node=graph->create_node<TranslucentBsdfNode>();
      else {
        auto diffuse=graph->create_node<DiffuseBsdfNode>();
        const float roughness[]={0,0,.000001f,.25f,.7f,1};
        diffuse->set_roughness(roughness[i]); node=diffuse;
      }
      node->set_color(i==1?zero_float3():make_float3(.75f,.2f,.08f));
      graph->connect(node->output("BSDF"),graph->output()->input("Surface"));
      auto shader=scene.create_node<Shader>(); shader->set_graph(std::move(graph));
      shader->reference(); shader->tag_update(&scene); shaders.push_back(shader);
    }
    Progress progress; scene.device_update(device.get(),progress);
    if(device->have_error()||progress.get_error()) throw std::runtime_error(device->error_message());
    std::string root=argv[1];
    write(root+"/material.svm.bin",scene.dscene.svm_nodes.data(),scene.dscene.svm_nodes.size()*4);
    std::vector<float> inputs,expected;
    std::vector<int> ids;
    for(auto shader:shaders) {
      int id=scene.shader_manager->get_shader_id(shader)&SHADER_MASK; ids.push_back(id);
      for(int i=0;i<1024;i++) {
        auto N=normalize(make_float3(float((i%7)-3),float((i%5)-2),4));
        if(i==0) N=normalize(make_float3(1,1,1)); // orthonormal fallback branch
        auto wi=normalize(N+make_float3(.2f,-.1f,.1f));
        auto Ng=(i%13==0)?normalize(N+make_float3(2,0,-1)):N;
        auto wo=normalize(make_float3(float(i%11-5),float(i%9-4),float(i%7-3)+.1f));
        float u=float(i%16)/15, v=float(i/16)/15;
        if(i==17) u=v=.5f;
        if(i>=256) {
          auto random=[](uint32_t x) {
            x^=x>>16; x*=0x7feb352d; x^=x>>15; x*=0x846ca68b; x^=x>>16;
            return float(x>>8)*(1.f/16777216.f);
          };
          u=random(uint32_t(i)); v=random(uint32_t(i)+0x917362);
          float cosine=.000001f+.999999f*random(uint32_t(i)+0x123456);
          if(i==256) cosine=0.0000001f;
          float3 T,B; make_orthonormals(N,&T,&B);
          wi=normalize(N*cosine+T*std::sqrt(std::max(1-cosine*cosine,0.f)));
        }
        std::array<float,20> record={N.x,N.y,N.z,float(id),Ng.x,Ng.y,Ng.z,0,
           wi.x,wi.y,wi.z,0,u,v,0,0,wo.x,wo.y,wo.z,0};
        inputs.insert(inputs.end(),record.begin(),record.end());
      }
    }
    int count=inputs.size()/20; expected.resize(count*24);
    evaluate_material_reference(device.get(),inputs.data(),expected.data(),count);
    write(root+"/material.input.bin",inputs.data(),inputs.size()*4);
    write(root+"/material.expected.bin",expected.data(),expected.size()*4);
    std::fill(expected.begin(),expected.end(),0); write(root+"/material.output.bin",expected.data(),expected.size()*4);
    std::cout<<"{\"originalCyclesSvm\":true,\"recordsPerMaterial\":1024,\"records\":"<<count<<",\"words\":"<<scene.dscene.svm_nodes.size()<<",\"shaderIds\":[";
    for(size_t i=0;i<ids.size();i++) std::cout<<(i?",":"")<<ids[i];
    std::cout<<"],\"diffuseId\":"<<CLOSURE_BSDF_DIFFUSE_ID<<",\"orenNayarId\":"<<CLOSURE_BSDF_OREN_NAYAR_ID
      <<",\"translucentId\":"<<CLOSURE_BSDF_TRANSLUCENT_ID<<",\"reflectDiffuseLabel\":"<<(LABEL_REFLECT|LABEL_DIFFUSE)
      <<",\"transmitDiffuseLabel\":"<<(LABEL_TRANSMIT|LABEL_DIFFUSE)
      <<",\"opcodes\":{\"OP_END\":"<<NODE_END<<",\"OP_JUMP\":"<<NODE_SHADER_JUMP
      <<",\"OP_GEOMETRY\":"<<NODE_GEOMETRY<<",\"OP_SET_WEIGHT\":"<<NODE_CLOSURE_SET_WEIGHT
      <<",\"OP_BSDF\":"<<NODE_CLOSURE_BSDF<<"}}\n";
  } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; code=1; }
  ccl::TaskScheduler::exit(); return code;
}
