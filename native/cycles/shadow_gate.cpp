// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "wavefront_test.h"
#include "shadow_reference.h"
#include "wavefront.h"
#include "render_capture.h"
#include "scene/scene.h"
#include "object_motion_fixture.h"
#include "deforming_motion_fixture.h"
#include "precision_fixture.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/integrator.h"
#include "util/progress.h"
#include "util/task.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
using namespace ccl;
using wavefront_test::require;
using wavefront_test::healthy;
using States=std::vector<IntegratorShadowStateCPU>;
struct ShadowScheduler:wavefront_test::Scheduler {
  using Scheduler::Scheduler;
  void active(int n){integrator_next_shadow_path_index_[0]=n;integrator_next_shadow_path_index_.copy_to_device();}
};
struct Field {std::string name;uint32_t address;size_t stride,offset;};
static std::vector<Field> fields(WebGPUDevice &d){std::vector<Field> out;
#define KERNEL_STRUCT_BEGIN(name) for(int index=0;;index++){
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature) KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) if(d.state.parent.name)out.push_back({#parent "." #name,uint32_t(uintptr_t(d.state.parent.name)),sizeof(type),offsetof(IntegratorShadowStateCPU,parent.name)});
#define KERNEL_STRUCT_MEMBER_PACKED KERNEL_STRUCT_MEMBER
#define KERNEL_STRUCT_ARRAY_MEMBER(parent,type,name,feature) if(d.state.parent[index].name)out.push_back({#parent "." #name+std::to_string(index),uint32_t(uintptr_t(d.state.parent[index].name)),sizeof(type),offsetof(IntegratorShadowStateCPU,parent)+size_t(index)*sizeof(IntegratorShadowStateCPU{}.parent[0])+offsetof(std::remove_reference_t<decltype(IntegratorShadowStateCPU{}.parent[0])>,name)});
#define KERNEL_STRUCT_END(name) (void)index;break;}
#define KERNEL_STRUCT_END_ARRAY(name,cpu_size,gpu_size) if(index>=gpu_size-1)break;}
#define KERNEL_STRUCT_VOLUME_STACK_SIZE d.data.volume_stack_size
#include "kernel/integrator/shadow_state_template.h"
#undef KERNEL_STRUCT_BEGIN
#undef KERNEL_STRUCT_BEGIN_PACKED
#undef KERNEL_STRUCT_MEMBER
#undef KERNEL_STRUCT_MEMBER_PACKED
#undef KERNEL_STRUCT_ARRAY_MEMBER
#undef KERNEL_STRUCT_END
#undef KERNEL_STRUCT_END_ARRAY
#undef KERNEL_STRUCT_VOLUME_STACK_SIZE
  return out;
}
static void upload(WebGPUDevice &d,const std::vector<Field> &layout,const States &states){
  for(auto &f:layout){std::vector<uint8_t> bytes(states.size()*f.stride);
    for(size_t i=0;i<states.size();i++)std::memcpy(bytes.data()+i*f.stride,reinterpret_cast<const uint8_t*>(&states[i])+f.offset,f.stride);
    d.upload(f.address,bytes.data(),bytes.size());}
}
struct Metrics {uint64_t exact=0,floats=0,strict=0,paths=0,blocked=0,clear=0,excluded=0;unsigned cases=0,direct=0,instances=0;double maximum=0;};
static void compare(WebGPUDevice &d,const std::vector<Field> &layout,const States &expected,Metrics &m){
  for(auto &f:layout){std::vector<uint8_t> bytes(expected.size()*f.stride);d.download(f.address,bytes.data(),bytes.size());
    for(size_t i=0;i<expected.size();i++){m.exact++;require(std::memcmp(bytes.data()+i*f.stride,reinterpret_cast<const uint8_t*>(&expected[i])+f.offset,f.stride)==0,
      "Shadow state mismatch "+f.name+" slot "+std::to_string(i)+" got_first_byte="+std::to_string(bytes[i*f.stride])+" expected_first_byte="+std::to_string(*(reinterpret_cast<const uint8_t*>(&expected[i])+f.offset)));}}
  IntegratorQueueCounter counts{};for(auto &s:expected)if(s.shadow_path.queued_kernel)counts.num_queued[s.shadow_path.queued_kernel]++;
  IntegratorQueueCounter actual{};d.download(uintptr_t(d.state.queue_counter),&actual,sizeof(actual));
  require(std::memcmp(&actual,&counts,sizeof(counts))==0,"Shadow queue counters differ");healthy(d);
}
static void film_compare(WebGPUDevice &d,device_vector<float> &film,const std::vector<float> &expected,Metrics &m){
  film.copy_from_device();healthy(d);
  for(size_t i=0;i<expected.size();i++){double error=std::abs(double(film[i])-expected[i]);m.maximum=std::max(m.maximum,error);m.floats++;
    m.strict+=error>1e-7+1e-7*std::abs(double(expected[i]));
    require(std::isfinite(film[i])&&error<=5e-5+2e-4*std::abs(double(expected[i])),"Shadow film mismatch at "+std::to_string(i));}
}
static void prepare(Scene &scene){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);
  auto mesh=scene.create_node<Mesh>();array<Node*> shaders;shaders.push_back_slow(scene.default_surface);mesh->set_used_shaders(shaders);mesh->resize_mesh(4,2);
  const float positions[][3]={{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};
  for(int i=0;i<4;i++)mesh->get_position_for_write()[i]=make_float3(positions[i][0],positions[i][1],positions[i][2]);
  const int triangles[]={0,1,2,0,2,3};std::copy_n(triangles,6,mesh->get_triangles().data());
  std::ranges::fill(mesh->get_smooth(),false);std::ranges::fill(mesh->get_shader(),0);mesh->tag_triangles_modified();
  for(int i=0;i<4;i++){auto object=scene.create_node<Object>();object->set_geometry(mesh);
    object->set_tfm(transform_translate(make_float3((i-1)*2.7f,0,i*.5f))*transform_scale(make_float3(i==2?-1.2f:1.f,.8f+.2f*i,1.5f)));
    if(i==3)object->set_visibility(PATH_RAY_VISIBILITY_CAMERA);}
}
static void configure(KernelData &data){
  data.integrator.transparent_shadows=0;data.kernel_features|=KERNEL_FEATURE_LIGHT_PASSES;
  data.film.pass_stride=48;data.film.light_pass_flag=PASSMASK(COMBINED);data.film.pass_combined=0;data.film.pass_adaptive_aux_buffer=4;
  data.film.pass_diffuse_direct=8;data.film.pass_glossy_direct=11;data.film.pass_transmission_direct=14;
  data.film.pass_diffuse_indirect=17;data.film.pass_glossy_indirect=20;data.film.pass_transmission_indirect=23;data.film.pass_lightgroup=26;
  data.integrator.sample_clamp_direct=.7f;data.integrator.sample_clamp_indirect=.23f;
}
static States initial(int count,int variant,bool motion=false){
  States states(count);
  for(int i=0;i<count;i++){auto &s=states[i];auto &p=s.shadow_path;
    p.render_pixel_index=i;p.sample=i%16;p.bounce=i%3;p.visibility=PATH_RAY_VISIBILITY_DIFFUSE;p.flag=i%7?PATH_RAY_SURFACE_PASS:0;
    p.throughput=make_float3((i%11)*.13f,-.18f,.39f);p.pass_diffuse_weight=make_float3(.1f,.2f,.3f);p.pass_glossy_weight=make_float3(.4f,.3f,.2f);
    if(i%31==0)p.throughput=make_float3(std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),.4f);
    p.lightgroup=i%3;p.packed_num_hits=333;p.rng_offset=17;p.rng_pixel=123456+i;p.transparent_bounce=i%4;
    s.shadow_ray.P=make_float3((i%4-1)*2.7f+.31f,-.23f,-3);s.shadow_ray.D=make_float3(0,0,1);
    s.shadow_ray.tmin=variant==3?4.f:0.f;s.shadow_ray.tmax=variant==4?2.f:100.f;s.shadow_ray.time=motion?float(i)/float(std::max(1,count-1)):.5f;
    s.shadow_ray.self_light_object=OBJECT_NONE;s.shadow_ray.self_light_prim=PRIM_NONE;
    for(auto &hit:s.shadow_isect){hit.object=OBJECT_NONE;hit.prim=PRIM_NONE;}
    if(variant==1||variant==2){
      const int object=i%4;const int prim=i%2;
      if(variant==1){s.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].object=object;s.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].prim=prim;
        s.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_CPU-1]=s.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1];}
      else{s.shadow_ray.self_light_object=object;s.shadow_ray.self_light_prim=prim;}
    }
    if(variant==5){
      if(i%3==0)s.shadow_ray.D=zero_float3();
      if(i%3==1)s.shadow_ray.P.x=std::numeric_limits<float>::infinity();
      if(i%3==2)s.shadow_ray.tmin=std::numeric_limits<float>::max();
    }
  }
  return states;
}
// Traversal precision: near-tangent shadow rays toward sliver triangles and an
// instanced emissive quad (light self-exclusion on the aimed emitter triangle,
// grazing its diagonal neighbour), tmax within 1e-6 of the target, inputs
// perturbed by 0-2 ULP. Blocked decisions must equal the CPU oracle exactly.
struct ShadowPrecision {uint64_t paths=0,blocked=0,clear=0,emitter_paths=0;unsigned instances=0;};
static ShadowPrecision shadow_precision(const DeviceInfo &info){
  ShadowPrecision p;Stats stats;Profiler profiler;auto owner=Device::create(info,stats,profiler,true);auto &d=require_webgpu_device(owner.get(),"shadow_gate device creation");
  {
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=false;params.use_texture_cache=false;
    Scene scene(params,&d);scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);
    precision_fixture(scene);Progress progress;scene.device_update(&d,progress);healthy(d);configure(scene.dscene.data);
    for(size_t i=0;i<scene.dscene.bvh_leaf_nodes.size();i++)p.instances+=scene.dscene.bvh_leaf_nodes[i].x<0;require(p.instances>=5,"Shadow precision fixture lost its instances");
    d.const_copy_to("data",&scene.dscene.data,sizeof(KernelData));bool cancel=false;ShadowScheduler scheduler(&d,scene.film,&scene.dscene,&cancel);
    scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);const auto layout=fields(d);
    const int n=std::min(scheduler.capacity(),4096);const auto rays=precision_rays(scene,n,true);
    scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Shadow precision reset failed");
    auto expected=initial(n,0);std::vector<int> indices;
    for(int i=0;i<n;i++){auto &s=expected[i];s.shadow_path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW;
      s.shadow_ray.P=rays[i].P;s.shadow_ray.D=rays[i].D;s.shadow_ray.tmin=0;s.shadow_ray.tmax=rays[i].tmax;s.shadow_ray.time=.5f;
      const bool emitter=rays[i].object>=3;p.emitter_paths+=emitter;
      if(emitter||i%2){s.shadow_ray.self_light_object=rays[i].object;s.shadow_ray.self_light_prim=rays[i].prim;}
      indices.push_back(i);}
    upload(d,layout,expected);IntegratorQueueCounter counter{};counter.num_queued[DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW]=n;d.upload(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
    scheduler.active(n);require(scheduler.update_queue_counter_and_cache(),"Shadow precision counters failed");
    std::vector<float> film(48);original_shadow(scene.dscene,DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,indices,expected,film);
    scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW);require(scheduler.update_queue_counter_and_cache(),"Shadow precision intersect failed: "+d.error_message());
    std::vector<uint16_t> queued(n);d.download(uintptr_t(d.state.shadow_path.queued_kernel),queued.data(),queued.size()*2);uint64_t mismatches=0;
    for(int i=0;i<n;i++){mismatches+=queued[i]!=expected[i].shadow_path.queued_kernel;if(expected[i].shadow_path.queued_kernel)p.clear++;else p.blocked++;}
    p.paths=n;std::cerr<<"shadow precision paths="<<p.paths<<" blocked="<<p.blocked<<" unblocked="<<p.clear<<" emitterPaths="<<p.emitter_paths<<" decisionMismatches="<<mismatches<<"\n";
    require(mismatches==0,"Shadow precision decisions differ from the CPU oracle: "+std::to_string(mismatches));
    Metrics full;compare(d,layout,expected,full); // every state field and queue counter, bit for bit
    require(p.blocked>200&&p.clear>200&&p.emitter_paths>200,"Missing shadow precision coverage");
  }
  require(stats.mem_used==0,"Shadow precision leaked Cycles allocations");return p;
}
static unsigned rejections(const DeviceInfo &info){unsigned rejected=0;
  for(int test=0;test<18;test++){
    Stats stats;Profiler profiler;auto owner=Device::create(info,stats,profiler,true);auto &d=require_webgpu_device(owner.get(),"shadow_gate device creation");
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=false;params.use_texture_cache=false;
    Scene scene(params,&d);prepare(scene);Progress progress;scene.device_update(&d,progress);healthy(d);configure(scene.dscene.data);
    d.const_copy_to("data",&scene.dscene.data,sizeof(KernelData));bool cancel=false;ShadowScheduler scheduler(&d,scene.film,&scene.dscene,&cancel);
    scheduler.alloc_work_memory();scheduler.init_execution();scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Shadow rejection setup failed");
    const bool shading=test>=9;const auto kernel=shading?DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW:DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW;
    States states=initial(1,0);states[0].shadow_path.queued_kernel=kernel;states[0].shadow_path.packed_num_hits=0;
    auto &data=scene.dscene.data;std::string reason="reported invalid data";
    if(test==0){data.integrator.transparent_shadows=1;data.integrator.transparent_max_bounce=4096;reason="packed state";}
    if(test==1){data.bvh.have_motion=1;reason="motion flag/table mismatch";}
    if(test==2){data.kernel_features|=KERNEL_FEATURE_VOLUME;reason="optional integration";}
    if(test==3){data.kernel_features|=KERNEL_FEATURE_SHADOW_LINKING;reason="optional integration";}
    if(test==4||test==10)states[0].shadow_path.flag|=PATH_RAY_SHADOW_FOR_AO;
    if(test==5){data.bvh.root=INT_MAX;}
    if(test==6||test==11)states[0].shadow_path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND;
    if(test==7||test==12)reason="Incomplete shadow";
    if(test==8||test==13)reason="pointer argument";
    if(test==9)states[0].shadow_path.packed_num_hits=1;
    if(test==14)states[0].shadow_path.render_pixel_index=1;
    if(test==15)states[0].shadow_path.lightgroup=255;
    if(test==16){data.film.pass_diffuse_direct=47;reason="film pass offset";}
    if(test==17){data.integrator.sample_clamp_direct=-1;reason="shadow clamp";}
    d.const_copy_to("data",&data,sizeof(data));const auto layout=fields(d);upload(d,layout,states);
    IntegratorQueueCounter counter{};counter.num_queued[kernel]=1;d.upload(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
    device_vector<float> film(&d,"rejected_shadow_film",MEM_READ_WRITE);film.alloc(48);std::fill_n(film.data(),48,.03125f);film.copy_to_device();
    device_ptr null=0;int n=(test==7||test==12)?2:1,wrong=0;DeviceKernelArguments args;
    if(test==8||test==13)args.add(&wrong);else args.add(&null);if(shading)args.add(&film.device_pointer);args.add(&n);
    auto queue=d.gpu_queue_create();bool accepted=queue->enqueue(kernel,1,args);if(accepted)accepted=queue->synchronize();
    require(!accepted&&d.error_message().find(reason)!=std::string::npos,"Shadow negative "+std::to_string(test)+": "+d.error_message());
    IntegratorQueueCounter after{};d.download(uintptr_t(d.state.queue_counter),&after,sizeof(after));require(std::memcmp(&counter,&after,sizeof(after))==0,"Rejected shadow changed counters");
    for(auto &f:layout){std::vector<uint8_t> bytes(f.stride);d.download(f.address,bytes.data(),bytes.size());require(std::memcmp(bytes.data(),reinterpret_cast<uint8_t*>(&states[0])+f.offset,f.stride)==0,"Rejected shadow changed state");}
    std::vector<float> output(48);d.download(film.device_pointer,output.data(),192);require(std::all_of(output.begin(),output.end(),[](float v){return v==.03125f;}),"Rejected shadow changed film");rejected++;
  }return rejected;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2||(argc==3&&(std::string(argv[2])=="--motion"||std::string(argv[2])=="--deform"||std::string(argv[2])=="--deform-motion")),"Usage: shadow_gate OUTPUT_PREFIX [--motion|--deform|--deform-motion]");const bool deform=argc==3&&std::string(argv[2])!="--motion";const bool motion=argc==3&&std::string(argv[2])!="--deform";std::string prefix=argv[1];auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");
  Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=require_webgpu_device(owner.get(),"shadow_gate device creation");Metrics m;
  {
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=false;params.use_texture_cache=false;
    Scene scene(params,&d);prepare(scene);if(motion){for(size_t i=0;i<scene.objects.size();i++)object_motion_fixture(scene,*scene.objects[i],unsigned(i));}if(deform)for(size_t i=0;i<scene.objects.size();i++)deforming_motion_fixture(scene,*scene.objects[i],unsigned(i));Progress progress;scene.device_update(&d,progress);healthy(d);configure(scene.dscene.data);
    for(size_t i=0;i<scene.dscene.bvh_leaf_nodes.size();i++)m.instances+=scene.dscene.bvh_leaf_nodes[i].x<0;
    require(m.instances>=3,"Shadow fixture has no native instances");
    d.const_copy_to("data",&scene.dscene.data,sizeof(KernelData));bool cancel=false;ShadowScheduler scheduler(&d,scene.film,&scene.dscene,&cancel);
    scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);const auto layout=fields(d);const auto original=scene.dscene.data;
    BufferParams bp;bp.width=193;bp.height=1;bp.pass_stride=48;auto buffers=scheduler.get_render_buffers();buffers->reset(bp);
    for(int variant=0;variant<12;variant++)for(int count:{1,63,64,65,193}){
      std::cerr<<"shadow variant="<<variant<<" count="<<count<<"\n";
      scene.dscene.data=original;auto &data=scene.dscene.data;
      if(variant>=6){data.integrator.sample_clamp_direct=100;data.integrator.sample_clamp_indirect=100;}
      if(variant==10){data.film.light_pass_flag=0;data.film.pass_adaptive_aux_buffer=PASS_UNUSED;}
      if(variant==11){data.film.pass_glossy_direct=PASS_UNUSED;data.film.pass_transmission_indirect=PASS_UNUSED;}
      d.const_copy_to("data",&data,sizeof(data));scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Shadow reset failed");
      auto expected=initial(193,variant%6,motion||deform);std::vector<int> indices;
      for(int i=0;i<count;i++)if(variant<6||i%3!=0){expected[i].shadow_path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW;indices.push_back(i);}
      upload(d,layout,expected);std::vector<float> film(buffers->buffer.size(),.03125f);std::copy(film.begin(),film.end(),buffers->buffer.data());buffers->buffer.copy_to_device();
      IntegratorQueueCounter counter{};counter.num_queued[DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW]=indices.size();d.upload(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
      scheduler.active(count);require(scheduler.update_queue_counter_and_cache(),"Shadow counters failed");
      if(variant%6==1||variant%6==2){auto unexcluded=initial(193,0,motion||deform);for(int i:indices)unexcluded[i].shadow_path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW;
        original_shadow(scene.dscene,DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,indices,unexcluded,film);
        auto probe=expected;original_shadow(scene.dscene,DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,indices,probe,film);
        for(int i:indices)m.excluded+=unexcluded[i].shadow_path.queued_kernel==0&&probe[i].shadow_path.queued_kernel!=0;
      }
      std::unique_ptr<RenderCapture> capture;std::string captured;
      if(count==193&&(variant==0||variant==6)){
        captured=prefix+(variant==0?".capture-direct/run":".capture-indexed/run");
        std::filesystem::create_directories(std::filesystem::path(captured).parent_path());
        capture=std::make_unique<RenderCapture>(d,captured);
      }
      original_shadow(scene.dscene,DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,indices,expected,film);
      if(!indices.empty()){scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW);require(scheduler.update_queue_counter_and_cache(),"Shadow intersect failed: "+d.error_message());}
      // The replay asserts native scheduling readbacks, while independent CPU
      // state/film comparisons remain separate numerical acceptance checks.
      if(capture)d.observer=nullptr;
      compare(d,layout,expected,m);film_compare(d,buffers->buffer,film,m);
      if(capture)d.observer=capture.get();
      std::vector<int> visible;for(int i:indices){if(expected[i].shadow_path.queued_kernel)visible.push_back(i);else m.blocked++;}
      m.clear+=visible.size();m.paths+=indices.size();m.cases++;m.direct+=indices.size()==size_t(count);
      original_shadow(scene.dscene,DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW,visible,expected,film);
      if(!visible.empty()){scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW);require(scheduler.update_queue_counter_and_cache(),"Shadow shade failed: "+d.error_message());}
      if(capture){
        capture->finish(captured,buffers->buffer.device_pointer,buffers->buffer.memory_size(),
                        uintptr_t(d.state.path.queued_kernel),scheduler.capacity(),193,1,1,48,"shadow-stages");
        RenderCapture::file(captured+".cpu.film.bin",film.data(),film.size()*sizeof(float));
      }
      compare(d,layout,expected,m);film_compare(d,buffers->buffer,film,m);
    }
    require(m.blocked>100&&m.clear>100&&m.excluded>50,"Insufficient shadow occlusion/self-light coverage");
  }
  require(stats.mem_used==0,"Shadow gate leaked Cycles allocations");auto rejected=rejections(infos[0]);
  const ShadowPrecision exact=motion||deform?ShadowPrecision{}:shadow_precision(infos[0]);
  std::ofstream(prefix+".library.txt")<<d.gpu.library_path;std::ofstream(prefix+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(prefix+".wgsl")<<shadow_source();
  std::cout<<std::setprecision(17)<<"{\"passed\":true,\"originalCpuShadowKernels\":true,\"originalGpuScheduler\":true,\"transparentShadowsProven\":false,\"directLightingProven\":false,\"cases\":"<<m.cases
    <<",\"paths\":"<<m.paths<<",\"blocked\":"<<m.blocked<<",\"unoccluded\":"<<m.clear<<",\"selfOrLightExclusions\":"<<m.excluded<<",\"nativeInstanceLeaves\":"<<m.instances
    <<",\"directDispatchCases\":"<<m.direct<<",\"exactStateChecks\":"<<m.exact<<",\"filmFloatChecks\":"<<m.floats<<",\"strictDifferences\":"<<m.strict<<",\"maximumAbsoluteError\":"<<m.maximum
    <<",\"recordedShadowCases\":2,\"invalidCasesRejected\":"<<rejected<<",\"precisionPaths\":"<<exact.paths<<",\"precisionBlocked\":"<<exact.blocked<<",\"precisionUnblocked\":"<<exact.clear<<",\"precisionEmitterPaths\":"<<exact.emitter_paths<<",\"precisionDecisionMismatches\":0,\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
