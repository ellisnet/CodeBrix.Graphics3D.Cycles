// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "wavefront_test.h"
#include "shadow_reference.h"
#include "wavefront.h"
#include "render_capture.h"
#include "transparent_shadow_fixture.h"
#include "scene/scene.h"
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
struct Metrics {uint64_t exact=0,floats=0,strict=0,paths=0,blocked=0,clear=0,excluded=0,attenuated=0,resumed=0,max_hits=0,four_hit_stages=0,overflow_stages=0;unsigned cases=0,direct=0,instances=0;double maximum=0;};
static void compare(WebGPUDevice &d,const std::vector<Field> &layout,const States &expected,Metrics &m){
  for(auto &f:layout){std::vector<uint8_t> bytes(expected.size()*f.stride);d.download(f.address,bytes.data(),bytes.size());
    const bool numeric=f.name=="shadow_path.throughput"||f.name=="shadow_ray.tmin"||(f.name.starts_with("shadow_isect.t")&&!f.name.starts_with("shadow_isect.type"))||f.name.starts_with("shadow_isect.u")||f.name.starts_with("shadow_isect.v");
    for(size_t i=0;i<expected.size();i++){
      const auto ref=reinterpret_cast<const uint8_t*>(&expected[i])+f.offset;
      if(numeric){for(size_t k=0;k<f.stride;k+=4){float a,b;std::memcpy(&a,bytes.data()+i*f.stride+k,4);std::memcpy(&b,ref+k,4);double e=std::abs(double(a)-b);m.floats++;m.maximum=std::max(m.maximum,e);m.strict+=e>1e-7+1e-7*std::abs(double(b));
        require(std::isfinite(a)&&e<=5e-5+2e-4*std::abs(double(b)),"Transparent state mismatch "+f.name+" slot="+std::to_string(i)+" actual="+std::to_string(a)+" expected="+std::to_string(b));}}
      else{m.exact++;require(std::memcmp(bytes.data()+i*f.stride,ref,f.stride)==0,"Transparent exact mismatch "+f.name+" slot "+std::to_string(i));}}}

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
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);scene.integrator->set_transparent_max_bounce(16);
  std::array<Mesh*,5> meshes{};
  for(unsigned mode=0;mode<5;mode++){
    auto mesh=scene.create_node<Mesh>();meshes[mode]=mesh;array<Node*> shaders;shaders.push_back_slow(mode==4?scene.default_surface:transparent_material(scene,mode));mesh->set_used_shaders(shaders);mesh->resize_mesh(4,2);
    const float positions[][3]={{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};for(int i=0;i<4;i++)mesh->get_position_for_write()[i]=make_float3(positions[i][0],positions[i][1],positions[i][2]);
    const int triangles[]={0,1,2,0,2,3};std::copy_n(triangles,6,mesh->get_triangles().data());std::ranges::fill(mesh->get_smooth(),false);std::ranges::fill(mesh->get_shader(),0);mesh->tag_triangles_modified();
  }
  auto hidden=scene.create_node<Object>();hidden->set_geometry(meshes[1]);hidden->set_tfm(transform_translate(make_float3(-2.7f,0,.2f)));hidden->set_visibility(PATH_RAY_VISIBILITY_CAMERA);
  // Columns: clear, one colored pane, transparent+opaque, seven transparent panes.
  for(int column=1;column<4;column++)for(int layer=0;layer<(column==1?1:column==2?2:7);layer++){
    auto object=scene.create_node<Object>();object->set_geometry(meshes[column==2&&layer==1?4:column==1?1:layer%4]);
    object->set_tfm(transform_translate(make_float3((column-1)*2.7f,0,layer*.37f))*transform_scale(make_float3(column==2?-1.2f:1.f,.8f+.2f*column,1.5f)));
  }
}
static void configure(KernelData &data){
  data.integrator.transparent_shadows=1;data.integrator.transparent_max_bounce=16;data.kernel_features|=KERNEL_FEATURE_LIGHT_PASSES;
  data.film.pass_stride=48;data.film.light_pass_flag=PASSMASK(COMBINED);data.film.pass_combined=0;data.film.pass_adaptive_aux_buffer=4;
  data.film.pass_diffuse_direct=8;data.film.pass_glossy_direct=11;data.film.pass_transmission_direct=14;
  data.film.pass_diffuse_indirect=17;data.film.pass_glossy_indirect=20;data.film.pass_transmission_indirect=23;data.film.pass_lightgroup=26;
  data.integrator.sample_clamp_direct=.7f;data.integrator.sample_clamp_indirect=.23f;
}
static States initial(int count,int variant){
  States states(count);
  for(int i=0;i<count;i++){auto &s=states[i];auto &p=s.shadow_path;
    p.render_pixel_index=i;p.sample=i%16;p.bounce=i%3;p.visibility=PATH_RAY_VISIBILITY_DIFFUSE;p.flag=i%7?PATH_RAY_SURFACE_PASS:0;
    p.throughput=make_float3((i%11)*.13f,-.18f,.39f);p.pass_diffuse_weight=make_float3(.1f,.2f,.3f);p.pass_glossy_weight=make_float3(.4f,.3f,.2f);

    p.lightgroup=i%3;p.packed_num_hits=333;p.rng_offset=17;p.rng_pixel=123456+i;p.transparent_bounce=i%4;
    s.shadow_ray.P=make_float3((i%4-1)*2.7f+.31f,-.23f,-3);s.shadow_ray.D=make_float3(0,0,1);
    s.shadow_ray.tmin=variant==3?4.f:0.f;s.shadow_ray.tmax=variant==4?2.f:100.f;s.shadow_ray.time=.5f;
    s.shadow_ray.self_light_object=OBJECT_NONE;s.shadow_ray.self_light_prim=PRIM_NONE;
    for(auto &hit:s.shadow_isect){hit.object=OBJECT_NONE;hit.prim=PRIM_NONE;}
    if(variant==5){
      if(i%3==0)s.shadow_ray.D=zero_float3();
      if(i%3==1)s.shadow_ray.P.x=std::numeric_limits<float>::infinity();
      if(i%3==2)s.shadow_ray.tmin=std::numeric_limits<float>::max();
    }
  }
  return states;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2,"Usage: shadow_gate OUTPUT_PREFIX");std::string prefix=argv[1];auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");
  Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=*dynamic_cast<WebGPUDevice*>(owner.get());Metrics m;
  {
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=false;params.use_texture_cache=false;
    Scene scene(params,&d);prepare(scene);Progress progress;scene.device_update(&d,progress);healthy(d);configure(scene.dscene.data);
    for(size_t i=0;i<scene.dscene.bvh_leaf_nodes.size();i++)m.instances+=scene.dscene.bvh_leaf_nodes[i].x<0;
    require(m.instances>=3,"Shadow fixture has no native instances");
    d.const_copy_to("data",&scene.dscene.data,sizeof(KernelData));bool cancel=false;ShadowScheduler scheduler(&d,scene.film,&scene.dscene,&cancel);
    scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);const auto layout=fields(d);const auto original=scene.dscene.data;
    BufferParams bp;bp.width=193;bp.height=1;bp.pass_stride=48;auto buffers=scheduler.get_render_buffers();buffers->reset(bp);
    for(int variant=0;variant<12;variant++)for(int count:{1,63,64,65,193}){
      std::cerr<<"shadow variant="<<variant<<" count="<<count<<"\n";
      scene.dscene.data=original;auto &data=scene.dscene.data;
      if(variant>=6){data.integrator.transparent_max_bounce=4;data.integrator.sample_clamp_direct=100;data.integrator.sample_clamp_indirect=100;}
      if(variant==10){data.film.light_pass_flag=0;data.film.pass_adaptive_aux_buffer=PASS_UNUSED;}
      if(variant==11){data.film.pass_glossy_direct=PASS_UNUSED;data.film.pass_transmission_indirect=PASS_UNUSED;}
      d.const_copy_to("data",&data,sizeof(data));scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Shadow reset failed");
      auto expected=initial(193,variant%6);std::vector<int> indices;
      for(int i=0;i<count;i++)if(variant<6||i%3!=0){expected[i].shadow_path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW;indices.push_back(i);}
      if(variant%6==1||variant%6==2){
        auto before=expected;std::vector<float> unused(buffers->buffer.size());original_shadow(scene.dscene,DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,indices,before,unused);
        for(int i:indices)if(before[i].shadow_path.packed_num_hits&&before[i].shadow_path.queued_kernel){const auto &hit=before[i].shadow_isect[0];
          if(variant%6==1){expected[i].shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].object=hit.object;expected[i].shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].prim=hit.prim;}
          else{expected[i].shadow_ray.self_light_object=hit.object;expected[i].shadow_ray.self_light_prim=hit.prim;}}
        auto after=expected;original_shadow(scene.dscene,DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,indices,after,unused);
        for(int i:indices)m.excluded+=before[i].shadow_path.packed_num_hits>after[i].shadow_path.packed_num_hits;
      }
      upload(d,layout,expected);std::vector<float> film(buffers->buffer.size(),.03125f);std::copy(film.begin(),film.end(),buffers->buffer.data());buffers->buffer.copy_to_device();
      IntegratorQueueCounter counter{};counter.num_queued[DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW]=indices.size();d.upload(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
      scheduler.active(count);require(scheduler.update_queue_counter_and_cache(),"Shadow counters failed");
      std::unique_ptr<RenderCapture> capture;std::string captured;
      if(count==193&&(variant==0||variant==6)){
        captured=prefix+(variant==0?".capture-direct/run":".capture-indexed/run");
        std::filesystem::create_directories(std::filesystem::path(captured).parent_path());
        capture=std::make_unique<RenderCapture>(d,captured);
      }
      m.paths+=indices.size();m.cases++;m.direct+=indices.size()==size_t(count);
      for(unsigned round=0;round<10;round++){
        std::vector<int> pending;for(int i:indices)if(expected[i].shadow_path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW)pending.push_back(i);
        if(pending.empty())break;require(round<9,"Transparent shadow loop did not terminate");
        original_shadow(scene.dscene,DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,pending,expected,film);
        scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW);require(scheduler.update_queue_counter_and_cache(),"Transparent intersect failed: "+d.error_message());
        if(capture)d.observer=nullptr;compare(d,layout,expected,m);film_compare(d,buffers->buffer,film,m);if(capture)d.observer=capture.get();
        std::vector<int> visible;for(int i:pending){if(expected[i].shadow_path.queued_kernel)visible.push_back(i);else m.blocked++;}
        m.clear+=visible.size();for(int i:visible){const auto hits=expected[i].shadow_path.packed_num_hits;m.max_hits=std::max(m.max_hits,uint64_t(hits));m.four_hit_stages+=hits==4;m.overflow_stages+=hits>4;}
        const auto before=expected;original_shadow(scene.dscene,DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW,visible,expected,film);
        for(int i:visible){m.attenuated+=expected[i].shadow_path.transparent_bounce>before[i].shadow_path.transparent_bounce;m.resumed+=expected[i].shadow_path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW;}
        if(!visible.empty()){scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW);require(scheduler.update_queue_counter_and_cache(),"Transparent shade failed: "+d.error_message());}
        if(capture)d.observer=nullptr;compare(d,layout,expected,m);film_compare(d,buffers->buffer,film,m);if(capture)d.observer=capture.get();
      }
      if(capture){
        capture->finish(captured,buffers->buffer.device_pointer,buffers->buffer.memory_size(),
                        uintptr_t(d.state.path.queued_kernel),scheduler.capacity(),193,1,1,48,"shadow-stages");
        RenderCapture::file(captured+".cpu.film.bin",film.data(),film.size()*sizeof(float));
      }
      compare(d,layout,expected,m);film_compare(d,buffers->buffer,film,m);
    }
    require(m.blocked>100&&m.clear>100&&m.attenuated>100&&m.resumed>50&&m.max_hits>=7&&m.four_hit_stages>50&&m.overflow_stages>50&&m.excluded>50,"Insufficient transparent shadow coverage");
  }
  require(stats.mem_used==0,"Shadow gate leaked Cycles allocations");unsigned rejected=0;
  std::ofstream(prefix+".library.txt")<<d.gpu.library_path;std::ofstream(prefix+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(prefix+".wgsl")<<shadow_source();
  std::cout<<std::setprecision(17)<<"{\"passed\":true,\"originalCpuShadowKernels\":true,\"originalAlgorithmsWithGpuHitCapacity\":4,\"originalGpuScheduler\":true,\"transparentTriangleShadowSliceProven\":true,\"directLightingProven\":false,\"cases\":"<<m.cases
    <<",\"fourHitStages\":"<<m.four_hit_stages<<",\"overflowStages\":"<<m.overflow_stages<<",\"attenuatedStages\":"<<m.attenuated<<",\"resumedStages\":"<<m.resumed<<",\"maximumHitCount\":"<<m.max_hits<<",\"paths\":"<<m.paths<<",\"blocked\":"<<m.blocked<<",\"unoccluded\":"<<m.clear<<",\"selfOrLightExclusions\":"<<m.excluded<<",\"nativeInstanceLeaves\":"<<m.instances
    <<",\"directDispatchCases\":"<<m.direct<<",\"exactStateChecks\":"<<m.exact<<",\"stateAndFilmFloatChecks\":"<<m.floats<<",\"strictDifferences\":"<<m.strict<<",\"maximumAbsoluteError\":"<<m.maximum
    <<",\"recordedShadowCases\":2,\"invalidCasesRejected\":"<<rejected<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
