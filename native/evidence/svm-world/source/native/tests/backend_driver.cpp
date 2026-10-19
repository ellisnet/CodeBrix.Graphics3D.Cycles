// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "cycles_backend.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using Words=std::vector<uint32_t>;
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static Words read(const std::string &path){std::ifstream f(path,std::ios::binary|std::ios::ate);require(bool(f),"Missing input");
  auto size=f.tellg();require(size>0&&size%4==0,"Invalid input size");Words result(size/4);f.seekg(0);f.read(reinterpret_cast<char*>(result.data()),size);require(bool(f),"Input read failed");return result;}
static void write(const std::string &path,const std::vector<float> &v){std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(v.data()),v.size()*4);require(bool(f),"Output write failed");}
static void stats(std::ostream &out,const CBCBackendStats &s){out<<"{\"width\":"<<s.width<<",\"height\":"<<s.height<<",\"samples\":"<<s.samples
  <<",\"bounces\":"<<s.bounces<<",\"batchPaths\":"<<s.batch_paths<<",\"batches\":"<<s.batches<<",\"dispatches\":"<<s.dispatches
  <<",\"sceneBytes\":"<<s.scene_bytes<<",\"scratchBytes\":"<<s.scratch_bytes<<",\"filmBytes\":"<<s.film_bytes
  <<",\"readbackBytes\":"<<s.readback_bytes<<",\"pipelineCreations\":"<<s.pipeline_creations<<",\"bufferCreations\":"<<s.buffer_creations
  <<",\"completedRenders\":"<<s.completed_renders<<",\"sceneGeneration\":"<<s.scene_generation<<"}";}
int main(int argc,char **argv){try{
  require(argc==3||(argc==4&&std::string(argv[3])=="--expect-render-failure"),"Usage: backend_driver SCENE_DIRECTORY OUTPUT_PREFIX [--expect-render-failure]");std::string root=argv[1],prefix=argv[2];
  Words geometry=read(root+"/pathtrace.scene.bin"),camera=read(root+"/camera.scene.bin"),lights=read(root+"/lights.scene.bin");
  const auto original_geometry=geometry,original_camera=camera,original_lights=lights;
  auto scene=[&]{return CBCBackendScene{sizeof(CBCBackendScene),1,geometry.data(),geometry.size()*4,camera.data(),camera.size()*4,lights.data(),lights.size()*4};};
  const size_t floats=size_t(camera[0])*camera[1]*4;std::vector<float> image(floats,37),second(floats),small(floats);
  std::array<char,2048> error{};CBCBackendConfig config{sizeof(config),CBC_BACKEND_ABI_VERSION,4096,0};CBCBackend *raw=nullptr;
  auto check=[&](int result){if(result!=CBC_BACKEND_OK)throw std::runtime_error(error.data());};
  require(cbc_backend_abi_version()==CBC_BACKEND_ABI_VERSION,"ABI mismatch");
  auto bad_config=config;bad_config.abi_version++;require(cbc_backend_create(&bad_config,&raw,error.data(),error.size())==CBC_BACKEND_INVALID&&!raw,"Unsupported ABI accepted");
  check(cbc_backend_create(&config,&raw,error.data(),error.size()));std::unique_ptr<CBCBackend,decltype(&cbc_backend_destroy)> backend(raw,cbc_backend_destroy);
  CBCBackendStats first{};first.struct_size=sizeof(first);
  require(cbc_backend_render(raw,image.data(),image.size(),&first,error.data(),error.size())==CBC_BACKEND_NOT_READY,"Render without scene accepted");
  require(image[0]==37,"Failed render modified caller output");auto descriptor=scene();check(cbc_backend_set_scene(raw,&descriptor,error.data(),error.size()));
  // Verify set_scene owns all uploaded inputs, not caller pointers.
  std::fill(geometry.begin(),geometry.end(),0);std::fill(camera.begin(),camera.end(),0);std::fill(lights.begin(),lights.end(),0);
  if(argc==4){
    require(cbc_backend_render(raw,image.data(),image.size(),&first,error.data(),error.size())==CBC_BACKEND_GPU_ERROR,"Missing GPU stage was accepted");
    require(std::all_of(image.begin(),image.end(),[](float x){return x==37;}),"Failed GPU render modified caller output");
    require(std::string(error.data()).find("Kernel output/completion validation failed")!=std::string::npos,"Failure came from an unexpected source");
    std::cout<<"{\"missingStageDetected\":true,\"callerOutputPreserved\":true,\"message\":\""<<error.data()<<"\"}\n";return 0;
  }
  check(cbc_backend_render(raw,image.data(),image.size(),&first,error.data(),error.size()));write(prefix+".rgba.bin",image);
  CBCBackendStats repeated{};repeated.struct_size=sizeof(repeated);check(cbc_backend_render(raw,second.data(),second.size(),&repeated,error.data(),error.size()));
  require(image==second,"Repeat render differs");require(first.buffer_creations==repeated.buffer_creations&&first.pipeline_creations==repeated.pipeline_creations,"Render recreated GPU resources");
  geometry=original_geometry;camera=original_camera;lights=original_lights;unsigned rejected=0;
  auto reject=[&](int expected){auto d=scene();require(cbc_backend_set_scene(raw,&d,error.data(),error.size())==expected,"Invalid scene was not rejected as expected");rejected++;};
  geometry[2]=0x7fc00000;reject(CBC_BACKEND_INVALID);geometry=original_geometry;
  geometry[6]=std::bit_cast<uint32_t>(int32_t(1));reject(CBC_BACKEND_INVALID);geometry=original_geometry; // Root cycle.
  camera[3]=5;reject(CBC_BACKEND_UNSUPPORTED);camera=original_camera;
  camera[0]=0;reject(CBC_BACKEND_INVALID);camera=original_camera;
  camera[48]=0;reject(CBC_BACKEND_UNSUPPORTED);camera=original_camera;
  lights[0]=0;reject(CBC_BACKEND_UNSUPPORTED);lights=original_lights;
  lights[3*4+3]=0;reject(CBC_BACKEND_INVALID);lights=original_lights;
  const uint32_t triangles=uint32_t(std::bit_cast<float>(geometry[0])),vertices=uint32_t(std::bit_cast<float>(geometry[3]));
  const uint32_t metadata=vertices+4*triangles,code=geometry[metadata*4+1];
  const uint32_t shader=uint32_t(std::bit_cast<float>(geometry[(vertices+3*triangles)*4+3]));
  geometry[code+shader*4]=0xffffffff;reject(CBC_BACKEND_UNSUPPORTED);geometry=original_geometry;
  std::fill(second.begin(),second.end(),37);auto invalid_stats=repeated;invalid_stats.struct_size=0;
  require(cbc_backend_render(raw,second.data(),second.size(),&invalid_stats,error.data(),error.size())==CBC_BACKEND_INVALID,"Invalid stats size accepted");
  require(second[0]==37,"Invalid render modified output");rejected++;
  check(cbc_backend_render(raw,second.data(),second.size(),&repeated,error.data(),error.size()));
  require(image==second&&repeated.scene_generation==1&&repeated.buffer_creations==first.buffer_creations,"Rejected scene damaged previous scene/resources");
  // Valid scene replacement changes lighting, then restoration reproduces the original.
  for(uint32_t i=0;i<lights[0];i++)for(unsigned c=0;c<3;c++){size_t index=(3+i*4+1)*4+c;lights[index]=std::bit_cast<uint32_t>(std::bit_cast<float>(lights[index])*.5f);}
  descriptor=scene();check(cbc_backend_set_scene(raw,&descriptor,error.data(),error.size()));
  check(cbc_backend_render(raw,second.data(),second.size(),&repeated,error.data(),error.size()));require(second!=image,"Scene replacement had no effect");
  lights=original_lights;descriptor=scene();check(cbc_backend_set_scene(raw,&descriptor,error.data(),error.size()));
  check(cbc_backend_render(raw,second.data(),second.size(),&repeated,error.data(),error.size()));require(second==image&&repeated.scene_generation==3,"Scene restoration failed");
  require(repeated.pipeline_creations==first.pipeline_creations,"Scene replacement rebuilt pipelines");
  std::array<char,2048> text{};check(cbc_backend_adapter(raw,text.data(),text.size()));std::ofstream(prefix+".adapter.txt")<<text.data();
  check(cbc_backend_webgpu_library(raw,text.data(),text.size()));std::ofstream(prefix+".library.txt")<<text.data();
  // Non-multiple batch size exercises partial workgroups and sample boundaries.
  config.batch_paths=1021;CBCBackend *other=nullptr;check(cbc_backend_create(&config,&other,error.data(),error.size()));
  std::unique_ptr<CBCBackend,decltype(&cbc_backend_destroy)> small_backend(other,cbc_backend_destroy);
  descriptor=scene();check(cbc_backend_set_scene(other,&descriptor,error.data(),error.size()));CBCBackendStats other_stats{};other_stats.struct_size=sizeof(other_stats);
  check(cbc_backend_render(other,small.data(),small.size(),&other_stats,error.data(),error.size()));require(image==small,"Batch size changed image");
  std::cout<<"{\"abi\":"<<cbc_backend_abi_version()<<",\"invalidCasesRejected\":"<<rejected
    <<",\"callerMemoryOwnershipPassed\":true,\"repeatExact\":true,\"failedScenePreservesResources\":true,\"replacementAndRestoreExact\":true,\"batchSizeExact\":true,\"first\":";
  stats(std::cout,first);std::cout<<",\"final\":";stats(std::cout,repeated);std::cout<<",\"smallerBatch\":";stats(std::cout,other_stats);std::cout<<"}\n";
  return 0;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}}
