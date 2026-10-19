// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device.h"
#include "control_reference.h"
#include "integrator/path_trace_work_gpu.h"
#include "scene/scene.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/integrator.h"
#include "util/progress.h"
#include "util/task.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
using namespace ccl;
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
static void healthy(WebGPUDevice &d){if(d.have_error())throw std::runtime_error(d.error_message());}
struct StateFixture {
  struct Field {std::string name;std::vector<uint8_t> expected;std::unique_ptr<device_vector<uint8_t>> memory;};
  WebGPUDevice &d;IntegratorStateGPU gpu{},cpu{};std::vector<Field> fields;unsigned count;
  StateFixture(WebGPUDevice &device,unsigned n):d(device),count(n){
    auto add=[&](const std::string &name,size_t stride,void *gpu_pointer,void *cpu_pointer){
      Field f{name,std::vector<uint8_t>(stride*n),std::make_unique<device_vector<uint8_t>>(&d,name.c_str(),MEM_READ_WRITE)};
      f.memory->alloc(f.expected.size());f.memory->zero_to_device();healthy(d);
      std::memcpy(gpu_pointer,&f.memory->device_pointer,sizeof(device_ptr));void *host=f.expected.data();std::memcpy(cpu_pointer,&host,sizeof(host));fields.push_back(std::move(f));};
#define KERNEL_STRUCT_BEGIN(name) for(unsigned array_index=0;;array_index++) {
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature) KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) add(#parent "." #name,sizeof(type),&gpu.parent.name,&cpu.parent.name);
#define KERNEL_STRUCT_MEMBER_PACKED KERNEL_STRUCT_MEMBER
#define KERNEL_STRUCT_ARRAY_MEMBER(parent,type,name,feature) add(#parent "." #name+std::to_string(array_index),sizeof(type),&gpu.parent[array_index].name,&cpu.parent[array_index].name);
#define KERNEL_STRUCT_END(name) (void)array_index;break; }
#define KERNEL_STRUCT_END_ARRAY(name,cpu_size,gpu_size) if(array_index>=gpu_size-1)break; }
#define KERNEL_STRUCT_VOLUME_STACK_SIZE 2
#include "kernel/integrator/state_template.h"
#include "kernel/integrator/shadow_state_template.h"
#undef KERNEL_STRUCT_BEGIN
#undef KERNEL_STRUCT_BEGIN_PACKED
#undef KERNEL_STRUCT_MEMBER
#undef KERNEL_STRUCT_MEMBER_PACKED
#undef KERNEL_STRUCT_ARRAY_MEMBER
#undef KERNEL_STRUCT_END
#undef KERNEL_STRUCT_END_ARRAY
#undef KERNEL_STRUCT_VOLUME_STACK_SIZE
    KernelData data{};data.volume_stack_size=2;data.kernel_features=KERNEL_FEATURE_PATH_TRACING;
    d.const_copy_to("data",&data,sizeof(data));d.const_copy_to("integrator_state",&gpu,sizeof(gpu));healthy(d);seed();
  }
  void seed(){unsigned salt=0;for(auto &f:fields){++salt;for(size_t i=0;i<f.expected.size();i++){
        uint32_t v=uint32_t(i+1)*747796405u+salt*2891336453u;v=(v^(v>>16))*2246822519u;f.expected[i]=uint8_t(v>>24);}}
    for(unsigned i=0;i<count;i++){
      cpu.path.queued_kernel[i]=i%5==0?0:(i%3?DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE:DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST);
      cpu.shadow_path.queued_kernel[i]=i%7==0?0:DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW;
      cpu.path.shader_sort_key[i]=(i*13+7)%29;
    }
  }
  void upload(){for(auto &f:fields){std::memcpy(f.memory->data(),f.expected.data(),f.expected.size());f.memory->copy_to_device();}healthy(d);}
  void compare(){for(auto &f:fields){f.memory->copy_from_device();healthy(d);
      if(std::memcmp(f.memory->data(),f.expected.data(),f.expected.size()))throw std::runtime_error("State mismatch: "+f.name);}}
};
struct SchedulerProbe:PathTraceWorkGPU {
  using PathTraceWorkGPU::PathTraceWorkGPU;
  using PathTraceWorkGPU::enqueue_reset;
  using PathTraceWorkGPU::update_queue_counter_and_cache;
  int paths()const{return max_num_paths_;}
};
static unsigned scene_and_scheduler(WebGPUDevice &d){
  SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=false;params.use_texture_cache=false;
  Scene scene(params,&d);scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);
  auto mesh=scene.create_node<Mesh>();array<Node*> shaders;shaders.push_back_slow(scene.default_surface);mesh->set_used_shaders(shaders);mesh->resize_mesh(6,2);
  const float points[][3]={{-1,-1,0},{1,-1,0},{1,1,0},{-1,-1,0},{1,1,0},{-1,1,0}};
  for(int i=0;i<6;i++){mesh->get_position_for_write()[i]=make_float3(points[i][0],points[i][1],points[i][2]);mesh->get_triangles()[i]=i;}
  std::ranges::fill(mesh->get_smooth(),false);std::ranges::fill(mesh->get_shader(),0);mesh->tag_triangles_modified();
  auto object=scene.create_node<Object>();object->set_geometry(mesh);object->set_tfm(transform_identity());
  Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Scene preparation failed");
  unsigned checked=0;
  auto check=[&](auto &buffer){if(!buffer.memory_size())return;std::vector<uint8_t> expected(buffer.memory_size()),actual(expected.size());
    std::memcpy(expected.data(),buffer.data(),expected.size());d.download(buffer.device_pointer,actual.data(),actual.size());require(expected==actual,"Prepared Cycles table upload differs");checked++;};
  check(scene.dscene.bvh_nodes);check(scene.dscene.bvh_leaf_nodes);check(scene.dscene.prim_index);
  check(scene.dscene.tri_verts);check(scene.dscene.tri_vindex);check(scene.dscene.tri_shader);check(scene.dscene.svm_nodes);
  check(scene.dscene.objects);check(scene.dscene.shaders);check(scene.dscene.lookup_table);check(scene.dscene.sample_pattern_lut);
  require(checked>=9&&d.globals.size()>=9,"Missing uploaded scene tables");
  KernelData copy{};d.download(d.constants.at("data"),&copy,sizeof(copy));require(std::memcmp(&copy,&scene.dscene.data,sizeof(copy))==0,"KernelData upload differs");
  bool cancelled=false;
  auto factory=PathTraceWork::create(&d,scene.film,&scene.dscene,&cancelled);
  require(dynamic_cast<PathTraceWorkGPU*>(factory.get())!=nullptr,"Original path-work factory did not select GPU scheduler");factory.reset();
  SchedulerProbe scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);
  std::vector<uint16_t> main(scheduler.paths(),23),shadow(scheduler.paths(),29);
  d.upload(uintptr_t(d.state.path.queued_kernel),main.data(),main.size()*2);
  d.upload(uintptr_t(d.state.shadow_path.queued_kernel),shadow.data(),shadow.size()*2);
  scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Original scheduler reset failed");healthy(d);
  d.download(uintptr_t(d.state.path.queued_kernel),main.data(),main.size()*2);d.download(uintptr_t(d.state.shadow_path.queued_kernel),shadow.data(),shadow.size()*2);
  require(std::all_of(main.begin(),main.end(),[](auto v){return v==0;})&&std::all_of(shadow.begin(),shadow.end(),[](auto v){return v==0;}),"Original scheduler did not reset both path arrays");
  return checked;
}
static unsigned selections(WebGPUDevice &d,DeviceQueue &queue,StateFixture &f){
  const int n=int(f.count);device_vector<int> indices(&d,"indices",MEM_READ_WRITE),count(&d,"count",MEM_READ_WRITE);
  indices.alloc(n+32);count.alloc(1);unsigned cases=0;
  for(auto kernel:{DEVICE_KERNEL_INTEGRATOR_QUEUED_PATHS_ARRAY,DEVICE_KERNEL_INTEGRATOR_QUEUED_SHADOW_PATHS_ARRAY,
      DEVICE_KERNEL_INTEGRATOR_ACTIVE_PATHS_ARRAY,DEVICE_KERNEL_INTEGRATOR_TERMINATED_PATHS_ARRAY,
      DEVICE_KERNEL_INTEGRATOR_TERMINATED_SHADOW_PATHS_ARRAY,DEVICE_KERNEL_INTEGRATOR_COMPACT_PATHS_ARRAY,
      DEVICE_KERNEL_INTEGRATOR_COMPACT_SHADOW_PATHS_ARRAY}){
    std::fill_n(indices.data(),indices.size(),-1);indices.copy_to_device();queue.zero_to_device(count);
    bool shadow=kernel==DEVICE_KERNEL_INTEGRATOR_QUEUED_SHADOW_PATHS_ARRAY||kernel==DEVICE_KERNEL_INTEGRATOR_TERMINATED_SHADOW_PATHS_ARRAY||kernel==DEVICE_KERNEL_INTEGRATOR_COMPACT_SHADOW_PATHS_ARRAY;
    bool terminated=kernel==DEVICE_KERNEL_INTEGRATOR_TERMINATED_PATHS_ARRAY||kernel==DEVICE_KERNEL_INTEGRATOR_TERMINATED_SHADOW_PATHS_ARRAY;
    bool compact=kernel==DEVICE_KERNEL_INTEGRATOR_COMPACT_PATHS_ARRAY||kernel==DEVICE_KERNEL_INTEGRATOR_COMPACT_SHADOW_PATHS_ARRAY;
    bool active=kernel==DEVICE_KERNEL_INTEGRATOR_ACTIVE_PATHS_ARRAY;
    int arg=terminated?17:(compact?n/2:(shadow?DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW:DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE));
    DeviceKernelArguments args(&n,&indices.device_pointer,&count.device_pointer);if(!active)args.add(&arg);
    require(queue.enqueue(kernel,n,args),"Selection dispatch failed");queue.copy_from_device(count);queue.copy_from_device(indices);require(queue.synchronize(),"Selection sync failed");
    std::vector<int> expected;auto queued=shadow?f.cpu.shadow_path.queued_kernel:f.cpu.path.queued_kernel;
    for(int i=0;i<n;i++)if(terminated?queued[i]==0:(compact?(i>=arg&&queued[i]!=0):(active?queued[i]!=0:queued[i]==arg)))expected.push_back(i);
    require(count[0]==int(expected.size()),"Selection count differs");const int offset=terminated?arg:0;
    std::vector<int> actual(indices.data()+offset,indices.data()+offset+count[0]);std::sort(actual.begin(),actual.end());require(actual==expected,"Selection membership differs");
    for(int i=0;i<int(indices.size());i++)if(i<offset||i>=offset+count[0])require(indices[i]==-1,"Selection changed an output guard");
    cases++;
  }return cases;
}
static unsigned sorting(WebGPUDevice &d,DeviceQueue &queue,StateFixture &f){
  const int n=int(f.count),keys=29,kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE;
  device_vector<int> counter(&d,"sort_counter",MEM_READ_WRITE),prefix(&d,"prefix",MEM_READ_WRITE),indices(&d,"sorted",MEM_READ_WRITE),unused(&d,"unused",MEM_READ_WRITE);
  counter.alloc(keys);prefix.alloc(keys);indices.alloc(n);unused.alloc(1);
  unsigned cases=0;
  for(int limit:{0,1,64,n/2,n,n+17}){
    std::vector<int> expected_counter(keys),expected_prefix(keys),expected_indices(n,-1);
    for(int i=0;i<n;i++)if(f.cpu.path.queued_kernel[i]==kernel)expected_counter[f.cpu.path.shader_sort_key[i]]++;
    std::copy(expected_counter.begin(),expected_counter.end(),counter.data());counter.copy_to_device();prefix.zero_to_device();unused.zero_to_device();
    std::fill_n(indices.data(),n,-1);indices.copy_to_device();
    DeviceKernelArguments prefix_args(&counter.device_pointer,&prefix.device_pointer,&keys);
    require(queue.enqueue(DEVICE_KERNEL_PREFIX_SUM,1,prefix_args),"Prefix dispatch failed");
    original_prefix(expected_counter.data(),expected_prefix.data(),keys);
    DeviceKernelArguments args(&n,&limit,&indices.device_pointer,&unused.device_pointer,&counter.device_pointer,&prefix.device_pointer,&kernel);
    require(queue.enqueue(DEVICE_KERNEL_INTEGRATOR_SORTED_PATHS_ARRAY,n,args),"Sorted dispatch failed");
    original_sorted(f.cpu.path.queued_kernel,f.cpu.path.shader_sort_key,n,limit,kernel,expected_indices.data(),expected_counter.data(),expected_prefix.data());
    queue.copy_from_device(counter);queue.copy_from_device(prefix);queue.copy_from_device(indices);queue.copy_from_device(unused);require(queue.synchronize(),"Sort sync failed");
    require(std::equal(expected_counter.begin(),expected_counter.end(),counter.data()),"Deferred sort counters differ from original helper");
    require(std::equal(expected_prefix.begin(),expected_prefix.end(),prefix.data()),"Prefix counters differ from original helper");require(unused[0]==0,"Sorted helper changed unused counter");
    std::set<int> seen;
    for(int i=0;i<n;i++){
      if(expected_indices[i]<0)require(indices[i]==-1,"Sorted output guard changed");
      else{int path=indices[i];require(path>=0&&path<n&&seen.insert(path).second,"Invalid/duplicate sorted index");
        require(f.cpu.path.queued_kernel[path]==kernel&&f.cpu.path.shader_sort_key[path]==f.cpu.path.shader_sort_key[expected_indices[i]],"Sorted bucket membership differs from original helper");}
    }cases++;
  }return cases;
}
static void allocation_ordering(WebGPUDevice &d){
  auto queue=d.gpu_queue_create();device_vector<int> counter(&d,"growth_counter",MEM_READ_WRITE),prefix(&d,"growth_prefix",MEM_READ_WRITE);
  const int n=65;counter.alloc(n);prefix.alloc(n);for(int i=0;i<n;i++)counter[i]=i+1;counter.copy_to_device();prefix.zero_to_device();
  const auto original_pointer=prefix.device_pointer;
  DeviceKernelArguments args(&counter.device_pointer,&prefix.device_pointer,&n);require(queue->enqueue(DEVICE_KERNEL_PREFIX_SUM,1,args),"Growth test prefix failed");
  const auto previous_growths=d.growths;
  const size_t large_size=d.arena_bytes+4096;
  device_only_memory<uint8_t> large(&d,"force_arena_growth");large.alloc_to_device(large_size);healthy(d);
  require(d.growths>previous_growths&&prefix.device_pointer==original_pointer,"Arena growth changed virtual addresses");
  prefix.copy_from_device();healthy(d);for(int i=0;i<n;i++)require(prefix[i]==i*(i+1)/2,"Arena growth lost queued GPU writes");
  const auto address=large.device_pointer;large.free();device_only_memory<uint8_t> reused(&d,"reused_range");reused.alloc_to_device(large_size);healthy(d);
  require(reused.device_pointer==address,"Free ranges were not reused");
  // A queued GPU write followed by a host upload must preserve submission order.
  for(int i=0;i<n;i++)counter[i]=1;
  counter.copy_to_device();require(queue->enqueue(DEVICE_KERNEL_PREFIX_SUM,1,args),"Ordered prefix failed");
  for(int i=0;i<n;i++)prefix[i]=1234;
  prefix.copy_to_device();prefix.copy_from_device();healthy(d);
  require(std::all_of(prefix.data(),prefix.data()+n,[](int v){return v==1234;}),"Host upload overtook pending kernel");
  // The native memory interface must also preserve unaligned readback slices.
  device_vector<uint8_t> bytes(&d,"byte_slice",MEM_READ_WRITE);bytes.alloc(67);for(unsigned i=0;i<67;i++)bytes[i]=uint8_t(i+3);bytes.copy_to_device();
  std::fill_n(bytes.data(),67,255);d.mem_copy_from(bytes,3,1,19,1);healthy(d);
  for(unsigned i=0;i<67;i++)require(bytes[i]==(i>=3&&i<22?i+3:255),"Partial byte readback changed a guard");
}
static unsigned rejections(const DeviceInfo &info){
  unsigned cases=0;
  for(unsigned test=0;test<7;test++){
    Stats stats;Profiler profiler;auto owner=Device::create(info,stats,profiler,true);auto *backend=dynamic_cast<WebGPUDevice*>(owner.get());require(backend,"Negative-test device creation failed");
    auto &d=*backend;auto queue=d.gpu_queue_create();device_vector<int> counter(&d,"invalid_counter",MEM_READ_WRITE),prefix(&d,"invalid_prefix",MEM_READ_WRITE);
    counter.alloc(1);prefix.alloc(1);counter.zero_to_device();prefix.zero_to_device();healthy(d);
    int one=1,two=2;device_ptr null=0;bool result=true;std::string reason;
    if(test==0){DeviceKernelArguments args(&counter.device_pointer,&prefix.device_pointer,&one);result=queue->enqueue(DEVICE_KERNEL_PREFIX_SUM,-1,args);reason="work size";}
    if(test==1){DeviceKernelArguments args(&null,&prefix.device_pointer,&one);result=queue->enqueue(DEVICE_KERNEL_PREFIX_SUM,1,args);reason="virtual pointer";}
    if(test==2){DeviceKernelArguments args(&one,&prefix.device_pointer,&one);result=queue->enqueue(DEVICE_KERNEL_PREFIX_SUM,1,args);reason="pointer kernel argument";}
    if(test==3){DeviceKernelArguments args(&counter.device_pointer,&prefix.device_pointer,&two);result=queue->enqueue(DEVICE_KERNEL_PREFIX_SUM,1,args);reason="range outside allocation";}
    if(test==4){DeviceKernelArguments args(&two);result=queue->enqueue(DEVICE_KERNEL_INTEGRATOR_RESET,1,args);reason="Incomplete reset";}
    if(test==5){KernelData data{};data.kernel_features=KERNEL_FEATURE_MNEE;d.const_copy_to("data",&data,sizeof(data));
      DeviceKernelArguments args(&counter.device_pointer,&one,&one,&one);result=queue->enqueue(DEVICE_KERNEL_INTEGRATOR_COMPACT_STATES,1,args);reason="MNEE ownership";}
    if(test==6){StateFixture f(d,3);f.cpu.path.shader_sort_key[1]=0;f.cpu.path.shader_sort_key[2]=1;f.upload();
      device_vector<int> indices(&d,"bad_key_indices",MEM_READ_WRITE);indices.alloc(3);indices.zero_to_device();
      int n=3,kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE;
      DeviceKernelArguments args(&n,&n,&indices.device_pointer,&counter.device_pointer,&counter.device_pointer,&prefix.device_pointer,&kernel);
      require(queue->enqueue(DEVICE_KERNEL_INTEGRATOR_SORTED_PATHS_ARRAY,n,args),"Bad key should be detected by the GPU data check");
      result=queue->synchronize();reason="scheduler kernel reported invalid data";}
    require(!result&&d.error_message().find(reason)!=std::string::npos,"Negative case did not report the expected error");cases++;
  }
  return cases;
}
int main(int argc,char **argv){TaskScheduler::init(1);int result=0;try{
  require(argc==2,"Usage: codebrix_cycles_device_gate OUTPUT_PREFIX");std::string output=argv[1];
  require(Device::type_from_string("WEBGPU")==DEVICE_WEBGPU&&Device::string_from_type(DEVICE_WEBGPU)=="WEBGPU","Device type registration missing");
  auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Registered WebGPU adapter missing");
  Stats stats;Profiler profiler;auto device=Device::create(infos[0],stats,profiler,true);auto *backend=dynamic_cast<WebGPUDevice*>(device.get());
  if(!backend)throw std::runtime_error("Device factory: "+device->error_message());
  auto &d=*backend;healthy(d);
  unsigned scene_tables=scene_and_scheduler(d);require(stats.mem_used==0,"Scene/scheduler leaked Cycles allocations");
  allocation_ordering(d);require(stats.mem_used==0,"Allocation-order tests leaked Cycles allocations");
  const unsigned rejected=rejections(infos[0]);
  unsigned reset_cases=0,selection_cases=0,sort_cases=0,compaction_cases=0,field_count=0;
  {
    StateFixture f(d,4099);field_count=unsigned(f.fields.size());auto queue=d.gpu_queue_create();
    for(int n:{0,1,63,64,65,193,4099}){
      f.seed();f.upload();DeviceKernelArguments args(&n);require(queue->enqueue(DEVICE_KERNEL_INTEGRATOR_RESET,n,args),"Reset dispatch failed");
      require(queue->synchronize(),"Reset sync failed");std::fill_n(f.cpu.path.queued_kernel,n,0);std::fill_n(f.cpu.shadow_path.queued_kernel,n,0);f.compare();reset_cases++;
    }
    f.seed();f.upload();selection_cases=selections(d,*queue,f);sort_cases=sorting(d,*queue,f);
    for(bool shadow:{false,true}){
      f.seed();const int n=193,active=0,terminated=n;
      device_vector<int> pairs(&d,"compaction_pairs",MEM_READ_WRITE);pairs.alloc(2*n);
      for(int i=0;i<n;i++){pairs[i]=2048+i;pairs[n+i]=i*2+1;(shadow?f.cpu.shadow_path.queued_kernel:f.cpu.path.queued_kernel)[pairs[n+i]]=0;}
      f.upload();pairs.copy_to_device();for(int i=0;i<n;i++)original_move(f.cpu,2,shadow,pairs[n+i],pairs[i]);
      DeviceKernelArguments args(&pairs.device_pointer,&active,&terminated,&n);
      require(queue->enqueue(shadow?DEVICE_KERNEL_INTEGRATOR_COMPACT_SHADOW_STATES:DEVICE_KERNEL_INTEGRATOR_COMPACT_STATES,n,args),"Compaction dispatch failed");
      require(queue->synchronize(),"Compaction sync failed");f.compare();compaction_cases++;
    }
    require(!queue->enqueue(DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,1,DeviceKernelArguments()),"Unimplemented shading kernel was accepted");
    require(d.error_message().find("kernel not implemented")!=std::string::npos,"Unsupported kernel did not report the reason");
  }
  require(stats.mem_used==0,"Control tests leaked Cycles allocations");
  std::ofstream(output+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(output+".library.txt")<<d.gpu.library_path;
  std::cout<<"{\"passed\":true,\"registeredCyclesDevice\":true,\"originalGpuSchedulerReset\":true,\"preparedSceneTables\":"<<scene_tables
    <<",\"resetCases\":"<<reset_cases<<",\"selectionCases\":"<<selection_cases<<",\"sortCases\":"<<sort_cases<<",\"compactionCases\":"<<compaction_cases
    <<",\"nativeStateFields\":"<<field_count<<",\"states\":4099,\"unsupportedKernelRejected\":true,\"invalidCasesRejected\":"<<rejected
    <<",\"growthPreservesGpuWrites\":true,\"uploadOrderingPassed\":true,\"partialReadbackPassed\":true,\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used
    <<",\"arenaBytes\":"<<d.arena_bytes<<",\"arenaGrowths\":"<<d.growths<<",\"gpuDispatches\":"<<d.dispatches
    <<",\"gpuSubmissions\":"<<d.submissions<<",\"uploads\":"<<d.uploads<<",\"downloads\":"<<d.downloads<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';result=1;}TaskScheduler::exit();return result;}
