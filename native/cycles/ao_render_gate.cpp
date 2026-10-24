// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Ambient occlusion image gate: fast GI (replace and additive modes, per-object
// AO distance, an HDRI world), the AO pass and the Ambient Occlusion shader node,
// rendered by the original GPU scheduler on the WebGPU device against an
// independent loop of the unmodified CPU kernels on the same prepared scene.
// Device-scene flow and replay capture as lighting_gate.cpp.
#include "wavefront_test.h"
#include "render_capture.h"
#include "reference_policy.h"
#include "camera_reference.h"
#include "closest_reference.h"
#include "surface_reference.h"
#include "background_reference.h"
#include "light_reference.h"
#include "shadow_reference.h"
#include "environment_reference.h"
#include "ao_reference.h"
#include "environment_fixture.h"
#include "scene/camera.h"
#include "scene/film.h"
#include "scene/integrator.h"
#include "scene/pass.h"
#include "util/progress.h"
#include "util/task.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
using namespace ccl;
using namespace wavefront_test;
constexpr unsigned width=32,height=24,samples=8;
// Fast GI settings (Integrator sockets), the world, the AO pass and the AO node.
struct Case {const char *name;int ao_bounces;float ao_factor,ao_distance,additive;bool object_distance,hdri,transparent,pass;int node;};
// node: -1 none; 0 AO node mixing two BSDFs on the centre quad; 1 the same node with
// only_local and inside on an instanced object.
static const Case cases[]={
  {"replace",1,.5f,1.f,0,false,false,false,false,-1},
  {"replace-object-distance",1,.8f,3.f,0,true,false,false,false,-1},
  {"additive",1,.5f,1.2f,.7f,false,false,false,false,-1},
  {"additive-only-transparent",0,0,2.f,.5f,false,false,true,false,-1},
  {"additive-hdri",2,.6f,1.5f,.4f,false,true,false,false,-1},
  {"replace-ao-pass",1,.5f,1.f,0,false,false,false,true,-1},
  {"ao-node",0,0,1.f,0,false,false,false,false,0},
  {"ao-node-local-instanced",0,0,1.f,0,false,false,false,false,1},
};
constexpr unsigned case_count=sizeof(cases)/sizeof(cases[0]);
struct Metrics {uint64_t ao_paths=0,ao_blocked=0,shadows=0,nee=0,raytrace=0,ao_terminated=0,stages=0,floats=0,strict=0,repeat_differences=0;double maximum=0,repeat_maximum=0;};
static Shader *diffuse_shader(Scene &scene,float3 color){
  auto g=make_unique<ShaderGraph>();auto d=g->create_node<DiffuseBsdfNode>();d->set_color(color);d->set_roughness(.25f);
  g->connect(d->output("BSDF"),g->output()->input("Surface"));auto s=scene.create_node<Shader>();s->set_graph(std::move(g));s->tag_update(&scene);return s;
}
static Shader *mixed_shader(Scene &scene,bool transparent){
  auto g=make_unique<ShaderGraph>();auto d=g->create_node<DiffuseBsdfNode>();d->set_color(make_float3(.55,.32,.18));d->set_roughness(.31f);
  ShaderNode *other;if(transparent){auto t=g->create_node<TransparentBsdfNode>();t->set_color(make_float3(.8,.9,.7));other=t;}
  else{auto glossy=g->create_node<GlossyBsdfNode>();glossy->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_ID);glossy->set_color(make_float3(.2,.5,.8));glossy->set_roughness(.35f);other=glossy;}
  auto mix=g->create_node<MixClosureNode>();mix->set_fac(transparent?.55f:.4f);g->connect(d->output("BSDF"),mix->input("Closure1"));g->connect(other->output("BSDF"),mix->input("Closure2"));
  g->connect(mix->output("Closure"),g->output()->input("Surface"));auto s=scene.create_node<Shader>();s->set_graph(std::move(g));s->tag_update(&scene);return s;
}
// The AO node's colour output drives a diffuse BSDF and its factor mixes it with
// a glossy one (svm_node_ao: ao and color * ao).
static Shader *ao_node_shader(Scene &scene,bool local){
  auto g=make_unique<ShaderGraph>();auto ao=g->create_node<AmbientOcclusionNode>();ao->set_color(make_float3(.9,.7,.4));ao->set_distance(local?.9f:1.4f);
  ao->set_samples(local?6:4);ao->set_only_local(local);ao->set_inside(local);
  auto d=g->create_node<DiffuseBsdfNode>();d->set_roughness(.2f);g->connect(ao->output("Color"),d->input("Color"));
  auto glossy=g->create_node<GlossyBsdfNode>();glossy->set_distribution(CLOSURE_BSDF_MICROFACET_GGX_ID);glossy->set_color(make_float3(.3,.4,.8));glossy->set_roughness(.3f);
  auto mix=g->create_node<MixClosureNode>();g->connect(ao->output("AO"),mix->input("Fac"));g->connect(glossy->output("BSDF"),mix->input("Closure1"));g->connect(d->output("BSDF"),mix->input("Closure2"));
  g->connect(mix->output("Closure"),g->output()->input("Surface"));auto s=scene.create_node<Shader>();s->set_graph(std::move(g));s->tag_update(&scene);return s;
}
static Object *quad(Scene &scene,Shader *shader,const std::array<float3,4> &v){
  auto mesh=scene.create_node<Mesh>();array<Node*> slots;slots.push_back_slow(shader);mesh->set_used_shaders(slots);mesh->resize_mesh(4,2);
  std::copy(v.begin(),v.end(),mesh->get_position_for_write());const int indices[]={0,1,2,0,2,3};std::copy_n(indices,6,mesh->get_triangles().data());
  std::ranges::fill(mesh->get_smooth(),false);std::ranges::fill(mesh->get_shader(),0);mesh->tag_triangles_modified();auto object=scene.create_node<Object>();object->set_geometry(mesh);return object;
}
// A closed box (12 triangles), used twice as an instanced object for the local AO case.
static Mesh *box_mesh(Scene &scene,Shader *shader){
  auto mesh=scene.create_node<Mesh>();array<Node*> slots;slots.push_back_slow(shader);mesh->set_used_shaders(slots);mesh->resize_mesh(8,12);
  for(int i=0;i<8;i++)mesh->get_position_for_write()[i]=make_float3(i&1?.5f:-.5f,i&2?.5f:-.5f,i&4?.5f:-.5f);
  const int t[]={0,2,1,1,2,3,4,5,6,5,7,6,0,1,4,1,5,4,2,6,3,3,6,7,0,4,2,2,4,6,1,3,5,3,7,5};std::copy_n(t,36,mesh->get_triangles().data());
  std::ranges::fill(mesh->get_smooth(),false);std::ranges::fill(mesh->get_shader(),0);mesh->tag_triangles_modified();return mesh;
}
static void fixture(Scene &scene,const Case &c){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);
  scene.integrator->set_aa_samples(samples);scene.integrator->set_min_bounce(2);scene.integrator->set_max_bounce(4);scene.integrator->set_filter_glossy(0);
  scene.integrator->set_ao_bounces(c.ao_bounces);scene.integrator->set_ao_factor(c.ao_factor);scene.integrator->set_ao_distance(c.ao_distance);scene.integrator->set_ao_additive_factor(c.additive);
  scene.film->set_use_sample_count(false);scene.camera->set_full_width(width);scene.camera->set_full_height(height);scene.camera->set_screen_size(width,height);
  scene.camera->set_fov(.9f);scene.camera->set_matrix(transform_translate(make_float3(0,0,-7)));scene.camera->compute_auto_viewplane();
  if(c.pass){auto pass=scene.create_node<Pass>();pass->set_type(PASS_AO);pass->set_name(ustring("AO"));}
  auto wall=diffuse_shader(scene,make_float3(.6,.62,.65)),floor=mixed_shader(scene,false);
  auto back=quad(scene,wall,{make_float3(-4,-3,3),make_float3(4,-3,3),make_float3(4,3,3),make_float3(-4,3,3)});
  auto ground=quad(scene,floor,{make_float3(-4,-2,-2),make_float3(4,-2,-2),make_float3(4,-2,3),make_float3(-4,-2,3)});
  Shader *center=c.node==0?ao_node_shader(scene,false):diffuse_shader(scene,make_float3(.5,.45,.3));
  auto middle=quad(scene,center,{make_float3(-.8f,-2,0),make_float3(.8f,-2,0),make_float3(.8f,1.1f,0),make_float3(-.8f,1.1f,0)});
  if(c.object_distance){middle->set_ao_distance(.35f);ground->set_ao_distance(1.5f);(void)back;}
  if(c.transparent)quad(scene,mixed_shader(scene,true),{make_float3(-2.9f,.6f,-1.8f),make_float3(2.9f,.6f,-1.8f),make_float3(2.9f,.6f,2.8f),make_float3(-2.9f,.6f,2.8f)});
  if(c.node==1){auto box=box_mesh(scene,ao_node_shader(scene,true));
    for(int k=0;k<2;k++){auto o=scene.create_node<Object>();o->set_geometry(box);
      o->set_tfm(transform_translate(make_float3(k?1.9f:-1.9f,-1.3f,-.6f))*transform_rotate(k?.6f:-.4f,make_float3(0,1,0))*transform_scale(make_float3(1.1f,1.4f,.9f)));}}
  if(c.hdri){auto world=environment_world(scene,EnvironmentWorld::Hdri,0);environment_light(scene,world,true,64);}
  else{auto g=make_unique<ShaderGraph>();auto bg=g->create_node<BackgroundNode>();bg->set_color(make_float3(.3,.35,.42));bg->set_strength(1.f);
    g->connect(bg->output("Background"),g->output()->input("Surface"));auto world=scene.create_node<Shader>();world->set_graph(std::move(g));world->tag_update(&scene);scene.background->set_shader(world);}
  auto eg=make_unique<ShaderGraph>();auto emission=eg->create_node<EmissionNode>();emission->set_color(one_float3());emission->set_strength(1);
  eg->connect(emission->output("Emission"),eg->output()->input("Surface"));auto emitter=scene.create_node<Shader>();emitter->set_graph(std::move(eg));emitter->tag_update(&scene);
  auto point=scene.create_node<PointLight>();point->set_radius(.45f);point->set_is_sphere(true);point->set_use_mis(true);point->set_strength(make_float3(110,90,70));
  array<Node*> slots;slots.push_back_slow(emitter);point->set_used_shaders(slots);auto lamp=scene.create_node<Object>();lamp->set_geometry(point);lamp->set_tfm(transform_translate(make_float3(2.2f,2,-2)));
  for(auto shader:scene.shaders)shader->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
}
// Shadow paths of one surface queue: light NEE (non-constant world), intersect, shade.
static void cpu_shadows(DeviceScene &scene,std::vector<IntegratorStateCPU> &state,const std::vector<int> &ids,std::vector<float> &film,Metrics &m,bool ao){
  std::vector<IntegratorShadowStateCPU> shadow(state.size());std::vector<int> nee,pending;
  for(int i:ids){auto &s=ao?state[i].ao:state[i].shadow;if(!s.shadow_path.queued_kernel)continue;shadow[i]=s;
    if(s.shadow_path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_NEE)nee.push_back(i);else pending.push_back(i);}
  m.nee+=nee.size();if(!nee.empty())original_light_nee(scene,nee,shadow,film);
  for(int i:nee)if(shadow[i].shadow_path.queued_kernel){require(shadow[i].shadow_path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,"Unexpected CPU light NEE successor");pending.push_back(i);}
  if(ao)m.ao_paths+=pending.size();else m.shadows+=pending.size();
  original_shadow(scene,DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,pending,shadow,film);
  // Transparent shadows may need several intersect/shade rounds.
  for(unsigned round=0;round<64;round++){std::vector<int> shade,intersect;
    for(int i:pending){const auto k=shadow[i].shadow_path.queued_kernel;if(k==DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW)shade.push_back(i);else if(k==DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW)intersect.push_back(i);}
    if(round==0&&ao)for(int i:pending)m.ao_blocked+=!shadow[i].shadow_path.queued_kernel;
    if(shade.empty()&&intersect.empty())break;
    require(round<63,"CPU shadow paths did not terminate");
    if(!shade.empty())original_shadow(scene,DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW,shade,shadow,film);
    if(!intersect.empty())original_shadow(scene,DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,intersect,shadow,film);}
  for(int i:ids){auto &s=ao?state[i].ao:state[i].shadow;if(s.shadow_path.queued_kernel)s=shadow[i];}
}
static void cpu_render(DeviceScene &scene,std::vector<float> &film,Metrics &m){
  std::vector<IntegratorStateCPU> state(width*height);
  for(unsigned sample=0;sample<samples;sample++){
    std::fill(state.begin(),state.end(),IntegratorStateCPU{});KernelWorkTile tile{};tile.w=width;tile.h=height;tile.stride=width;tile.num_samples=1;tile.start_sample=sample;
    original_camera(scene,{tile},0,state,film);
    for(unsigned step=0;step<100;step++){
      std::map<DeviceKernel,std::vector<int>> queues;
      for(size_t i=0;i<state.size();i++)if(state[i].path.queued_kernel)queues[DeviceKernel(state[i].path.queued_kernel)].push_back(i);
      if(queues.empty())break;
      require(step<99,"CPU AO paths did not terminate");
      for(auto &[kernel,ids]:queues){
        if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST){const auto before=ids;original_closest(scene,ids,state,film);
          for(int i:before)m.ao_terminated+=!state[i].path.queued_kernel&&scene.data.integrator.ao_bounces;}
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND)original_background(scene,ids,state,film);
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD)original_light_forward(scene,ids,state,film);
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE||kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE){
          for(int i:ids){state[i].shadow.shadow_path.queued_kernel=0;state[i].ao.shadow_path.queued_kernel=0;}
          if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE)original_surface(scene,ids,state,film);
          else{m.raytrace+=ids.size();original_surface_raytrace(scene,ids,state,film);}
          cpu_shadows(scene,state,ids,film,m,false);cpu_shadows(scene,state,ids,film,m,true);
        }else throw std::runtime_error("Unexpected CPU AO kernel "+std::string(device_kernel_as_string(kernel)));
      }
    }
  }
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  reference_policy::use_reference_oracle_arithmetic();
  require(argc>=2,"Usage: ao_render_gate OUTPUT_DIRECTORY [CASE...]");
  const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
  std::vector<unsigned> selected;for(int a=2;a<argc;a++)selected.push_back(unsigned(std::stoul(argv[a])));
  if(selected.empty())for(unsigned i=0;i<case_count;i++)selected.push_back(i);
  auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");Stats stats;Profiler profiler;
  auto owner=Device::create(infos[0],stats,profiler,true);auto &device=require_webgpu_device(owner.get(),"ao_render_gate device creation");Metrics total;
  std::ostringstream case_reports;
  for(unsigned image:selected){
    require(image<case_count,"Unknown AO case");const Case &c=cases[image];
    std::cerr<<"ao case="<<image<<" "<<c.name<<'\n';auto folder=out/("case-"+std::to_string(image));std::filesystem::create_directories(folder);const auto prefix=(folder/"run").string();Metrics metrics;
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=false;params.use_texture_cache=false;
    Scene scene(params,&device);fixture(scene,c);Progress progress;scene.device_update(&device,progress);healthy(device);require(!progress.get_error(),"AO scene preparation failed");
    const auto &data=scene.dscene.data;const unsigned stride=unsigned(data.film.pass_stride);
    std::cerr<<"native ao_bounces="<<data.integrator.ao_bounces<<" factor="<<data.integrator.ao_bounces_factor<<" distance="<<data.integrator.ao_bounces_distance<<" additive="<<data.integrator.ao_additive_factor
             <<" features=0x"<<std::hex<<data.kernel_features<<std::dec<<" stride="<<stride<<" pass_ao="<<data.film.pass_ao<<" transparent="<<data.integrator.transparent_shadows<<"\n";
    require(bool(data.kernel_features&KERNEL_FEATURE_AO_ADDITIVE)==(c.additive!=0)&&bool(data.kernel_features&KERNEL_FEATURE_AO_PASS)==c.pass,"Unexpected native AO features");
    require(data.integrator.ao_bounces==(c.ao_factor!=0?c.ao_bounces:0),"Unexpected native AO bounces");
    require(bool(data.kernel_features&KERNEL_FEATURE_NODE_RAYTRACE)==(c.node>=0)&&bool(data.integrator.transparent_shadows)==c.transparent,"Unexpected native AO node or transparency configuration");
    require(c.pass?stride==7&&data.film.pass_ao==4:stride==4,"Unexpected AO film layout");
    if(c.object_distance){unsigned set=0;for(size_t i=0;i<scene.dscene.objects.size();i++)set+=scene.dscene.objects[i].ao_distance!=0;require(set==2,"Per-object AO distance not uploaded");}
    const std::string key="case-"+std::to_string(image);
    const bool exchange_only=reference_policy::exchange_scene(scene.dscene,key,{});
    if(exchange_only){std::vector<float> local(width*height*stride);cpu_render(scene.dscene,local,metrics);
      RenderCapture::file(prefix+".cpu.film.bin",local.data(),local.size()*4);reference_policy::record_local(key+".film",local);
      reference_policy::resolve(key+".film",local,(out/"reference-diagnostic.jsonl").string());continue;}
    bool cancelled=false;Scheduler scheduler(&device,scene.film,&scene.dscene,&cancelled);
    scheduler.alloc_work_memory();scheduler.init_execution();healthy(device);auto buffers=scheduler.get_render_buffers();BufferParams bp;bp.width=width;bp.height=height;bp.pass_stride=stride;buffers->reset(bp);
    std::vector<float> local(width*height*stride),first;cpu_render(scene.dscene,local,metrics);reference_policy::record_local(key+".film",local);
    RenderCapture::file(prefix+".cpu.film.bin",local.data(),local.size()*4);
    const auto reference=reference_policy::resolve(key+".film",local,(out/"reference-diagnostic.jsonl").string());
    for(unsigned repeat=0;repeat<2;repeat++){
      buffers->zero();std::unique_ptr<RenderCapture> capture;if(repeat==1)capture=std::make_unique<RenderCapture>(device,prefix);
      for(unsigned sample=0;sample<samples;sample++){
        scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"AO reset failed");KernelWorkTile tile{};tile.w=width;tile.h=height;tile.stride=width;tile.num_samples=1;tile.start_sample=sample;
        scheduler.enqueue_work_tiles(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,&tile,1,0,0);require(scheduler.update_queue_counter_and_cache(),"AO camera failed");
        for(unsigned step=0;step<300;step++){const auto kernel=scheduler.get_most_queued_kernel();if(kernel==DEVICE_KERNEL_NUM)break;require(step<299,"GPU AO paths did not terminate");
          require(scheduler.enqueue_path_iteration(),"Native AO scheduler stopped with pending paths");require(scheduler.update_queue_counter_and_cache(),"AO stage failed: "+device.error_message());metrics.stages++;}
      }
      if(capture)capture->finish(prefix,buffers->buffer.device_pointer,buffers->buffer.memory_size(),uintptr_t(device.state.path.queued_kernel),scheduler.capacity(),width,height,samples,stride);
      buffers->buffer.copy_from_device();healthy(device);std::vector<float> actual(buffers->buffer.data(),buffers->buffer.data()+buffers->buffer.size());
      RenderCapture::file(prefix+(repeat?".repeat.film.bin":".gpu.film.bin"),actual.data(),actual.size()*4);
      for(size_t i=0;i<actual.size();i++){double error=std::abs(double(actual[i])-reference[i]);metrics.maximum=std::max(metrics.maximum,error);metrics.floats++;
        metrics.strict+=error>1e-7+1e-7*std::abs(double(reference[i]));
        require(std::isfinite(actual[i])&&std::isfinite(reference[i])&&error<=5e-5+2e-4*std::abs(double(reference[i])),"AO film mismatch case="+std::to_string(image)+" component="+std::to_string(i)+" pixel="+std::to_string(i/stride)+" offset="+std::to_string(i%stride)+" actual="+std::to_string(actual[i])+" expected="+std::to_string(reference[i]));
        if(repeat){metrics.repeat_differences+=std::memcmp(&actual[i],&first[i],4)!=0;metrics.repeat_maximum=std::max(metrics.repeat_maximum,std::abs(double(actual[i])-first[i]));}}
      if(!repeat)first=std::move(actual);
    }
    // Coverage: AO shadow rays (both outcomes), fast-GI terminations, raytrace queue, the AO pass.
    if(c.additive!=0||c.pass)require(metrics.ao_paths>100&&metrics.ao_blocked>10&&metrics.ao_blocked<metrics.ao_paths,"Missing AO shadow ray coverage");
    if(c.ao_factor!=0)require(metrics.ao_terminated>10,"Missing fast GI AO termination coverage");
    if(c.node>=0)require(metrics.raytrace>100,"Missing raytrace surface queue coverage");
    if(c.pass){double sum=0;for(size_t i=4;i<first.size();i+=stride)sum+=first[i];require(sum>1,"Empty AO pass");}
    auto report=[&](std::ostream &stream){stream<<std::setprecision(17)<<"{\"passed\":true,\"case\":"<<image<<",\"name\":\""<<c.name<<"\",\"aoShadowPaths\":"<<metrics.ao_paths<<",\"aoBlocked\":"<<metrics.ao_blocked
      <<",\"aoTerminations\":"<<metrics.ao_terminated<<",\"raytracePaths\":"<<metrics.raytrace<<",\"lightShadows\":"<<metrics.shadows<<",\"lightNeePaths\":"<<metrics.nee<<",\"stages\":"<<metrics.stages
      <<",\"filmChecks\":"<<metrics.floats<<",\"strictDifferences\":"<<metrics.strict<<",\"maximumAbsoluteError\":"<<metrics.maximum<<",\"repeatBitwiseDifferences\":"<<metrics.repeat_differences<<",\"repeatMaximumError\":"<<metrics.repeat_maximum<<"}";};
    std::ofstream details(folder/"result.json");report(details);details<<'\n';report(std::cerr);std::cerr<<'\n';if(case_reports.tellp()>0)case_reports<<',';report(case_reports);
    total.floats+=metrics.floats;total.strict+=metrics.strict;total.maximum=std::max(total.maximum,metrics.maximum);total.repeat_differences+=metrics.repeat_differences;
    total.ao_paths+=metrics.ao_paths;total.raytrace+=metrics.raytrace;total.ao_terminated+=metrics.ao_terminated;
  }
  require(stats.mem_used==0,"AO test leaked native allocations");std::ofstream(out/"library.txt")<<device.gpu.library_path;std::ofstream(out/"adapter.txt")<<device.gpu.adapter_name;
  const bool exchange_only=reference_policy::prepare_only()||reference_policy::reference_only();
  if(exchange_only)std::cout<<"{\"passed\":true,\"sceneExchangeOnly\":\""<<(reference_policy::prepare_only()?"dump":"reference")<<"\",\"cases\":"<<selected.size()<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
  else std::cout<<std::setprecision(17)<<"{\"passed\":true,\"nativeSceneAndScheduler\":true,\"independentOriginalCpuImage\":true,\"cases\":"<<selected.size()<<",\"aoShadowPaths\":"<<total.ao_paths
    <<",\"aoTerminations\":"<<total.ao_terminated<<",\"raytracePaths\":"<<total.raytrace<<",\"filmChecks\":"<<total.floats<<",\"strictDifferences\":"<<total.strict<<",\"maximumAbsoluteError\":"<<total.maximum
    <<",\"repeatBitwiseDifferences\":"<<total.repeat_differences<<",\"caseReports\":["<<case_reports.str()<<"],\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<",\"pipelineReport\":"<<ccl::pipeline_process_report_json()<<"}\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
