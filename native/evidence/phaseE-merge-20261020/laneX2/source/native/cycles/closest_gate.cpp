// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "wavefront_test.h"
#include "closest_reference.h"
#include "scene/scene.h"
#include "object_motion_fixture.h"
#include "deforming_motion_fixture.h"
#include "precision_fixture.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/camera.h"
#include "scene/integrator.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/progress.h"
#include "util/task.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
using namespace ccl;
using namespace wavefront_test;
struct Metrics {uint64_t paths=0,exact_checks=0,float_checks=0,strict_failures=0,hits=0,misses=0,terminated=0,rr=0,emission_termination=0,raytrace_states=0;double maximum_error=0;unsigned cases=0,instances=0,sort_cases=0,direct_cases=0;std::map<int,unsigned> object_hits;};
static void compare(WebGPUDevice &d,Scene &scene,const std::vector<Field> &layout,std::vector<IntegratorStateCPU> &expected,const std::vector<int> &indices,Metrics &m){
  std::set<int> selected(indices.begin(),indices.end());std::map<int,std::vector<int>> sorting;
  for(int kernel:{DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE})if(d.state.sort_key_counter[kernel])sorting[kernel].resize(d.remaining(uintptr_t(d.state.sort_key_counter[kernel]))/4);
  for(size_t i=0;i<expected.size();i++){
    auto &s=expected[i];if(s.path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE||s.path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE){
      // CPU scheduling omits GPU sort keys/counters. Compute precisely the
      // original INTEGRATOR_SORT_KEY formula, independent of the GPU outputs.
      auto shader=scene.dscene.tri_shader[s.isect.prim]&SHADER_MASK;
      s.path.shader_sort_key=shader+scene.dscene.data.max_shaders*(i/d.state.sort_partition_divisor);
      sorting.at(s.path.queued_kernel).at(s.path.shader_sort_key)++;
    }
  }
  auto actual=download(d,layout,expected.size());
  for(auto &f:layout)for(size_t i=0;i<expected.size();i++){
    auto *e=reinterpret_cast<const uint8_t*>(&expected[i])+f.offset;auto *a=reinterpret_cast<const uint8_t*>(&actual[i])+f.offset;
    bool numeric=selected.contains(i)&&(f.name=="isect.t"||f.name=="isect.u"||f.name=="isect.v"||f.name=="path.continuation_probability");
    if(numeric){float av,ev;std::memcpy(&av,a,4);std::memcpy(&ev,e,4);double error=std::abs(double(av)-ev);m.maximum_error=std::max(m.maximum_error,error);m.float_checks++;
      if(error>1e-7+1e-7*std::abs(double(ev)))m.strict_failures++;
      require(std::isfinite(av)&&error<=5e-5+2e-4*std::abs(double(ev)),"Closest numeric mismatch "+f.name+" index="+std::to_string(i)+" got="+std::to_string(av)+" expected="+std::to_string(ev));
    }else{m.exact_checks++;require(std::memcmp(a,e,f.stride)==0,"Closest state mismatch "+f.name+" index="+std::to_string(i)+" got="+std::to_string(actual[i].isect.prim)+" expected="+std::to_string(expected[i].isect.prim));}
  }
  IntegratorQueueCounter counter{};d.download(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
  std::vector<int> counts(DEVICE_GPU_KERNEL_INTEGRATOR_NUM);for(auto &s:expected)if(s.path.queued_kernel)counts.at(s.path.queued_kernel)++;
  for(unsigned k=0;k<counts.size();k++)require(counter.num_queued[k]==counts[k],"Closest queue count differs");
  require(!counter.cache_miss,"Unexpected closest cache miss");
  for(auto &[kernel,wanted]:sorting){std::vector<int> values(wanted.size());d.download(uintptr_t(d.state.sort_key_counter[kernel]),values.data(),values.size()*4);require(values==wanted,"Closest shader sorting counters differ");}
  for(int i:indices){auto &s=expected[i];m.hits+=s.isect.prim!=PRIM_NONE;m.misses+=s.isect.prim==PRIM_NONE;m.terminated+=s.path.queued_kernel==0;
    if(s.isect.prim!=PRIM_NONE)m.object_hits[s.isect.object]++;
    m.raytrace_states+=s.path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE;
    m.rr+=s.path.continuation_probability>0&&s.path.continuation_probability<1;m.emission_termination+=(s.path.flag&PATH_RAY_TERMINATE_ON_NEXT_SURFACE)!=0;}
  m.paths+=indices.size();m.cases++;
}
static void prepare(Scene &scene){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);
  scene.integrator->set_aa_samples(64);scene.integrator->set_seed(17);scene.film->set_use_sample_count(false);
  scene.camera->set_full_width(64);scene.camera->set_full_height(48);scene.camera->set_screen_size(64,48);scene.camera->set_fov(1.0f);
  scene.camera->set_matrix(transform_translate(make_float3(0,0,-8)));scene.camera->compute_auto_viewplane();
  auto emission=scene.create_node<Shader>();auto graph=make_unique<ShaderGraph>();auto light=graph->create_node<EmissionNode>();light->set_color(make_float3(.4,.6,.8));light->set_strength(1);
  graph->connect(light->output("Emission"),graph->output()->input("Surface"));emission->set_graph(std::move(graph));emission->tag_update(&scene);
  auto mesh=[&](Shader *shader){auto m=scene.create_node<Mesh>();array<Node*> shaders;shaders.push_back_slow(shader);m->set_used_shaders(shaders);m->resize_mesh(4,2);
    const float v[][3]={{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};for(int i=0;i<4;i++)m->get_position_for_write()[i]=make_float3(v[i][0],v[i][1],v[i][2]);
    const int tris[]={0,1,2,0,2,3};std::copy_n(tris,6,m->get_triangles().data());std::ranges::fill(m->get_smooth(),false);std::ranges::fill(m->get_shader(),0);m->tag_triangles_modified();return m;};
  auto shared=mesh(scene.default_surface);
  for(int i=0;i<3;i++){auto object=scene.create_node<Object>();object->set_geometry(shared);
    auto tfm=transform_translate(make_float3(float((i-1)*2.3f),0,float(i)*.4f))*transform_rotate(.17f*i,make_float3(0,1,0))*transform_scale(make_float3(i==2?-1.2f:1.f,.8f+.2f*i,1.5f));
    object->set_tfm(tfm);object->set_ao_distance(i==1?.25f:0);
    if(i==2)object->set_visibility(PATH_RAY_VISIBILITY_TRANSMIT|PATH_RAY_VISIBILITY_DIFFUSE);
  }
  auto object=scene.create_node<Object>();object->set_geometry(mesh(emission));object->set_tfm(transform_translate(make_float3(0,2.1f,1))*transform_scale(make_float3(1.8f,.8f,1)));
  auto raytrace=scene.create_node<Shader>();auto rg=make_unique<ShaderGraph>();auto ao=rg->create_node<AmbientOcclusionNode>();auto diffuse=rg->create_node<DiffuseBsdfNode>();
  rg->connect(ao->output("Color"),diffuse->input("Color"));rg->connect(diffuse->output("BSDF"),rg->output()->input("Surface"));raytrace->set_graph(std::move(rg));raytrace->tag_update(&scene);
  auto ro=scene.create_node<Object>();ro->set_geometry(mesh(raytrace));ro->set_tfm(transform_translate(make_float3(0,-2.1f,1))*transform_scale(make_float3(1.8f,.8f,1)));
}
static unsigned consume_sort_counters(WebGPUDevice &d,Scheduler &scheduler,const std::vector<IntegratorStateCPU> &states){
  unsigned cases=0;
  for(auto kernel:{DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE}){
    std::set<int> expected;for(size_t i=0;i<states.size();i++)if(states[i].path.queued_kernel==kernel)expected.insert(i);
    require(!expected.empty(),"Missing scheduled shading paths");
    scheduler.compute_sorted_queued_paths(kernel,INT_MAX);require(scheduler.update_queue_counter_and_cache(),"Original scheduler sort failed");
    auto &buffer=scheduler.sorted_paths();buffer.copy_from_device();healthy(d);uint32_t previous=0;const size_t count=expected.size();
    for(size_t i=0;i<count;i++){
      int index=buffer[i];require(expected.contains(index),"Sorted path missing/duplicate/wrong queue");
      require(i==0||states[index].path.shader_sort_key>=previous,"Shader buckets are not sorted");previous=states[index].path.shader_sort_key;expected.erase(index);
    }
    require(expected.empty(),"Sort omitted scheduled paths");
    std::vector<int> counters(d.remaining(uintptr_t(d.state.sort_key_counter[kernel]))/4);
    d.download(uintptr_t(d.state.sort_key_counter[kernel]),counters.data(),counters.size()*4);
    require(std::all_of(counters.begin(),counters.end(),[](int n){return n==0;}),"Sort did not consume all shader counters");
    cases++;
  }
  return cases;
}
// Traversal precision: sliver triangles (with the Principled bench ray), mirrored
// instances and an instanced emitter, edge-aimed and ULP-perturbed rays. Hits
// must equal the CPU oracle bit for bit (t, u, v, prim, object, type).
struct Precision {uint64_t paths=0,hits=0,instanced_hits=0,emitter_hits=0,bit_mismatches=0,decision_mismatches=0,strict=0;double maximum=0;bool bench_exact=false;};
static Precision precision(const DeviceInfo &info){
  Precision p;Stats stats;Profiler profiler;auto owner=Device::create(info,stats,profiler,true);auto *backend=dynamic_cast<WebGPUDevice*>(owner.get());require(backend,"Precision device creation failed");auto &d=*backend;
  {
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=false;params.use_texture_cache=false;
    Scene scene(params,&d);
    scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);
    scene.integrator->set_aa_samples(64);scene.integrator->set_seed(17);scene.film->set_use_sample_count(false);
    scene.camera->set_full_width(64);scene.camera->set_full_height(48);scene.camera->set_screen_size(64,48);scene.camera->compute_auto_viewplane();
    precision_fixture(scene);Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Precision scene update failed");
    auto &data=scene.dscene.data;data.integrator.use_light_mis=0;data.integrator.use_caustics=0;d.const_copy_to("data",&data,sizeof(KernelData));
    unsigned instances=0;for(size_t i=0;i<scene.dscene.bvh_leaf_nodes.size();i++)instances+=scene.dscene.bvh_leaf_nodes[i].x<0;
    require(instances>=5,"Precision fixture lost its instances");
    bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);
    auto layout=fields(d);BufferParams bp;bp.width=64;bp.height=48;bp.pass_stride=data.film.pass_stride;
    auto buffers=scheduler.get_render_buffers();buffers->reset(bp);buffers->zero();
    const unsigned n=std::min(scheduler.capacity(),4096);const auto rays=precision_rays(scene,n,false);
    for(unsigned round=0;round<2;round++){
      scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Precision reset failed");
      std::vector<IntegratorStateCPU> expected(scheduler.capacity());std::vector<int> indices;
      for(unsigned i=0;i<n;i++){auto &s=expected[i];s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST;s.path.visibility=PATH_RAY_VISIBILITY_CAMERA;
        s.path.throughput=one_float3();s.path.continuation_probability=1;s.path.sample=i%64;s.path.render_pixel_index=i%3072;
        s.ray.P=rays[i].P;s.ray.D=rays[i].D;s.ray.tmin=0;s.ray.tmax=rays[i].tmax;s.ray.time=.5f;s.isect.prim=PRIM_NONE;s.isect.object=OBJECT_NONE;
        // Round 1 marks every other aimed triangle as the previous hit (self-hit skip).
        if(round&&i%2){s.isect.object=rays[i].object;s.isect.prim=rays[i].prim;s.isect.type=PRIMITIVE_TRIANGLE;}
        indices.push_back(int(i));}
      upload(d,layout,expected);IntegratorQueueCounter counters{};counters.num_queued[DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST]=n;d.upload(uintptr_t(d.state.queue_counter),&counters,sizeof(counters));
      scheduler.set_active(int(n));require(scheduler.update_queue_counter_and_cache(),"Precision counters failed");
      std::vector<float> film(buffers->buffer.size());original_closest(scene.dscene,indices,expected,film);
      scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST);require(scheduler.update_queue_counter_and_cache(),"Precision closest failed: "+d.error_message());
      auto actual=download(d,layout,n);
      for(unsigned i=0;i<n;i++){const auto &e=expected[i].isect;const auto &a=actual[i].isect;p.paths++;
        const bool same_hit=e.prim==a.prim&&e.object==a.object&&e.type==a.type;p.decision_mismatches+=!same_hit;
        const bool bits=same_hit&&std::memcmp(&e.t,&a.t,4)==0&&std::memcmp(&e.u,&a.u,4)==0&&std::memcmp(&e.v,&a.v,4)==0;p.bit_mismatches+=!bits;
        if(e.prim!=PRIM_NONE){p.hits++;p.instanced_hits+=e.object!=0;p.emitter_hits+=e.object>=3;}
        if(same_hit)for(auto [ev,av]:{std::pair{e.t,a.t},std::pair{e.u,a.u},std::pair{e.v,a.v}}){double error=std::abs(double(av)-ev);p.maximum=std::max(p.maximum,error);p.strict+=error>1e-7+1e-7*std::abs(double(ev));}
        if(round==0&&i==0)p.bench_exact=bits&&e.prim==0&&e.object==0;
      }
    }
    std::cerr<<"precision paths="<<p.paths<<" hits="<<p.hits<<" instancedHits="<<p.instanced_hits<<" emitterHits="<<p.emitter_hits<<" decisionMismatches="<<p.decision_mismatches
      <<" bitMismatches="<<p.bit_mismatches<<" strict="<<p.strict<<" max="<<p.maximum<<" benchExact="<<p.bench_exact<<"\n";
    require(p.hits>1000&&p.hits<p.paths&&p.instanced_hits>500&&p.emitter_hits>100,"Missing precision coverage");
    require(p.decision_mismatches==0&&p.bit_mismatches==0&&p.bench_exact,"Closest precision differs from the CPU oracle: decisions="+std::to_string(p.decision_mismatches)+" bitMismatches="+std::to_string(p.bit_mismatches));
  }
  require(stats.mem_used==0,"Precision closest leaked Cycles allocations");
  return p;
}
static unsigned rejections(const DeviceInfo &info){
  unsigned rejected=0;
  for(unsigned test=0;test<21;test++){
    Stats stats;Profiler profiler;auto owner=Device::create(info,stats,profiler,true);auto *backend=dynamic_cast<WebGPUDevice*>(owner.get());require(backend,"Negative closest device creation failed");auto &d=*backend;
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=false;params.use_texture_cache=false;
    Scene scene(params,&d);prepare(scene);if(test>=16)object_motion_fixture(scene,*scene.objects[0],0);Progress progress;scene.device_update(&d,progress);healthy(d);
    auto &data=scene.dscene.data;data.integrator.use_light_mis=0;data.integrator.use_caustics=0;
    bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Negative closest setup failed");
    const auto layout=fields(d);std::vector<IntegratorStateCPU> states(scheduler.capacity());auto &s=states[0];
    s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST;s.path.visibility=PATH_RAY_VISIBILITY_CAMERA;s.path.throughput=one_float3();s.path.continuation_probability=1;
    s.ray.P=make_float3(0,0,-8);s.ray.D=make_float3(0,0,1);s.ray.tmax=100;s.ray.time=.5;s.isect.prim=PRIM_NONE;s.isect.object=OBJECT_NONE;
    std::string reason="reported invalid data";
    if(test==0){data.bvh.have_motion=1;reason="motion flag/table mismatch";}
    if(test==1){data.bvh.have_curves=1;reason="triangle primitives";}
    if(test==2){data.bvh.have_points=1;reason="triangle primitives";}
    if(test==3){data.kernel_features|=KERNEL_FEATURE_VOLUME;data.integrator.volume_ray_marching=1;reason="biased volume ray marching";}
    if(test==4){data.integrator.use_caustics=1;reason="optional integration features";}
    if(test==5){data.integrator.use_light_mis=1;data.integrator.use_light_tree=1;reason="light tree not implemented";}
    if(test==6){data.integrator.sampling_pattern=SAMPLING_PATTERN_SOBOL_BURLEY;reason="tabulated Sobol";}
    if(test==8)data.bvh.root=INT_MAX;
    if(test==9){for(size_t i=0;i<scene.dscene.tri_vindex.size();i++)scene.dscene.tri_vindex[i].x=0x7fffffffu;scene.dscene.tri_vindex.copy_to_device();}
    if(test==10){for(size_t i=0;i<scene.dscene.prim_index.size();i++)scene.dscene.prim_index[i]=INT_MAX;scene.dscene.prim_index.copy_to_device();}
    if(test==11)s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND;
    if(test==12)reason="pointer argument";
    if(test==13)reason="Incomplete closest";
    if(test==14){auto state=d.state;state.sort_key_counter[DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE]=nullptr;d.const_copy_to("integrator_state",&state,sizeof(state));s.ray.D=make_float3(0,-2.1f/9.f,1);}
    if(test==15){for(size_t i=0;i<scene.dscene.bvh_leaf_nodes.size();i++)if(scene.dscene.bvh_leaf_nodes[i].x>=0)scene.dscene.bvh_leaf_nodes[i].w=PRIMITIVE_POINT;scene.dscene.bvh_leaf_nodes.copy_to_device();}
    if(test>=16){auto &object=scene.dscene.objects[0];auto &flags=scene.dscene.object_flag[0];
      if(test==16){flags|=SD_OBJECT_HAS_VERTEX_MOTION;reason="deforming triangle flags/type";}
      if(test==17){object.num_tfm_steps=1;reason="motion range";}
      if(test==18){flags|=SD_OBJECT_TRANSFORM_APPLIED;reason="applied object motion";}
      if(test==19){object.primitive_type=PRIMITIVE_LAMP;reason="animated emitters";}
      if(test==20){object.motion_offset=uint(scene.dscene.object_motion.size());reason="motion range";}
      scene.dscene.objects.copy_to_device();scene.dscene.object_flag.copy_to_device();}
    d.const_copy_to("data",&data,sizeof(data));upload(d,layout,states);healthy(d);
    device_vector<float> film(&d,"negative_closest_film",MEM_READ_WRITE);film.alloc(16);film.zero_to_device();
    device_vector<int> indices(&d,"negative_closest_indices",MEM_READ_WRITE);indices.alloc(1);indices[0]=test==7?scheduler.capacity():0;indices.copy_to_device();
    IntegratorQueueCounter counter{};counter.num_queued[s.path.queued_kernel]=1;d.upload(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
    auto queue=d.gpu_queue_create();int count=test==13?2:1,wrong=0;
    DeviceKernelArguments args;if(test==12)args.add(&wrong);else args.add(&indices.device_pointer);args.add(&film.device_pointer);args.add(&count);
    bool accepted=queue->enqueue(DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST,1,args);if(accepted)accepted=queue->synchronize();
    require(!accepted&&d.error_message().find(reason)!=std::string::npos,"Closest negative case "+std::to_string(test)+": "+d.error_message());
    IntegratorQueueCounter after{};d.download(uintptr_t(d.state.queue_counter),&after,sizeof(after));require(std::memcmp(&after,&counter,sizeof(counter))==0,"Rejected closest dispatch changed counters");
    std::vector<float> unchanged(16);d.download(film.device_pointer,unchanged.data(),64);require(std::all_of(unchanged.begin(),unchanged.end(),[](float v){return v==0;}),"Rejected closest dispatch changed film");rejected++;
  }
  return rejected;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2||(argc==3&&(std::string(argv[2])=="--motion"||std::string(argv[2])=="--deform"||std::string(argv[2])=="--deform-motion")),"Usage: closest_gate OUTPUT_PREFIX [--motion|--deform|--deform-motion]");const bool deform=argc==3&&std::string(argv[2])!="--motion";const bool motion=argc==3&&std::string(argv[2])!="--deform";auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");
  Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto *device=dynamic_cast<WebGPUDevice*>(owner.get());require(device,"Missing registered backend");auto &d=*device;Metrics m;
  {
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=false;params.use_texture_cache=false;
    Scene scene(params,&d);prepare(scene);if(motion){for(size_t i=0;i<scene.objects.size();i++)if(i<3)object_motion_fixture(scene,*scene.objects[i],unsigned(i));}if(deform)for(size_t i=0;i<3;i++)deforming_motion_fixture(scene,*scene.objects[i],unsigned(i));Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Scene update failed");
    scene.dscene.data.integrator.use_light_mis=0; // Isolate surface/BVH stage; forward light intersection remains pending.
    scene.dscene.data.integrator.use_caustics=0;d.const_copy_to("data",&scene.dscene.data,sizeof(KernelData));
    for(size_t i=0;i<scene.dscene.bvh_leaf_nodes.size();i++)if(scene.dscene.bvh_leaf_nodes[i].x<0)m.instances++;
    require(m.instances>=3,"Fixture did not preserve native instances");
    bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);
    auto layout=fields(d);BufferParams bp;bp.width=64;bp.height=48;bp.pass_stride=scene.dscene.data.film.pass_stride;
    auto buffers=scheduler.get_render_buffers();buffers->reset(bp);buffers->zero();scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Initial reset failed");
    KernelWorkTile tile{};tile.w=64;tile.h=48;tile.stride=64;tile.num_samples=1;tile.start_sample=7;
    scheduler.enqueue_work_tiles(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,&tile,1,3,0);require(scheduler.update_queue_counter_and_cache(),"Initial camera dispatch failed");
    auto camera=download(d,layout,scheduler.capacity());std::vector<int> indices;for(int i=0;i<int(camera.size());i++)if(camera[i].path.queued_kernel)indices.push_back(i);
    require(indices.size()==3072,"Camera path count differs");std::vector<float> film(buffers->buffer.size());auto expected=camera;
    original_closest(scene.dscene,indices,expected,film);
    scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST);require(scheduler.update_queue_counter_and_cache(),"Scheduler closest failed: "+d.error_message());
    compare(d,scene,layout,expected,indices,m);auto first_hits=expected;
    m.sort_cases=consume_sort_counters(d,scheduler,expected);
    const auto original=scene.dscene.data;
    for(unsigned test=0;test<16;test++){
      auto &data=scene.dscene.data;data=original;data.integrator.min_bounce=1;data.integrator.transparent_min_bounce=1;
      if(test>=8&&test<12){data.integrator.ao_bounces=1;data.integrator.ao_bounces_distance=test%2?9.f:.25f;}
      if(test>=12)data.integrator.filter_closures|=FILTER_CLOSURE_DIRECT_LIGHT;
      data.integrator.scrambling_distance=std::array{.125f,.75f,.9f,1.f}[test%4];d.const_copy_to("data",&data,sizeof(data));healthy(d);
      scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Closest test reset failed");
      expected=camera;indices.clear();
      // Noncontiguous paths force the original queue-selection entry to build
      // the index array; all other slots are exact guards.
      for(int i=0;i<int(expected.size());i++){
        auto &s=expected[i];s.path.queued_kernel=0;
        if(i>=3&&i<3075&&i%3!=0){indices.push_back(i);s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST;
          s.path.bounce=test<12?3:1;s.path.sample=(i*13)%64;s.path.throughput=make_float3(.04f,.16f,.09f);
          if(test%4==0)s.path.throughput=zero_float3();
          if(test%4==3)s.path.throughput=one_float3();
          if(test>=4&&test<8){s.path.flag|=PATH_RAY_TRANSPARENT;s.path.transparent_bounce=3;}
          if(test%4==1){s.path.visibility=PATH_RAY_VISIBILITY_TRANSMIT|PATH_RAY_VISIBILITY_DIFFUSE|PATH_RAY_VISIBILITY_GLOSSY;}
          if(test%4==2){s.path.visibility=PATH_RAY_VISIBILITY_DIFFUSE;}
          if(test%4==3&&first_hits[i].isect.prim!=PRIM_NONE){s.isect=first_hits[i].isect;}
          if(test>=12&&i%13==0)s.ray.D=make_float3(0,0,0);
        }
      }
      upload(d,layout,expected);IntegratorQueueCounter counters{};counters.num_queued[DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST]=indices.size();d.upload(uintptr_t(d.state.queue_counter),&counters,sizeof(counters));
      scheduler.set_active(3075);require(scheduler.update_queue_counter_and_cache(),"Closest synthetic counters failed");
      original_closest(scene.dscene,indices,expected,film);
      scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST);require(scheduler.update_queue_counter_and_cache(),"Closest iteration failed: "+d.error_message());
      compare(d,scene,layout,expected,indices,m);
      buffers->buffer.copy_from_device();healthy(d);require(std::memcmp(buffers->buffer.data(),film.data(),film.size()*4)==0,"Closest unexpectedly changed film");
    }
    for(int n:{1,63,64,65,193}){
      scene.dscene.data=original;d.const_copy_to("data",&scene.dscene.data,sizeof(KernelData));
      scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Direct closest reset failed");
      expected.assign(camera.size(),IntegratorStateCPU{});indices.clear();
      for(int i=0;i<n;i++){expected[i]=camera[3+(1375+i*31)%3072];indices.push_back(i);}
      upload(d,layout,expected);IntegratorQueueCounter counters{};counters.num_queued[DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST]=n;d.upload(uintptr_t(d.state.queue_counter),&counters,sizeof(counters));
      scheduler.set_active(n);require(scheduler.update_queue_counter_and_cache(),"Direct closest counters failed");
      original_closest(scene.dscene,indices,expected,film);
      // All slots in [0,n) are queued, so the original scheduler supplies a
      // null index array and dispatches directly, including partial groups.
      scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST);require(scheduler.update_queue_counter_and_cache(),"Direct closest dispatch failed");
      compare(d,scene,layout,expected,indices,m);m.direct_cases++;
    }
    require(m.hits>1000&&m.misses>1000&&m.terminated>100&&m.rr>100&&m.emission_termination>10&&m.raytrace_states>100,"Missing closest behavior coverage");
    require(m.object_hits.size()==5,"Not every instance/material object was hit");
  }
  require(stats.mem_used==0,"Closest test leaked Cycles allocations");const unsigned rejected=rejections(infos[0]);
  const Precision exact=motion||deform?Precision{}:precision(infos[0]);
  std::string output=argv[1];std::ofstream(output+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(output+".library.txt")<<d.gpu.library_path;
  std::cout<<"{\"passed\":true,\"originalCameraToIntersectionScheduler\":true,\"nativeSceneTables\":true,\"nativeInstanceLeaves\":"<<m.instances<<",\"cases\":"<<m.cases
    <<",\"paths\":"<<m.paths<<",\"hits\":"<<m.hits<<",\"misses\":"<<m.misses<<",\"terminated\":"<<m.terminated<<",\"rouletteStates\":"<<m.rr<<",\"emissionTerminationStates\":"<<m.emission_termination
    <<",\"raytraceShaderStates\":"<<m.raytrace_states<<",\"hitObjects\":"<<m.object_hits.size()
    <<",\"originalSchedulerSortCases\":"<<m.sort_cases
    <<",\"directDispatchCases\":"<<m.direct_cases
    <<",\"invalidCasesRejected\":"<<rejected<<",\"precisionPaths\":"<<exact.paths<<",\"precisionHits\":"<<exact.hits<<",\"precisionInstancedHits\":"<<exact.instanced_hits<<",\"precisionEmitterHits\":"<<exact.emitter_hits<<",\"precisionBitMismatches\":"<<exact.bit_mismatches<<",\"precisionBenchRayExact\":"<<(exact.bench_exact?"true":"false")
    <<",\"exactStateChecks\":"<<m.exact_checks<<",\"floatChecks\":"<<m.float_checks<<",\"strictFloatFailures\":"<<m.strict_failures<<",\"maximumAbsoluteError\":"<<m.maximum_error
    <<",\"absoluteTolerance\":0.00005,\"relativeTolerance\":0.0002,\"strictAbsoluteTolerance\":0.0000001,\"strictRelativeTolerance\":0.0000001,\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
