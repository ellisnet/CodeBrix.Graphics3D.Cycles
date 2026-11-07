// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Point image gate: the point fixture (clouds baked and instanced, over a triangle floor) under a point lamp and a world,
// with a diffuse shader, a shader reading the Point Info node, the Particle Info node and point attributes, emission,
// transparency, motion and curves in the same scene, rendered by the original GPU scheduler on the WebGPU device against
// an independent loop of the unmodified CPU kernels on the same prepared scene. Device-scene flow and replay capture as
// lighting_gate.cpp.
#include "wavefront_test.h"
#include "render_capture.h"
#include "reference_policy.h"
#include "camera_reference.h"
#include "closest_reference.h"
#include "surface_reference.h"
#include "background_reference.h"
#include "light_reference.h"
#include "shadow_reference.h"
#include "curve_fixture.h"
#include "point_fixture.h"
#include "scene/particles.h"
#include "scene/background.h"
#include "scene/camera.h"
#include "scene/film.h"
#include "scene/integrator.h"
#include "scene/light.h"
#include "util/progress.h"
#include "util/task.h"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
using namespace ccl;
using namespace wavefront_test;
constexpr unsigned width=64,height=48,samples=8;
enum Material {DIFFUSE,POINT_INFO,PARTICLE_INFO,EMISSION,TRANSPARENT};
struct Case {const char *name;PointMotion motion;Material material;bool curves;};
static const std::vector<Case> cases={{"diffuse",POINT_STATIC,DIFFUSE,false},{"point-info",POINT_STATIC,POINT_INFO,false},{"particle-info",POINT_STATIC,PARTICLE_INFO,false},{"emission",POINT_STATIC,EMISSION,false},{"transparent",POINT_STATIC,TRANSPARENT,false},
  {"object-motion",POINT_OBJECT_MOTION,DIFFUSE,false},{"deform-3",POINT_DEFORM_3,DIFFUSE,false},{"deform-5-info",POINT_DEFORM_5,POINT_INFO,false},{"transparent-motion",POINT_DEFORM_3,TRANSPARENT,false},{"object-motion-info",POINT_OBJECT_MOTION,POINT_INFO,false},{"with-curves",POINT_STATIC,DIFFUSE,true}};
struct Metrics {uint64_t point_hits=0,floats=0,strict=0,stages=0,shadows=0,repeat_differences=0;double maximum=0,repeat_maximum=0;};
static Shader *graph_shader(Scene &scene,unique_ptr<ShaderGraph> g){auto shader=scene.create_node<Shader>();shader->set_graph(std::move(g));shader->tag_update(&scene);return shader;}
// Position scaled into colour, the radius, and the Random attribute.
static Shader *point_info_shader(Scene &scene){
  auto g=make_unique<ShaderGraph>();auto info=g->create_node<PointInfoNode>();auto combine=g->create_node<CombineColorNode>();auto radius=g->create_node<MathNode>();auto position=g->create_node<SeparateXYZNode>();auto wrap=g->create_node<MathNode>();auto diffuse=g->create_node<DiffuseBsdfNode>();
  radius->set_math_type(NODE_MATH_MULTIPLY);radius->set_value2(5.f);wrap->set_math_type(NODE_MATH_ABSOLUTE);
  g->connect(info->output("Position"),position->input("Vector"));g->connect(position->output("X"),wrap->input("Value1"));g->connect(wrap->output("Value"),combine->input("Red"));g->connect(info->output("Radius"),radius->input("Value1"));g->connect(radius->output("Value"),combine->input("Green"));g->connect(info->output("Random"),combine->input("Blue"));
  g->connect(combine->output("Color"),diffuse->input("Color"));g->connect(diffuse->output("BSDF"),g->output()->input("Surface"));
  return graph_shader(scene,std::move(g));
}
// Every Particle Info output that the Mix of Add nodes folds into the colour.
static Shader *particle_info_shader(Scene &scene){
  auto g=make_unique<ShaderGraph>();auto info=g->create_node<ParticleInfoNode>();auto combine=g->create_node<CombineColorNode>();auto diffuse=g->create_node<DiffuseBsdfNode>();
  auto sum=[&](ShaderOutput *a,ShaderOutput *b){auto math=g->create_node<MathNode>();math->set_math_type(NODE_MATH_ADD);g->connect(a,math->input("Value1"));g->connect(b,math->input("Value2"));return math->output("Value");};
  auto x=[&](ShaderOutput *v){auto s=g->create_node<SeparateXYZNode>();g->connect(v,s->input("Vector"));return s->output("X");};
  ShaderOutput *red=sum(info->output("Random"),info->output("Age"));ShaderOutput *green=sum(info->output("Lifetime"),info->output("Size"));ShaderOutput *blue=sum(sum(x(info->output("Location")),x(info->output("Velocity"))),x(info->output("Angular Velocity")));
  auto scale=g->create_node<MathNode>();scale->set_math_type(NODE_MATH_MULTIPLY);scale->set_value2(.25f);g->connect(info->output("Index"),scale->input("Value1"));
  g->connect(sum(red,scale->output("Value")),combine->input("Red"));g->connect(green,combine->input("Green"));g->connect(blue,combine->input("Blue"));
  g->connect(combine->output("Color"),diffuse->input("Color"));g->connect(diffuse->output("BSDF"),g->output()->input("Surface"));
  return graph_shader(scene,std::move(g));
}
static Shader *emission_shader(Scene &scene){
  auto g=make_unique<ShaderGraph>();auto emission=g->create_node<EmissionNode>();emission->set_color(make_float3(1.f,.6f,.3f));emission->set_strength(3.f);
  g->connect(emission->output("Emission"),g->output()->input("Surface"));return graph_shader(scene,std::move(g));
}
static Shader *transparent_shader(Scene &scene){
  auto g=make_unique<ShaderGraph>();auto bsdf=g->create_node<TransparentBsdfNode>();bsdf->set_color(make_float3(.7f,.8f,.9f));
  g->connect(bsdf->output("BSDF"),g->output()->input("Surface"));return graph_shader(scene,std::move(g));
}
static Shader *diffuse_shader(Scene &scene,float3 color){
  auto g=make_unique<ShaderGraph>();auto bsdf=g->create_node<DiffuseBsdfNode>();bsdf->set_color(color);
  g->connect(bsdf->output("BSDF"),g->output()->input("Surface"));return graph_shader(scene,std::move(g));
}
static void fixture(Scene &scene,const Case &c){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);scene.integrator->set_aa_samples(samples);scene.integrator->set_seed(17);
  scene.integrator->set_sampling_pattern(SAMPLING_PATTERN_TABULATED_SOBOL);scene.integrator->set_scrambling_distance(1.0f);scene.integrator->set_max_bounce(3);scene.integrator->set_min_bounce(2);scene.integrator->set_filter_glossy(0.f);
  scene.film->set_use_sample_count(false);scene.camera->set_full_width(width);scene.camera->set_full_height(height);scene.camera->set_screen_size(width,height);scene.camera->set_fov(.9f);
  scene.camera->set_matrix(transform_translate(make_float3(0,0,13.f))*transform_rotate(float(M_PI),make_float3(0,1,0)));scene.camera->compute_auto_viewplane();
  Shader *shader=c.material==POINT_INFO?point_info_shader(scene):c.material==PARTICLE_INFO?particle_info_shader(scene):c.material==EMISSION?emission_shader(scene):c.material==TRANSPARENT?transparent_shader(scene):scene.default_surface;
  // The node shaders also on the floor: Point Info on a triangle reads zero, Particle Info the dummy particle or the floor's.
  Shader *floor=c.material==POINT_INFO||c.material==PARTICLE_INFO?shader:scene.default_surface;
  auto f=point_fixture(scene,shader,diffuse_shader(scene,make_float3(.2f,.7f,.3f)),floor,192,c.motion);
  if(c.material==POINT_INFO)for(auto &g:f.geometry){auto random=g.cloud->attributes.add(ATTR_STD_POINT_RANDOM)->data_for_write<float>();for(size_t i=0;i<g.points.size();i++)random[i]=std::fmod(float(i)*.618034f,1.f);}
  if(c.material==PARTICLE_INFO){
    auto first=scene.create_node<ParticleSystem>();auto second=scene.create_node<ParticleSystem>();
    for(unsigned i=0;i<3;i++){Particle p;p.index=int(5+i*11);p.age=.2f+.1f*i;p.lifetime=.4f+.05f*i;p.location=make_float3(.1f*i,.2f,.3f);p.rotation=make_float4(0,0,0,1);p.size=.15f+.02f*i;p.velocity=make_float3(.05f*i,.1f,0);p.angular_velocity=make_float3(.03f+.01f*i,0,.2f);
      first->particles.push_back_slow(p);p.index+=1000;second->particles.push_back_slow(p);}
    unsigned n=0;for(Object *o:scene.objects){if(o->get_geometry()&&o->get_geometry()->is_pointcloud()){o->set_particle_system(n%2?second:first);o->set_particle_index(int(n%3));n++;}}
  }
  if(c.curves){auto geometry=new CurveFixtureGeometry();auto hair=curve_fixture_hair(scene,*geometry,CURVE_THICK,scene.default_surface,2026511);auto object=scene.create_node<Object>();object->set_geometry(hair);object->set_tfm(transform_translate(make_float3(0.f,3.5f,1.f)));delete geometry;}
  auto eg=make_unique<ShaderGraph>();auto emission=eg->create_node<EmissionNode>();emission->set_color(one_float3());emission->set_strength(1);
  eg->connect(emission->output("Emission"),eg->output()->input("Surface"));auto emitter=scene.create_node<Shader>();emitter->set_graph(std::move(eg));emitter->tag_update(&scene);
  auto point=scene.create_node<PointLight>();point->set_radius(.6f);point->set_is_sphere(true);point->set_use_mis(true);point->set_strength(make_float3(60,55,50));array<Node*> slots;slots.push_back_slow(emitter);point->set_used_shaders(slots);
  auto lamp=scene.create_node<Object>();lamp->set_geometry(point);lamp->set_tfm(transform_translate(make_float3(3,3,7)));
  auto wg=make_unique<ShaderGraph>();auto bg=wg->create_node<BackgroundNode>();bg->set_color(make_float3(.3f,.35f,.42f));bg->set_strength(1.f);wg->connect(bg->output("Background"),wg->output()->input("Surface"));
  auto world=scene.create_node<Shader>();world->set_graph(std::move(wg));world->tag_update(&scene);scene.background->set_shader(world);
  for(auto s:scene.shaders)s->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
}
static void cpu_shadows(DeviceScene &scene,std::vector<IntegratorStateCPU> &state,const std::vector<int> &ids,std::vector<float> &film,Metrics &m){
  std::vector<IntegratorShadowStateCPU> shadow(state.size());std::vector<int> pending;
  for(int i:ids){if(!state[i].shadow.shadow_path.queued_kernel)continue;shadow[i]=state[i].shadow;pending.push_back(i);}
  m.shadows+=pending.size();original_shadow(scene,DEVICE_KERNEL_INTEGRATOR_INTERSECT_SHADOW,pending,shadow,film);
  std::vector<int> shade;for(int i:pending)if(shadow[i].shadow_path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW)shade.push_back(i);
  if(!shade.empty())original_shadow(scene,DEVICE_KERNEL_INTEGRATOR_SHADE_SHADOW,shade,shadow,film);
  for(int i:ids)if(state[i].shadow.shadow_path.queued_kernel)state[i].shadow=shadow[i];
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
      require(step<99,"CPU point paths did not terminate");
      for(auto &[kernel,ids]:queues){
        if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST){original_closest(scene,ids,state,film);for(int i:ids)m.point_hits+=state[i].isect.prim!=PRIM_NONE&&(state[i].isect.type&PRIMITIVE_POINT);}
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND)original_background(scene,ids,state,film);
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD)original_light_forward(scene,ids,state,film);
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE){
          for(int i:ids)state[i].shadow.shadow_path.queued_kernel=0;
          original_surface(scene,ids,state,film);cpu_shadows(scene,state,ids,film,m);
        }else throw std::runtime_error("Unexpected CPU point kernel "+std::string(device_kernel_as_string(kernel)));
      }
    }
  }
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  reference_policy::use_reference_oracle_arithmetic();
  require(argc>=2,"Usage: point_render_gate OUTPUT_DIRECTORY [CASE...]");
  const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
  std::vector<unsigned> selected;for(int a=2;a<argc;a++)selected.push_back(unsigned(std::stoul(argv[a])));
  if(selected.empty())for(unsigned i=0;i<cases.size();i++)selected.push_back(i);
  auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");Stats stats;Profiler profiler;
  auto owner=Device::create(infos[0],stats,profiler,true);auto &device=require_webgpu_device(owner.get(),"point_render_gate device creation");Metrics total;
  std::ostringstream reports;
  for(unsigned image:selected){
    require(image<cases.size(),"Unknown point case");const Case &c=cases[image];
    std::cerr<<"point case="<<image<<" "<<c.name<<'\n';auto folder=out/("case-"+std::to_string(image));std::filesystem::create_directories(folder);const auto prefix=(folder/"run").string();Metrics metrics;
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=true;params.use_texture_cache=false;
    Scene scene(params,&device);fixture(scene,c);Progress progress;scene.device_update(&device,progress);healthy(device);require(!progress.get_error(),"Point scene preparation failed");
    const auto &data=scene.dscene.data;const unsigned stride=unsigned(data.film.pass_stride);require(data.bvh.have_points&&bool(data.integrator.transparent_shadows)==(c.material==TRANSPARENT),"Unexpected point scene configuration");
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
        scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Point reset failed");KernelWorkTile tile{};tile.w=width;tile.h=height;tile.stride=width;tile.num_samples=1;tile.start_sample=sample;
        scheduler.enqueue_work_tiles(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,&tile,1,0,0);require(scheduler.update_queue_counter_and_cache(),"Point camera failed");
        for(unsigned step=0;step<300;step++){const auto kernel=scheduler.get_most_queued_kernel();if(kernel==DEVICE_KERNEL_NUM)break;require(step<299,"GPU point paths did not terminate");
          require(scheduler.enqueue_path_iteration(),"Native point scheduler stopped with pending paths");require(scheduler.update_queue_counter_and_cache(),"Point stage failed: "+device.error_message());metrics.stages++;}
      }
      if(capture)capture->finish(prefix,buffers->buffer.device_pointer,buffers->buffer.memory_size(),uintptr_t(device.state.path.queued_kernel),scheduler.capacity(),width,height,samples,stride);
      buffers->buffer.copy_from_device();healthy(device);std::vector<float> actual(buffers->buffer.data(),buffers->buffer.data()+buffers->buffer.size());
      RenderCapture::file(prefix+(repeat?".repeat.film.bin":".gpu.film.bin"),actual.data(),actual.size()*4);
      for(size_t i=0;i<actual.size();i++){double error=std::abs(double(actual[i])-reference[i]);metrics.maximum=std::max(metrics.maximum,error);metrics.floats++;
        metrics.strict+=error>1e-7+1e-7*std::abs(double(reference[i]));
        require(std::isfinite(actual[i])&&std::isfinite(reference[i])&&error<=5e-5+2e-4*std::abs(double(reference[i])),"Point film mismatch case="+std::to_string(image)+" component="+std::to_string(i)+" pixel="+std::to_string(i/stride)+" actual="+std::to_string(actual[i])+" expected="+std::to_string(reference[i]));
        if(repeat){metrics.repeat_differences+=std::memcmp(&actual[i],&first[i],4)!=0;metrics.repeat_maximum=std::max(metrics.repeat_maximum,std::abs(double(actual[i])-first[i]));}}
      if(!repeat)first=std::move(actual);
    }
    require(metrics.point_hits>200&&metrics.shadows>100,"Missing point image coverage");
    auto report=[&](std::ostream &stream){stream<<std::setprecision(17)<<"{\"passed\":true,\"case\":"<<image<<",\"name\":\""<<c.name<<"\",\"pointHits\":"<<metrics.point_hits<<",\"lightShadows\":"<<metrics.shadows<<",\"stages\":"<<metrics.stages<<",\"filmChecks\":"<<metrics.floats<<",\"strictDifferences\":"<<metrics.strict<<",\"maximumAbsoluteError\":"<<metrics.maximum<<",\"repeatBitwiseDifferences\":"<<metrics.repeat_differences<<",\"repeatMaximumError\":"<<metrics.repeat_maximum<<"}";};
    std::ofstream details(folder/"result.json");report(details);details<<'\n';report(std::cerr);std::cerr<<'\n';if(reports.tellp()>0)reports<<',';report(reports);
    total.floats+=metrics.floats;total.strict+=metrics.strict;total.maximum=std::max(total.maximum,metrics.maximum);total.repeat_differences+=metrics.repeat_differences;total.shadows+=metrics.shadows;
  }
  require(stats.mem_used==0,"Point render gate leaked native allocations");std::ofstream(out/"library.txt")<<device.gpu.library_path;std::ofstream(out/"adapter.txt")<<device.gpu.adapter_name;
  const bool exchange_only=reference_policy::prepare_only()||reference_policy::reference_only();
  if(exchange_only)std::cout<<"{\"passed\":true,\"sceneExchangeOnly\":\""<<(reference_policy::prepare_only()?"dump":"reference")<<"\",\"cases\":"<<selected.size()<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
  else std::cout<<std::setprecision(17)<<"{\"passed\":true,\"nativeSceneAndScheduler\":true,\"independentOriginalCpuImage\":true,\"cases\":"<<selected.size()<<",\"lightShadows\":"<<total.shadows<<",\"filmChecks\":"<<total.floats<<",\"strictDifferences\":"<<total.strict<<",\"maximumAbsoluteError\":"<<total.maximum
    <<",\"repeatBitwiseDifferences\":"<<total.repeat_differences<<",\"caseReports\":["<<reports.str()<<"],\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<",\"pipelineReport\":"<<ccl::pipeline_process_report_json()<<"}\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
