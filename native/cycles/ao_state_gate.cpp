// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Ambient occlusion state gate: the surface stage's AO shadow ray
// (integrate_surface_ao: direction, origin, interval, self primitives, flags,
// throughputs, queue) and the shadow stage's AO film writes (the AO pass and the
// additive combined term), each against the unmodified CPU kernels from the same
// input states. Fast GI's AO bounce termination and per-object AO distance are
// the closest gate's cases (closest_gate.cpp).
#include "wavefront_test.h"
#include "wavefront.h"
#include "surface_reference.h"
#include "shadow_reference.h"
#include "ao_reference.h"
#include "scene/scene.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/camera.h"
#include "scene/integrator.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/progress.h"
#include "util/task.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
using namespace ccl;
using namespace wavefront_test;
#include "surface_fixture.h"
struct Metrics {uint64_t node_paths=0,node_sensitive=0,cases=0,paths=0,ao_paths=0,ao_clear=0,ao_blocked=0,integers=0,floats=0,strict=0;double maximum=0;};
struct AoScheduler:Scheduler {using Scheduler::Scheduler;void next_shadow(int n){integrator_next_shadow_path_index_[0]=n;integrator_next_shadow_path_index_.copy_to_device();}};
static void number(float a,float e,const std::string &where,Metrics &m){const double error=std::abs(double(a)-e);m.maximum=std::max(m.maximum,error);m.floats++;
  m.strict+=error>1e-7+1e-7*std::abs(double(e));require(std::isfinite(a)&&std::isfinite(e)&&error<=5e-5+2e-4*std::abs(double(e)),where+" actual="+std::to_string(a)+" expected="+std::to_string(e));}
// GPU shadow-state fields that integrate_surface_ao writes, with their CPU offsets.
struct ShadowField {std::string name;uint32_t address;size_t stride,offset;bool floating;};
static std::vector<ShadowField> ao_fields(WebGPUDevice &d){
  std::vector<ShadowField> out;
#define SF(parent,member,flt) out.push_back({#parent "." #member,uint32_t(uintptr_t(d.state.parent.member)),sizeof(IntegratorShadowStateCPU{}.parent.member),offsetof(IntegratorShadowStateCPU,parent.member),flt})
  SF(shadow_path,queued_kernel,false);SF(shadow_path,render_pixel_index,false);SF(shadow_path,rng_offset,false);SF(shadow_path,rng_pixel,false);SF(shadow_path,sample,false);
  SF(shadow_path,visibility,false);SF(shadow_path,flag,false);SF(shadow_path,bounce,false);SF(shadow_path,transparent_bounce,false);SF(shadow_path,volume_bounds_bounce,false);
  SF(shadow_path,throughput,true);
  if(d.data.kernel_features&KERNEL_FEATURE_AO_ADDITIVE)SF(shadow_path,unshadowed_throughput,true);
  SF(shadow_ray,P,true);SF(shadow_ray,D,true);SF(shadow_ray,tmin,true);SF(shadow_ray,tmax,true);SF(shadow_ray,time,true);SF(shadow_ray,dP,true);SF(shadow_ray,dD,true);
  SF(shadow_ray,self_light_object,false);SF(shadow_ray,self_light_prim,false);
#undef SF
  // integrator_state_write_shadow_ray_self: the last shadow_isect slot (GPU and CPU array sizes differ).
  out.push_back({"self.object",uint32_t(uintptr_t(d.state.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].object)),4,offsetof(IntegratorShadowStateCPU,shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_CPU-1].object),false});
  out.push_back({"self.prim",uint32_t(uintptr_t(d.state.shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_GPU-1].prim)),4,offsetof(IntegratorShadowStateCPU,shadow_isect[INTEGRATOR_SHADOW_ISECT_SIZE_CPU-1].prim),false});
  return out;
}
static std::vector<uint8_t> fetch(WebGPUDevice &d,uint32_t address,size_t bytes){std::vector<uint8_t> out(bytes);d.download(address,out.data(),bytes);healthy(d);return out;}
static void run_case(WebGPUDevice &d,AoScheduler &scheduler,Scene &scene,const std::vector<Field> &layout,std::vector<IntegratorStateCPU> input,const std::vector<int> &ids,
                     const std::string &label,Metrics &m,bool dense){
  auto &ds=scene.dscene;const size_t stride=ds.data.film.pass_stride,count=ids.size()*stride;
  // Unique pixels map each GPU shadow slot to its CPU path.
  for(size_t j=0;j<ids.size();j++)input[ids[j]].path.render_pixel_index=uint32_t(j);
  std::vector<float> wanted(count,.125f);auto expected=input;original_surface(ds,ids,expected,wanted);
  scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),label+": reset");scheduler.next_shadow(0);
  const uint32_t shadow_capacity=uint32_t(d.remaining(uintptr_t(d.state.shadow_path.queued_kernel))/2);d.zero(uintptr_t(d.state.shadow_path.queued_kernel),size_t(shadow_capacity)*2);
  upload(d,layout,input);
  device_vector<float> film(&d,"ao_state_film",MEM_READ_WRITE);film.alloc(count);std::fill_n(film.data(),count,.125f);film.copy_to_device();
  device_vector<int> selected(&d,"ao_state_indices",MEM_READ_WRITE);selected.alloc(std::max<size_t>(1,ids.size()));std::copy(ids.begin(),ids.end(),selected.data());selected.copy_to_device();
  IntegratorQueueCounter counter{};counter.num_queued[DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE]=ids.size();d.upload(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
  auto queue=d.gpu_queue_create();int n=int(ids.size());device_ptr selection=dense?0:selected.device_pointer;
  {DeviceKernelArguments args;args.add(&selection);args.add(&film.device_pointer);args.add(&n);
   require(queue->enqueue(DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,n,args)&&queue->synchronize(),label+": "+d.error_message());}
  // Main path state and film after the surface stage.
  const auto actual=download(d,layout,input.size());
  for(auto &f:layout)for(int i:ids){auto a=reinterpret_cast<const char*>(&actual[i])+f.offset;auto e=reinterpret_cast<const char*>(&expected[i])+f.offset;
    if(f.floating){for(size_t k=0;k<f.stride;k+=4){float av,ev;std::memcpy(&av,a+k,4);std::memcpy(&ev,e+k,4);number(av,ev,label+" "+f.name+" path="+std::to_string(i),m);}}
    else{require(!std::memcmp(a,e,f.stride),label+" integer "+f.name+" path="+std::to_string(i));m.integers++;}}
  film.copy_from_device();healthy(d);for(size_t i=0;i<count;i++)number(film[i],wanted[i],label+" surface film="+std::to_string(i),m);
  // AO shadow paths: every allocated slot against the CPU path's ao state.
  const auto fields=ao_fields(d);std::map<std::string,std::vector<uint8_t>> gpu;for(auto &f:fields)gpu[f.name]=fetch(d,f.address,size_t(shadow_capacity)*f.stride);
  std::vector<int> slots,owners;
  for(uint32_t slot=0;slot<shadow_capacity;slot++){uint16_t queued;std::memcpy(&queued,gpu["shadow_path.queued_kernel"].data()+slot*2,2);if(!queued)continue;
    uint32_t pixel;std::memcpy(&pixel,gpu["shadow_path.render_pixel_index"].data()+slot*4,4);require(pixel<ids.size(),label+": AO shadow slot pixel");
    slots.push_back(int(slot));owners.push_back(ids[pixel]);}
  size_t cpu_ao=0;for(int i:ids)cpu_ao+=expected[i].ao.shadow_path.queued_kernel!=0;
  require(slots.size()==cpu_ao,label+": AO shadow path count GPU="+std::to_string(slots.size())+" CPU="+std::to_string(cpu_ao));
  for(size_t k=0;k<slots.size();k++){const auto &e=expected[owners[k]].ao;require(e.shadow_path.queued_kernel!=0,label+": GPU AO path without a CPU AO path");
    for(auto &f:fields){const uint8_t *a=gpu[f.name].data()+size_t(slots[k])*f.stride;const uint8_t *b=reinterpret_cast<const uint8_t*>(&e)+f.offset;
      if(f.floating){for(size_t c=0;c<f.stride;c+=4){float av,ev;std::memcpy(&av,a+c,4);std::memcpy(&ev,b+c,4);number(av,ev,label+" AO "+f.name+" slot="+std::to_string(slots[k]),m);}}
      else{require(!std::memcmp(a,b,f.stride),label+" AO integer "+f.name+" slot="+std::to_string(slots[k]));m.integers++;}}}
  IntegratorQueueCounter observed{};d.download(uintptr_t(d.state.queue_counter),&observed,sizeof(observed));
  IntegratorQueueCounter want{};for(int i:ids){if(expected[i].path.queued_kernel)want.num_queued[expected[i].path.queued_kernel]++;if(expected[i].ao.shadow_path.queued_kernel)want.num_queued[expected[i].ao.shadow_path.queued_kernel]++;}
  require(!std::memcmp(&observed,&want,sizeof(want)),label+": surface queue counters");
  // Shadow stage on the AO paths: intersect, (transparent) shade rounds, film.
  std::vector<IntegratorShadowStateCPU> cpu(shadow_capacity);for(size_t k=0;k<slots.size();k++)cpu[slots[k]]=expected[owners[k]].ao;
  std::vector<float> shadow_wanted(film.data(),film.data()+count);
  device_vector<int> shadow_ids(&d,"ao_state_shadow_indices",MEM_READ_WRITE);shadow_ids.alloc(std::max<size_t>(1,slots.size()));
  for(unsigned round=0;round<16&&!slots.empty();round++){
    std::map<DeviceKernel,std::vector<int>> queues;for(int s:slots)if(cpu[s].shadow_path.queued_kernel)queues[DeviceKernel(cpu[s].shadow_path.queued_kernel)].push_back(s);
    if(queues.empty())break;
    require(round<15,label+": AO shadow paths did not terminate");
    for(auto &[kernel,list]:queues){
      if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW&&round==0){m.ao_paths+=list.size();}
      original_shadow(ds,kernel,list,cpu,shadow_wanted);
      if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW&&round==0)for(int s:list){if(cpu[s].shadow_path.queued_kernel)m.ao_clear++;else m.ao_blocked++;}
      std::copy(list.begin(),list.end(),shadow_ids.data());shadow_ids.copy_to_device();
      IntegratorQueueCounter c{};c.num_queued[kernel]=list.size();d.upload(uintptr_t(d.state.queue_counter),&c,sizeof(c));int k=int(list.size());
      DeviceKernelArguments args;args.add(&shadow_ids.device_pointer);if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW)args.add(&film.device_pointer);args.add(&k);
      require(queue->enqueue(kernel,k,args)&&queue->synchronize(),label+" shadow: "+d.error_message());
      auto queued=fetch(d,uintptr_t(d.state.shadow_path.queued_kernel),size_t(shadow_capacity)*2);
      for(int s:list){uint16_t a;std::memcpy(&a,queued.data()+size_t(s)*2,2);require(a==cpu[s].shadow_path.queued_kernel,label+": AO shadow next kernel slot="+std::to_string(s));m.integers++;}
    }
  }
  film.copy_from_device();healthy(d);for(size_t i=0;i<count;i++)number(film[i],shadow_wanted[i],label+" shadow film="+std::to_string(i),m);
  m.cases++;m.paths+=ids.size();std::cerr<<"Passed "<<label<<" ("<<ids.size()<<" paths, "<<slots.size()<<" AO rays)\n";
}
// Ambient Occlusion node (svm_node_ao) on the SHADE_SURFACE_RAYTRACE queue against the CPU
// integrator_shade_surface_raytrace, and the same shaders on SHADE_SURFACE against the CPU's
// non-raytrace node mask (ao = 1). Variants: default, global radius (distance 0), inside,
// only_local on an instanced box, linked normal and distance.
static Shader *ao_shader(Scene &scene,unsigned variant){
  auto g=make_unique<ShaderGraph>();auto ao=g->create_node<AmbientOcclusionNode>();ao->set_color(make_float3(.9,.7,.4));
  ao->set_distance(variant==1?0.f:1.4f);ao->set_samples(variant==2?8:variant==3?6:4);ao->set_inside(variant==2||variant==3);ao->set_only_local(variant==3);
  if(variant==4){auto geometry=g->create_node<GeometryNode>();auto tilt=g->create_node<VectorMathNode>();tilt->set_math_type(NODE_VECTOR_MATH_ADD);tilt->set_vector2(make_float3(.3f,0,.1f));
    g->connect(geometry->output("Normal"),tilt->input("Vector1"));g->connect(tilt->output("Vector"),ao->input("Normal"));
    auto distance=g->create_node<ValueNode>();distance->set_value(.8f);g->connect(distance->output("Value"),ao->input("Distance"));}
  auto d=g->create_node<DiffuseBsdfNode>();d->set_roughness(.2f);g->connect(ao->output("Color"),d->input("Color"));
  auto glossy=g->create_node<GlossyBsdfNode>();glossy->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_ID);glossy->set_color(make_float3(.3,.4,.8));glossy->set_roughness(.3f);
  auto mix=g->create_node<MixClosureNode>();g->connect(ao->output("AO"),mix->input("Fac"));g->connect(glossy->output("BSDF"),mix->input("Closure1"));g->connect(d->output("BSDF"),mix->input("Closure2"));
  g->connect(mix->output("Closure"),g->output()->input("Surface"));auto sh=scene.create_node<Shader>();sh->set_graph(std::move(g));sh->tag_update(&scene);return sh;
}
static Mesh *ao_mesh(Scene &scene,Shader *shader,const std::vector<float3> &v,const std::vector<int> &t){
  auto mesh=scene.create_node<Mesh>();array<Node*> slots;slots.push_back_slow(shader);mesh->set_used_shaders(slots);mesh->resize_mesh(v.size(),t.size()/3);
  std::copy(v.begin(),v.end(),mesh->get_position_for_write());std::copy(t.begin(),t.end(),mesh->get_triangles().data());
  std::ranges::fill(mesh->get_smooth(),false);std::ranges::fill(mesh->get_shader(),0);mesh->tag_triangles_modified();return mesh;
}
static void ao_node_cases(WebGPUDevice &d,Metrics &m){
  SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;Scene scene(params,&d);
  scene.integrator->set_ao_distance(2.f);scene.integrator->set_use_light_tree(false);scene.camera->set_full_width(64);scene.camera->set_full_height(48);scene.camera->compute_auto_viewplane();
  const std::vector<int> q={0,1,2,0,2,3};std::vector<Object*> targets;
  auto quad=[&](unsigned variant,std::vector<float3> v){auto o=scene.create_node<Object>();o->set_geometry(ao_mesh(scene,ao_shader(scene,variant),v,q));targets.push_back(o);};
  quad(0,{make_float3(-4,-3,3),make_float3(4,-3,3),make_float3(4,3,3),make_float3(-4,3,3)});
  quad(1,{make_float3(-4,-2,-2),make_float3(4,-2,-2),make_float3(4,-2,3),make_float3(-4,-2,3)});
  quad(4,{make_float3(-.8f,-2,0),make_float3(.8f,-2,0),make_float3(.8f,1.1f,0),make_float3(-.8f,1.1f,0)});
  quad(2,{make_float3(-3.5f,-2,1),make_float3(-2.5f,-2,1),make_float3(-2.5f,1,2.5f),make_float3(-3.5f,1,2.5f)});
  std::vector<float3> bv;for(int i=0;i<8;i++)bv.push_back(make_float3(i&1?.5f:-.5f,i&2?.5f:-.5f,i&4?.5f:-.5f));
  const std::vector<int> bt={0,2,1,1,2,3,4,5,6,5,7,6,0,1,4,1,5,4,2,6,3,3,6,7,0,4,2,2,4,6,1,3,5,3,7,5};auto box=ao_mesh(scene,ao_shader(scene,3),bv,bt);
  for(int k=0;k<2;k++){auto o=scene.create_node<Object>();o->set_geometry(box);o->set_tfm(transform_translate(make_float3(k?1.9f:-1.9f,-1.3f,-.6f))*transform_rotate(k?.6f:-.4f,make_float3(0,1,0))*transform_scale(make_float3(1.1f,1.4f,.9f)));targets.push_back(o);}
  for(auto shader:scene.shaders)shader->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
  Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"AO node scene preparation failed");
  auto &ds=scene.dscene;auto &data=ds.data;require(data.kernel_features&KERNEL_FEATURE_NODE_RAYTRACE,"Missing native raytrace node feature");
  data.background.use_mis=0;data.integrator.num_lights=0;data.integrator.use_light_mis=0;data.integrator.use_direct_light=0;data.integrator.filter_glossy=FLT_MAX;data.max_closures=MAX_CLOSURE;
  auto &f=data.film;f.pass_stride=8;f.pass_combined=0;f.pass_emission=4;f.pass_adaptive_aux_buffer=PASS_UNUSED;f.pass_lightgroup=PASS_UNUSED;f.pass_flag=0;f.light_pass_flag=PASSMASK(COMBINED)|PASSMASK(EMISSION);
  d.const_copy_to("data",&data,sizeof(data));
  bool cancelled=false;AoScheduler scheduler(&d,scene.film,&ds,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);auto layout=fields(d);
  std::vector<IntegratorStateCPU> initial(scheduler.capacity());std::vector<int> ids;const float3 camera=make_float3(0,0,-7);
  for(auto object:targets){auto mesh=static_cast<Mesh*>(object->get_geometry());const int triangles=int(mesh->num_triangles());const Transform tfm=object->get_tfm();
    for(int j=0;j<48;j++){const int t=j%triangles;const float u=.08f+.8f*((j*37)%97)/97.f,v=(1-u)*(.05f+.9f*((j*53)%89)/89.f);
      auto tri=mesh->get_triangle(t);const float3 a=mesh->get_position_for_write()[tri.v[0]],b=mesh->get_position_for_write()[tri.v[1]],c=mesh->get_position_for_write()[tri.v[2]];
      float3 P=transform_point(&tfm,(1-u-v)*a+u*b+v*c);
      // Two-thirds of the rays look at the surface from the camera, one third from behind (backfacing, inside).
      const float3 origin=j%3==2?P+(P-camera)*.4f:camera;const float3 D=normalize(P-origin);
      const unsigned i=unsigned(ids.size())*3+1;require(i<initial.size(),"AO node fixture capacity");ids.push_back(int(i));auto &s=initial[i];
      s.isect.object=object->get_device_index();s.isect.prim=int(mesh->prim_offset)+t;s.isect.type=PRIMITIVE_TRIANGLE;s.isect.u=u;s.isect.v=v;s.isect.t=len(P-origin);
      s.ray.P=origin;s.ray.D=D;s.ray.tmax=FLT_MAX;s.ray.time=.5f;s.ray.dP=.0002f;s.ray.dD=.0004f;
      s.path.visibility=PATH_RAY_VISIBILITY_CAMERA;s.path.rng_pixel=0x9e3779b9u*i+7u;s.path.rng_offset=uint16_t(65000u+i*31u);s.path.sample=j*29u;
      s.path.throughput=make_float3(.7,.8,.6);s.path.continuation_probability=1;s.path.min_ray_pdf=FLT_MAX;s.path.flag=PATH_RAY_MIS_SKIP|PATH_RAY_TRANSPARENT_BACKGROUND;
      s.path.render_pixel_index=unsigned(ids.size()-1);}}
  auto queue=d.gpu_queue_create();uint64_t sensitive=0;
  for(const DeviceKernel kernel:{DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE,DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE}){
    auto input=initial;for(int i:ids)input[i].path.queued_kernel=kernel;
    const size_t count=ids.size()*f.pass_stride;std::vector<float> wanted(count,.125f);auto expected=input;
    if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE)original_surface_raytrace(ds,ids,expected,wanted);else original_surface(ds,ids,expected,wanted);
    if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE){auto traced=input;for(int i:ids)traced[i].path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE;
      std::vector<float> unused(count,.125f);original_surface_raytrace(ds,ids,traced,unused);
      for(int i:ids)sensitive+=std::memcmp(&traced[i].path.throughput,&expected[i].path.throughput,sizeof(expected[i].path.throughput))!=0;}
    scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"AO node reset");scheduler.next_shadow(0);upload(d,layout,input);
    device_vector<float> film(&d,"ao_node_film",MEM_READ_WRITE);film.alloc(count);std::fill_n(film.data(),count,.125f);film.copy_to_device();
    device_vector<int> selected(&d,"ao_node_indices",MEM_READ_WRITE);selected.alloc(ids.size());std::copy(ids.begin(),ids.end(),selected.data());selected.copy_to_device();
    IntegratorQueueCounter counter{};counter.num_queued[kernel]=ids.size();d.upload(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
    int n=int(ids.size());DeviceKernelArguments args;args.add(&selected.device_pointer);args.add(&film.device_pointer);args.add(&n);
    const std::string label=kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE?"AO node, non-raytrace mask":"AO node, raytrace queue";
    require(queue->enqueue(kernel,n,args)&&queue->synchronize(),label+": "+d.error_message());
    const auto actual=download(d,layout,input.size());
    for(auto &field:layout)for(int i:ids){auto a=reinterpret_cast<const char*>(&actual[i])+field.offset;auto e=reinterpret_cast<const char*>(&expected[i])+field.offset;
      if(field.floating){for(size_t k=0;k<field.stride;k+=4){float av,ev;std::memcpy(&av,a+k,4);std::memcpy(&ev,e+k,4);number(av,ev,label+" "+field.name+" path="+std::to_string(i),m);}}
      else{require(!std::memcmp(a,e,field.stride),label+" integer "+field.name+" path="+std::to_string(i));m.integers++;}}
    film.copy_from_device();healthy(d);for(size_t i=0;i<count;i++)number(film[i],wanted[i],label+" film="+std::to_string(i),m);
    IntegratorQueueCounter observed{};d.download(uintptr_t(d.state.queue_counter),&observed,sizeof(observed));IntegratorQueueCounter want{};
    for(int i:ids)if(expected[i].path.queued_kernel)want.num_queued[expected[i].path.queued_kernel]++;require(!std::memcmp(&observed,&want,sizeof(want)),label+": queue counters");
    m.cases++;m.paths+=ids.size();m.node_paths+=kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE?ids.size():0;std::cerr<<"Passed "<<label<<" ("<<ids.size()<<" paths)\n";
  }
  // The node's ray tracing changes the result: raytrace and non-raytrace CPU throughputs differ.
  m.node_sensitive=sensitive;require(sensitive>50,"AO node coverage: raytrace and non-raytrace results differ for only "+std::to_string(sensitive)+" paths");
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2,"Usage: ao_state_gate OUTPUT_PREFIX");auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");
  Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=require_webgpu_device(owner.get(),"ao_state_gate device creation");Metrics m;
  {
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;
    Scene scene(params,&d);prepare(scene);
    // A large occluder above the fixture objects: AO rays reach it at the longer distances.
    Object *occluder;{auto mesh=scene.create_node<Mesh>();array<Node*> slots;slots.push_back_slow(scene.default_surface);mesh->set_used_shaders(slots);mesh->resize_mesh(4,2);
      const float3 v[]={make_float3(-20,-20,7.5f),make_float3(20,-20,7.5f),make_float3(20,20,7.5f),make_float3(-20,20,7.5f)};std::copy_n(v,4,mesh->get_position_for_write());
      const int t[]={0,1,2,0,2,3};std::copy_n(t,6,mesh->get_triangles().data());std::ranges::fill(mesh->get_smooth(),false);std::ranges::fill(mesh->get_shader(),0);mesh->tag_triangles_modified();
      occluder=scene.create_node<Object>();occluder->set_geometry(mesh);}
    for(auto shader:scene.shaders)shader->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
    Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"AO state scene preparation failed");
    auto &data=scene.dscene.data;
    data.background.use_mis=0;data.integrator.num_lights=0;data.integrator.use_light_mis=0;data.integrator.use_direct_light=0;data.integrator.use_caustics=0;
    data.integrator.filter_glossy=FLT_MAX;data.max_closures=MAX_CLOSURE;
    auto &f=data.film;f.pass_stride=40;f.pass_combined=0;f.pass_emission=4;f.pass_adaptive_aux_buffer=7;f.pass_ao=11;f.pass_lightgroup=PASS_UNUSED;f.pass_flag=0;
    f.light_pass_flag=PASSMASK(COMBINED)|PASSMASK(EMISSION)|PASSMASK(AO);
    data.kernel_features|=KERNEL_FEATURE_AO;data.integrator.ao_additive_factor=.6f;data.integrator.ao_bounces_distance=1.3f;
    d.const_copy_to("data",&data,sizeof(data));const auto original=data;
    bool cancelled=false;AoScheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);auto layout=fields(d);
    require(d.state.shadow_path.unshadowed_throughput,"Missing AO shadow state");
    std::vector<IntegratorStateCPU> initial(scheduler.capacity());auto all=states_for(scene,initial);std::vector<int> ids;
    for(int i:all){if(initial[i].isect.object==int(occluder->get_device_index()))initial[i]=IntegratorStateCPU{};else ids.push_back(i);}
    for(size_t j=0;j<ids.size();j++){auto &s=initial[ids[j]];s.path.throughput=make_float3(.6,.8,.4);s.path.continuation_probability=1;s.path.min_ray_pdf=FLT_MAX;s.ray.tmax=FLT_MAX;
      s.path.flag=PATH_RAY_MIS_SKIP|PATH_RAY_TRANSPARENT_BACKGROUND;
      s.path.visibility=j%3==2?PATH_RAY_VISIBILITY_DIFFUSE:PATH_RAY_VISIBILITY_CAMERA;if(j%3==2){s.path.bounce=1+j%2;s.path.flag=PATH_RAY_REFLECT|PATH_RAY_SURFACE_PASS;}}
    std::cerr<<"AO state paths="<<ids.size()<<" transparent shadows="<<data.integrator.transparent_shadows<<'\n';
    struct Variant {const char *name;uint features;float factor,distance;bool dense;uint filter;float clamp;};
    const Variant variants[]={{"additive and pass",KERNEL_FEATURE_AO,.6f,1.3f,false,0,0},{"additive",KERNEL_FEATURE_AO_ADDITIVE,.45f,2.f,false,0,0},
      {"pass only",KERNEL_FEATURE_AO_PASS,0,1.f,false,0,0},{"short distance, clamped",KERNEL_FEATURE_AO,.8f,.25f,false,0,.05f},
      {"diffuse filtered",KERNEL_FEATURE_AO,.6f,1.3f,false,FILTER_CLOSURE_DIFFUSE,0},{"dense dispatch",KERNEL_FEATURE_AO,.6f,1.3f,true,0,0},{"long distance",KERNEL_FEATURE_AO,.6f,9.f,false,0,0}};
    for(const auto &v:variants){
      data=original;data.kernel_features=(original.kernel_features&~KERNEL_FEATURE_AO)|v.features;data.integrator.ao_additive_factor=v.factor;data.integrator.ao_bounces_distance=v.distance;
      data.integrator.filter_closures|=v.filter;if(v.clamp>0){data.integrator.sample_clamp_direct=v.clamp;data.integrator.sample_clamp_indirect=v.clamp*.5f;}
      d.const_copy_to("data",&data,sizeof(data));
      if(v.dense){auto dense=initial;std::vector<int> dense_ids;for(size_t i=0;i<ids.size();i++){dense[i]=initial[ids[i]];dense_ids.push_back(int(i));}
        run_case(d,scheduler,scene,layout,dense,dense_ids,v.name,m,true);}
      else run_case(d,scheduler,scene,layout,initial,ids,v.name,m,false);
    }
  }
  ao_node_cases(d,m);
  require(stats.mem_used==0,"AO state Cycles allocation leak");
  require(m.ao_clear>100&&m.ao_blocked>100,"Missing AO shadow ray coverage: unoccluded="+std::to_string(m.ao_clear)+" occluded="+std::to_string(m.ao_blocked));
  std::ofstream(std::string(argv[1])+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(std::string(argv[1])+".library.txt")<<d.gpu.library_path;
  std::cout<<std::setprecision(17)<<"{\"passed\":true,\"originalCpuSurfaceAndShadowIntegrator\":true,\"cases\":"<<m.cases<<",\"paths\":"<<m.paths<<",\"aoShadowRays\":"<<m.ao_paths<<",\"aoUnoccluded\":"<<m.ao_clear
    <<",\"aoOccluded\":"<<m.ao_blocked<<",\"aoNodePaths\":"<<m.node_paths<<",\"aoNodeSensitivePaths\":"<<m.node_sensitive<<",\"integerChecks\":"<<m.integers<<",\"floatChecks\":"<<m.floats<<",\"strictFloatFailures\":"<<m.strict<<",\"maximumAbsoluteError\":"<<m.maximum
    <<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<",\"pipelineReport\":"<<ccl::pipeline_process_report_json()<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
