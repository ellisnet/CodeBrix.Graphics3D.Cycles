// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Curve state gate against the unmodified CPU kernels (curve_fixture.h): the closest traversal, the opaque shadow
// traversal and curve_shader_setup, with BVH2's unaligned nodes on and off, per curve shape and in a mixed scene.
// Hit decisions, primitive, object, packed type, queues and every state field are exact; t, u, v and the ShaderData
// floats must pass the practical criterion, the strict criterion and the bit count are reported. The refusals
// lane-owned combinations keep are checked by name.
#include "wavefront_test.h"
#include "closest_reference.h"
#include "shadow_reference.h"
#include "shader_data_reference.h"
#include "curve_fixture.h"
#include "wavefront.h"
#include "pipelines.h"
#include "scene/camera.h"
#include "scene/integrator.h"
#include "util/progress.h"
#include "util/task.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
using namespace ccl;
using namespace wavefront_test;
struct ShadowScheduler:Scheduler {
  using Scheduler::Scheduler;
  void active(int n){integrator_next_shadow_path_index_[0]=n;integrator_next_shadow_path_index_.copy_to_device();}
};
struct Row {std::string name;bool unaligned=false;unsigned unaligned_nodes=0,instances=0;uint64_t paths=0,hits=0,curve_hits=0,tri_hits=0,misses=0,decision=0,bits=0,strict=0,practical=0,
  shadow_paths=0,shadow_blocked=0,shadow_clear=0,shadow_decision=0,sd_paths=0,sd_float=0,sd_bits=0,sd_strict=0,sd_practical=0,sd_int=0;double maximum=0,sd_maximum=0;};
struct ShadowField {std::string name;uint32_t address;size_t stride,offset;};
static std::vector<ShadowField> shadow_fields(WebGPUDevice &d){std::vector<ShadowField> out;
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
static std::string probe_source(WebGPUDevice &d){
  std::ostringstream code;code<<d.module_wgsl(shader_data_source());
  code<<"@compute @workgroup_size(64) fn shader_data_probe(@builtin(global_invocation_id) id:vec3<u32>){\n"
        " if(id.x>=p(1u)){return;}var index=id.x;if(p(2u)!=0u){index=load_word(p(2u)+id.x*4u);}\n"
        " let sd=surface_from_ray(index);if(sd.error!=0u){return;}\n"
        " let base=p(4u)+id.x*"<<sizeof(ShaderDataRecord)<<"u;\n";
  auto field=[&](const char *name,size_t offset,size_t size,bool floating){
    const char *component[]={".x",".y",".z"};
    for(size_t i=0;i<size/4;i++)code<<" store_word(base+"<<offset+4*i<<"u,"<<(floating?"bitcast<u32>(":"")<<"sd."<<name<<(size>4?component[i]:"")<<(floating?")":"")<<");\n";
  };
#define FIELD(type,name,wgsl) field(#wgsl,offsetof(ShaderDataRecord,name),sizeof(type),true);
  CBC_SHADER_FLOAT_FIELDS(FIELD)
#undef FIELD
#define FIELD(type,name,wgsl) field(#wgsl,offsetof(ShaderDataRecord,name),sizeof(type),false);
  CBC_SHADER_INT_FIELDS(FIELD)
#undef FIELD
  code<<"}\n";return code.str();
}
static std::ofstream mismatch_log;
static void run_case(const DeviceInfo &info,const std::string &name,const std::vector<CurveShapeType> &shapes,bool unaligned,std::vector<Row> &rows){
  Row row;row.name=name;row.unaligned=unaligned;
  Stats stats;Profiler profiler;auto owner=Device::create(info,stats,profiler,true);auto *backend=dynamic_cast<WebGPUDevice*>(owner.get());require(backend,"Curve gate device creation failed");auto &d=*backend;
  {
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=unaligned;params.use_texture_cache=false;params.hair_subdivisions=name=="ribbon-subdivided"?1:3;
    Scene scene(params,&d);
    scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);
    scene.integrator->set_aa_samples(64);scene.integrator->set_seed(17);scene.film->set_use_sample_count(false);
    scene.camera->set_full_width(64);scene.camera->set_full_height(48);scene.camera->set_screen_size(64,48);scene.camera->compute_auto_viewplane();
    const auto fixture=curve_fixture(scene,scene.default_surface,shapes);
    Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Curve scene update failed");
    auto &data=scene.dscene.data;data.integrator.use_light_mis=0;data.integrator.use_caustics=0;data.integrator.transparent_shadows=0;d.const_copy_to("data",&data,sizeof(KernelData));
    require(data.bvh.have_curves,"Fixture has no curves");
    for(size_t i=0;i<scene.dscene.bvh_leaf_nodes.size();i++)row.instances+=scene.dscene.bvh_leaf_nodes[i].x<0;
    // Walk the node table's flags from the root for the unaligned count.
    {std::vector<int> stack;std::vector<int> pending{scene.dscene.data.bvh.root};for(size_t o=0;o<scene.dscene.object_node.size();o++)pending.push_back(scene.dscene.object_node[o]);
     std::map<int,bool> seen;while(!pending.empty()){int node=pending.back();pending.pop_back();if(node<0||seen[node])continue;seen[node]=true;
       const int4 w=scene.dscene.bvh_nodes[node];if(uint(w.x)&PATH_RAY_VISIBILITY_NODE_UNALIGNED)row.unaligned_nodes++;pending.push_back(w.z);pending.push_back(w.w);}}
    require(unaligned==(row.unaligned_nodes>0),"Unaligned node presence differs from the scene parameter");
    bool cancelled=false;ShadowScheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);
    const auto layout=fields(d);const auto shadow_layout=shadow_fields(d);BufferParams bp;bp.width=64;bp.height=48;bp.pass_stride=data.film.pass_stride;
    auto buffers=scheduler.get_render_buffers();buffers->reset(bp);buffers->zero();
    const unsigned n=std::min(scheduler.capacity(),4096);
    std::string source=probe_source(d);
    WGPUShaderSourceWGSL wgsl{};wgsl.chain.sType=WGPUSType_ShaderSourceWGSL;wgsl.code={source.data(),source.size()};
    WGPUShaderModuleDescriptor desc{};desc.nextInChain=&wgsl.chain;d.gpu.push_scope();
    cbc::Handle<WGPUShaderModule,wgpuShaderModuleRelease> module(wgpuDeviceCreateShaderModule(d.gpu.get(),&desc));d.gpu.pop_scope();
    auto probe=d.gpu.pipeline(module,d.pipeline_layout,"shader_data_probe");
    for(unsigned round=0;round<3;round++){
      const auto rays=curve_rays(fixture,n,20261106u+round*7919u+(unaligned?1u:0u));
      scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Curve reset failed");
      std::vector<IntegratorStateCPU> expected(scheduler.capacity());std::vector<int> indices;
      for(unsigned i=0;i<n;i++){auto &s=expected[i];s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST;s.path.visibility=PATH_RAY_VISIBILITY_CAMERA;
        s.path.throughput=one_float3();s.path.continuation_probability=1;s.path.sample=i%64;s.path.render_pixel_index=i%3072;
        s.ray.P=rays[i].P;s.ray.D=rays[i].D;s.ray.tmin=rays[i].tmin;s.ray.tmax=rays[i].tmax;s.ray.time=.5f;s.ray.dP=1e-4f;s.ray.dD=1e-3f;s.isect.prim=PRIM_NONE;s.isect.object=OBJECT_NONE;
        indices.push_back(int(i));}
      upload(d,layout,expected);IntegratorQueueCounter counters{};counters.num_queued[DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST]=n;d.upload(uintptr_t(d.state.queue_counter),&counters,sizeof(counters));
      scheduler.set_active(int(n));require(scheduler.update_queue_counter_and_cache(),"Curve counters failed");
      std::vector<float> film(buffers->buffer.size());const auto input=expected;original_closest(scene.dscene,indices,expected,film);
      scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST);require(scheduler.update_queue_counter_and_cache(),"Curve closest failed: "+d.error_message());
      auto actual=download(d,layout,n);
      for(unsigned i=0;i<n;i++){const auto &e=expected[i].isect;const auto &a=actual[i].isect;row.paths++;
        const bool same=e.prim==a.prim&&e.object==a.object&&e.type==a.type;const bool same_queue=expected[i].path.queued_kernel==actual[i].path.queued_kernel;
        if(e.prim!=PRIM_NONE){row.hits++;if(e.type&PRIMITIVE_CURVE)row.curve_hits++;else row.tri_hits++;}else row.misses++;
        bool bits=same&&std::memcmp(&e.t,&a.t,4)==0&&std::memcmp(&e.u,&a.u,4)==0&&std::memcmp(&e.v,&a.v,4)==0;
        bool practical=same&&same_queue;
        if(same)for(auto [ev,av]:{std::pair{e.t,a.t},std::pair{e.u,a.u},std::pair{e.v,a.v}}){const double error=std::abs(double(av)-ev);row.maximum=std::max(row.maximum,error);
          row.strict+=error>1e-7+1e-7*std::abs(double(ev));if(!(std::isfinite(av)&&error<=5e-5+2e-4*std::abs(double(ev))))practical=false;}
        row.decision+=!(same&&same_queue);row.bits+=!bits;row.practical+=!practical;
        if((!bits)&&mismatch_log){mismatch_log<<std::setprecision(9)<<name<<" unaligned="<<unaligned<<" round="<<round<<" i="<<i<<" kind="<<rays[i].P.x*0+(i%8)<<" P=("<<input[i].ray.P.x<<","<<input[i].ray.P.y<<","<<input[i].ray.P.z<<") D=("<<input[i].ray.D.x<<","<<input[i].ray.D.y<<","<<input[i].ray.D.z<<") tmin="<<input[i].ray.tmin<<" tmax="<<input[i].ray.tmax
          <<" cpu[t="<<e.t<<" u="<<e.u<<" v="<<e.v<<" prim="<<e.prim<<" obj="<<e.object<<" type="<<e.type<<"] gpu[t="<<a.t<<" u="<<a.u<<" v="<<a.v<<" prim="<<a.prim<<" obj="<<a.object<<" type="<<a.type<<"]\n";}
      }
      // ShaderData at the CPU's hits, from the same state.
      {
        std::vector<int> selected;for(unsigned i=0;i<n;i++)if(expected[i].isect.prim!=PRIM_NONE)selected.push_back(int(i));
        auto wanted=original_shader_data(scene.dscene,selected,expected);upload(d,layout,expected);
        device_vector<int> chosen(&d,"curve_indices",MEM_READ_WRITE);chosen.alloc(selected.size());std::copy(selected.begin(),selected.end(),chosen.data());chosen.copy_to_device();
        device_vector<uint> output(&d,"curve_output",MEM_READ_WRITE);output.alloc(16+(selected.size()+3)*sizeof(ShaderDataRecord)/4);std::fill_n(output.data(),output.size(),0xa5a5a5a5u);output.copy_to_device();
        std::array<uint32_t,32> p{};p[1]=selected.size();p[2]=chosen.device_pointer;p[4]=output.device_pointer+32;
        p[6]=d.constants.at("data");p[7]=d.constants.at("integrator_state");p[10]=uint32_t(expected.size());p[14]=d.scene_tables();p[17]=d.error_word;
        d.dispatch(p,selected.size()+63,probe);d.sync();healthy(d);output.copy_from_device();healthy(d);
        const auto bytes=reinterpret_cast<const uint8_t*>(output.data());
        for(size_t i=0;i<selected.size();i++){
          ShaderDataRecord got{};std::memcpy(&got,bytes+32+i*sizeof(got),sizeof(got));auto &want=wanted[i];row.sd_paths++;
          auto check=[&](const char *field,size_t offset,size_t size,bool floating){
            for(size_t k=0;k<size;k+=4){uint a,e;std::memcpy(&a,reinterpret_cast<const char*>(&got)+offset+k,4);std::memcpy(&e,reinterpret_cast<const char*>(&want)+offset+k,4);
              if(!floating){if(a!=e){row.sd_int++;if(mismatch_log)mismatch_log<<"sd-int "<<name<<" "<<field<<" i="<<selected[i]<<" gpu="<<a<<" cpu="<<e<<" type="<<want.type<<"\n";}continue;}
              const float av=std::bit_cast<float>(a),ev=std::bit_cast<float>(e);const double error=std::abs(double(av)-ev);row.sd_float++;row.sd_bits+=a!=e;
              if(std::isfinite(ev)&&std::isfinite(av))row.sd_maximum=std::max(row.sd_maximum,error);row.sd_strict+=error>1e-7+1e-7*std::abs(double(ev));
              const bool ok=a==e||(std::isfinite(av)&&std::isfinite(ev)&&error<=5e-5+2e-4*std::abs(double(ev)));row.sd_practical+=!ok;
              if((!ok||a!=e)&&mismatch_log)mismatch_log<<(ok?"sd-bits ":"sd-float ")<<name<<" "<<field<<"["<<k/4<<"] i="<<selected[i]<<" type="<<want.type<<" gpu="<<std::setprecision(9)<<av<<" cpu="<<ev<<"\n";}
          };
#define FIELD(type,name,wgsl) check(#name,offsetof(ShaderDataRecord,name),sizeof(type),true);
          CBC_SHADER_FLOAT_FIELDS(FIELD)
#undef FIELD
#define FIELD(type,name,wgsl) check(#name,offsetof(ShaderDataRecord,name),sizeof(type),false);
          CBC_SHADER_INT_FIELDS(FIELD)
#undef FIELD
        }
      }
      // Opaque shadow rays over the same rays, with self and light exclusion on the CPU's hits.
      {
        scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Curve shadow reset failed");
        std::vector<IntegratorShadowStateCPU> shadow(n);std::vector<int> selected;
        for(unsigned i=0;i<n;i++){auto &s=shadow[i];s.shadow_path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW;s.shadow_path.flag=0;s.shadow_path.render_pixel_index=i;s.shadow_path.throughput=one_float3();
          s.shadow_ray.P=rays[i].P;s.shadow_ray.D=rays[i].D;s.shadow_ray.tmin=rays[i].tmin;s.shadow_ray.tmax=rays[i].tmax;s.shadow_ray.time=.5f;
          s.shadow_ray.self_light_object=OBJECT_NONE;s.shadow_ray.self_light_prim=PRIM_NONE;
          auto &self=s.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1];self.object=OBJECT_NONE;self.prim=PRIM_NONE;
          if(i%3==1&&expected[i].isect.prim!=PRIM_NONE){self.object=expected[i].isect.object;self.prim=expected[i].isect.prim;}
          if(i%3==2&&expected[i].isect.prim!=PRIM_NONE){s.shadow_ray.self_light_object=expected[i].isect.object;s.shadow_ray.self_light_prim=expected[i].isect.prim;}
          s.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_CPU-1]=self;selected.push_back(int(i));}
        for(auto &f:shadow_layout){std::vector<uint8_t> bytes(size_t(n)*f.stride);for(size_t i=0;i<n;i++)std::memcpy(bytes.data()+i*f.stride,reinterpret_cast<const uint8_t*>(&shadow[i])+f.offset,f.stride);d.upload(f.address,bytes.data(),bytes.size());}
        IntegratorQueueCounter counter{};counter.num_queued[DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW]=n;d.upload(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
        scheduler.active(int(n));require(scheduler.update_queue_counter_and_cache(),"Curve shadow counters failed");
        std::vector<float> shadow_film(48);original_shadow(scene.dscene,DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,selected,shadow,shadow_film);
        scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW);require(scheduler.update_queue_counter_and_cache(),"Curve shadow failed: "+d.error_message());
        std::vector<uint16_t> queued(n);d.download(uintptr_t(d.state.shadow_path.queued_kernel),queued.data(),queued.size()*2);
        for(unsigned i=0;i<n;i++){row.shadow_paths++;const bool clear=shadow[i].shadow_path.queued_kernel!=0;row.shadow_clear+=clear;row.shadow_blocked+=!clear;
          if(queued[i]!=shadow[i].shadow_path.queued_kernel){row.shadow_decision++;if(mismatch_log)mismatch_log<<"shadow "<<name<<" unaligned="<<unaligned<<" i="<<i<<" kind="<<(i%8)<<" gpu="<<queued[i]<<" cpu="<<shadow[i].shadow_path.queued_kernel<<"\n";}}
      }
    }
  }
  require(stats.mem_used==0,"Curve gate leaked Cycles allocations");
  rows.push_back(row);
}
// Refusals by name: what lane-owned combinations still need, on a scene that otherwise renders.
static unsigned refusals(const DeviceInfo &info){
  unsigned refused=0;
  for(int test=0;test<5;test++){
    Stats stats;Profiler profiler;auto owner=Device::create(info,stats,profiler,true);auto *backend=dynamic_cast<WebGPUDevice*>(owner.get());require(backend,"Curve refusal device failed");auto &d=*backend;
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=true;params.use_texture_cache=false;Scene scene(params,&d);
    scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);
    scene.camera->set_full_width(64);scene.camera->set_full_height(48);scene.camera->set_screen_size(64,48);scene.camera->compute_auto_viewplane();
    curve_fixture(scene,scene.default_surface,{CURVE_THICK});Progress progress;scene.device_update(&d,progress);healthy(d);
    auto &data=scene.dscene.data;data.integrator.use_light_mis=0;data.integrator.use_caustics=0;data.integrator.transparent_shadows=0;
    std::string reason;
    if(test==0){data.bvh.have_motion=1;reason="curves with motion blur";}
    if(test==1){data.integrator.transparent_shadows=1;reason="curves with transparent shadows";}
    if(test==2){data.integrator.use_caustics=1;reason="curves with caustics";}
    if(test==3){data.bvh.have_points=1;reason="triangle or static curve primitives";}
    if(test==4){data.bvh.curve_subdivisions=0;reason="curve subdivisions";}
    d.const_copy_to("data",&data,sizeof(data));healthy(d);
    bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Curve refusal setup failed");
    device_vector<float> film(&d,"refused_film",MEM_READ_WRITE);film.alloc(16);film.zero_to_device();
    device_vector<int> indices(&d,"refused_indices",MEM_READ_WRITE);indices.alloc(1);indices[0]=0;indices.copy_to_device();int count=1;
    DeviceKernelArguments args;args.add(&indices.device_pointer);args.add(&film.device_pointer);args.add(&count);
    auto queue=d.gpu_queue_create();bool accepted=queue->enqueue(DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST,1,args);if(accepted)accepted=queue->synchronize();
    require(!accepted&&d.error_message().find(reason)!=std::string::npos,"Curve refusal "+std::to_string(test)+": "+d.error_message());refused++;
  }
  return refused;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc>=2,"Usage: curve_gate OUTPUT_PREFIX [case ...]");const std::string output=argv[1];std::vector<std::string> only(argv+2,argv+argc);
  mismatch_log.open(output+".mismatches.txt");
  auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(!infos.empty(),"No WebGPU device");
  std::vector<Row> rows;
  const std::vector<std::pair<std::string,std::vector<CurveShapeType>>> cases={{"ribbon",{CURVE_RIBBON}},{"ribbon-subdivided",{CURVE_RIBBON}},{"thick",{CURVE_THICK}},{"linear",{CURVE_THICK_LINEAR}},{"mixed",{CURVE_RIBBON,CURVE_THICK,CURVE_THICK_LINEAR}}};
  for(const auto &[name,shapes]:cases){if(!only.empty()&&std::find(only.begin(),only.end(),name)==only.end())continue;
    for(bool unaligned:{true,false}){std::cerr<<"[curve] "<<name<<" unaligned="<<unaligned<<"\n";run_case(infos[0],name,shapes,unaligned,rows);
      const auto &r=rows.back();std::cerr<<"  paths="<<r.paths<<" curveHits="<<r.curve_hits<<" triHits="<<r.tri_hits<<" misses="<<r.misses<<" decision="<<r.decision<<" bits="<<r.bits<<" strict="<<r.strict<<" practical="<<r.practical<<" max="<<r.maximum
        <<" shadow="<<r.shadow_paths<<" blocked="<<r.shadow_blocked<<" decision="<<r.shadow_decision<<" sdFloats="<<r.sd_float<<" sdBits="<<r.sd_bits<<" sdStrict="<<r.sd_strict<<" sdPractical="<<r.sd_practical<<" sdInt="<<r.sd_int<<" sdMax="<<r.sd_maximum<<" unalignedNodes="<<r.unaligned_nodes<<" instances="<<r.instances<<"\n";}}
  const unsigned refused=only.empty()?refusals(infos[0]):0;
  bool passed=true;for(const auto &r:rows)passed=passed&&r.decision==0&&r.practical==0&&r.shadow_decision==0&&r.sd_practical==0&&r.sd_int==0&&r.curve_hits>0;
  std::cout<<"{\"passed\":"<<(passed?"true":"false")<<",\"arithmetic\":\""<<wgsl_arithmetic()<<"\",\"refused\":"<<refused<<",\"rows\":[";bool first=true;
  for(const auto &r:rows){if(!first)std::cout<<",";first=false;
    std::cout<<"{\"case\":\""<<r.name<<"\",\"unaligned\":"<<r.unaligned<<",\"unalignedNodes\":"<<r.unaligned_nodes<<",\"instances\":"<<r.instances<<",\"paths\":"<<r.paths<<",\"curveHits\":"<<r.curve_hits<<",\"triangleHits\":"<<r.tri_hits<<",\"misses\":"<<r.misses<<",\"decisionMismatches\":"<<r.decision<<",\"bitMismatches\":"<<r.bits<<",\"strict\":"<<r.strict<<",\"practicalFailures\":"<<r.practical<<",\"maximum\":"<<r.maximum
      <<",\"shadowPaths\":"<<r.shadow_paths<<",\"shadowBlocked\":"<<r.shadow_blocked<<",\"shadowClear\":"<<r.shadow_clear<<",\"shadowDecisionMismatches\":"<<r.shadow_decision<<",\"shaderDataPaths\":"<<r.sd_paths<<",\"shaderDataFloats\":"<<r.sd_float<<",\"shaderDataBitMismatches\":"<<r.sd_bits<<",\"shaderDataStrict\":"<<r.sd_strict<<",\"shaderDataPracticalFailures\":"<<r.sd_practical<<",\"shaderDataIntegerMismatches\":"<<r.sd_int<<",\"shaderDataMaximum\":"<<r.sd_maximum<<"}";}
  std::cout<<"]}\n";
  status=passed?0:1;
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
