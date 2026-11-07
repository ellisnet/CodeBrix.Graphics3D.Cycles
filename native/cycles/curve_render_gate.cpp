// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Curve image gate: the curve fixture (ribbons, thick curves, thick linear curves, baked and instanced, over a triangle
// floor) under a point lamp and a world, with a diffuse shader or a shader reading the Hair Info node and curve
// attributes, rendered by the original GPU scheduler on the WebGPU device against an independent loop of the unmodified
// CPU kernels on the same prepared scene. Device-scene flow and replay capture as lighting_gate.cpp.
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
#ifdef CBC_HAIR_RENDER
#include "bsdf_fixture.h"
#endif
constexpr unsigned width=64,height=48,samples=8;
struct Case {const char *name;std::vector<CurveShapeType> shapes;bool info;CurveMotion motion;bool transparent=false;unsigned hair=0;};
static const std::vector<CurveShapeType> all_shapes{CURVE_RIBBON,CURVE_THICK,CURVE_THICK_LINEAR};
#ifdef CBC_HAIR_RENDER
// The hair closures on curves (and on the floor triangles in the mixed cases): hair = 1 + graph of hair_fixture.h.
#define CBC_HAIR_MIXED {CURVE_RIBBON,CURVE_THICK,CURVE_THICK_LINEAR}
static const std::vector<Case> cases={{"reflection-thick",{CURVE_THICK},false,{},false,1},{"transmission-thick",{CURVE_THICK},false,{},false,2},{"reflection-transmission-ribbon",{CURVE_RIBBON},false,{},false,3},
  {"reflection-tangent-linear",{CURVE_THICK_LINEAR},false,{},false,4},{"transmission-rough-ribbon",{CURVE_RIBBON},false,{},false,7},{"reflection-diffuse-mixed",CBC_HAIR_MIXED,false,{},false,8},
  {"chiang-direct-thick",{CURVE_THICK},false,{},false,11},{"chiang-melanin-ribbon",{CURVE_RIBBON},false,{},false,12},{"chiang-absorption-linear",{CURVE_THICK_LINEAR},false,{},false,13},
  {"chiang-melanin-ggx-mixed",CBC_HAIR_MIXED,false,{},false,14},{"chiang-random-thick",{CURVE_THICK},false,{},false,15},{"chiang-smooth-ribbon",{CURVE_RIBBON},false,{},false,16},
  {"chiang-rough-linear",{CURVE_THICK_LINEAR},false,{},false,17},{"chiang-sharp-thick",{CURVE_THICK},false,{},false,18},{"chiang-hair-diffuse-mixed",CBC_HAIR_MIXED,false,{},false,20}};
#else
static const std::vector<Case> cases={{"ribbon",{CURVE_RIBBON},false},{"thick",{CURVE_THICK},false},{"linear",{CURVE_THICK_LINEAR},false},{"mixed",all_shapes,false},
  {"ribbon-info",{CURVE_RIBBON},true},{"thick-info",{CURVE_THICK},true},{"mixed-info",all_shapes,true},
  {"deform-ribbon",{CURVE_RIBBON},false,{3,false,false}},{"deform-thick",{CURVE_THICK},false,{5,true,false}},{"deform-linear",{CURVE_THICK_LINEAR},false,{3,true,false}},
  {"object-mixed",all_shapes,false,{0,false,true}},{"deform-object-info",all_shapes,true,{5,true,true,0b100110u}},
  {"shadow-ribbon",{CURVE_RIBBON},false,{},true},{"shadow-thick",{CURVE_THICK},false,{},true},{"shadow-linear",{CURVE_THICK_LINEAR},false,{},true},
  {"shadow-mixed",all_shapes,false,{},true},{"shadow-motion",all_shapes,false,{3,true,true,0b100110u},true}};
#endif
// A diffuse mix with a Transparent BSDF over a checker of the position: the baked transparency varies along a strand.
[[maybe_unused]] static Shader *transparent_shader(Scene &scene){
  auto g=make_unique<ShaderGraph>();auto transparent=g->create_node<TransparentBsdfNode>();auto geometry=g->create_node<GeometryNode>();auto checker=g->create_node<CheckerTextureNode>();
  checker->set_scale(2.3f);checker->set_color1(make_float3(.8f,.3f,.6f));checker->set_color2(make_float3(.2f,.9f,.4f));
  g->connect(geometry->output("Position"),checker->input("Vector"));g->connect(checker->output("Color"),transparent->input("Color"));
  auto diffuse=g->create_node<DiffuseBsdfNode>();auto mix=g->create_node<MixClosureNode>();mix->set_fac(.65f);
  g->connect(diffuse->output("BSDF"),mix->input("Closure1"));g->connect(transparent->output("BSDF"),mix->input("Closure2"));g->connect(mix->output("Closure"),g->output()->input("Surface"));
  auto shader=scene.create_node<Shader>();shader->set_graph(std::move(g));shader->tag_update(&scene);return shader;
}
struct Metrics {uint64_t curve_hits=0,floats=0,strict=0,stages=0,shadows=0,repeat_differences=0;double maximum=0,repeat_maximum=0;};
[[maybe_unused]] static Shader *info_shader(Scene &scene){
  auto g=make_unique<ShaderGraph>();auto info=g->create_node<HairInfoNode>();auto combine=g->create_node<CombineColorNode>();auto value=g->create_node<MathNode>();auto diffuse=g->create_node<DiffuseBsdfNode>();
  value->set_math_type(NODE_MATH_MULTIPLY);value->set_value2(6.f);
  g->connect(info->output("Thickness"),value->input("Value1"));g->connect(info->output("Intercept"),combine->input("Red"));g->connect(value->output("Value"),combine->input("Green"));g->connect(info->output("Random"),combine->input("Blue"));
  g->connect(combine->output("Color"),diffuse->input("Color"));g->connect(info->output("Tangent Normal"),diffuse->input("Normal"));g->connect(diffuse->output("BSDF"),g->output()->input("Surface"));
  auto shader=scene.create_node<Shader>();shader->set_graph(std::move(g));shader->tag_update(&scene);return shader;
}
#ifdef CBC_HAIR_RENDER
static Shader *hair_shader(Scene &scene,unsigned graph){
  auto g=make_unique<ShaderGraph>();g->connect(hair_bsdf_material(*g,graph),g->output()->input("Surface"));
  auto shader=scene.create_node<Shader>();shader->set_graph(std::move(g));shader->tag_update(&scene);return shader;
}
#endif
static void fixture(Scene &scene,const Case &c){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);scene.integrator->set_aa_samples(samples);scene.integrator->set_seed(17);
  scene.integrator->set_sampling_pattern(SAMPLING_PATTERN_TABULATED_SOBOL);scene.integrator->set_scrambling_distance(1.0f);scene.integrator->set_max_bounce(3);scene.integrator->set_min_bounce(2);scene.integrator->set_filter_glossy(0.f);
  scene.film->set_use_sample_count(false);scene.camera->set_full_width(width);scene.camera->set_full_height(height);scene.camera->set_screen_size(width,height);scene.camera->set_fov(.9f);
  scene.camera->set_matrix(transform_translate(make_float3(0,0,c.shapes.size()>1?13.f:9.f))*transform_rotate(float(M_PI),make_float3(0,1,0)));scene.camera->compute_auto_viewplane();
#ifdef CBC_HAIR_RENDER
  Shader *shader=hair_shader(scene,c.hair-1);
  // The mixed cases shade the floor triangles with the hair closure too.
  auto f=curve_fixture(scene,shader,c.shapes,c.shapes.size()>1?shader:scene.default_surface);
  for(auto &g:f.geometry){auto random=g.hair->attributes.add(ATTR_STD_CURVE_RANDOM)->data_for_write<float>();for(size_t i=0;i<g.first.size();i++)random[i]=std::fmod(float(i)*.618034f,1.f);}
#else
  Shader *shader=c.transparent?transparent_shader(scene):c.info?info_shader(scene):scene.default_surface;
  // Hair Info on a triangle reads as no strand: the mixed case shades its floor with it too.
  auto f=curve_fixture(scene,shader,c.shapes,c.info&&c.shapes.size()>1?shader:scene.default_surface);
#endif
  if(c.motion.steps||c.motion.object){curve_fixture_motion(f,scene,c.motion);scene.integrator->set_motion_blur(true);scene.camera->set_shuttertime(.7f);}
  if(c.info)for(auto &g:f.geometry){auto intercept=g.hair->attributes.add(ATTR_STD_CURVE_INTERCEPT)->data_for_write<float>();auto length=g.hair->attributes.add(ATTR_STD_CURVE_LENGTH)->data_for_write<float>();auto random=g.hair->attributes.add(ATTR_STD_CURVE_RANDOM)->data_for_write<float>();
    for(size_t k=0;k<g.keys.size();k++){intercept[k]=float(k%5)*.2f;}
    for(size_t i=0;i<g.first.size();i++){length[i]=1.f+.1f*float(i);random[i]=std::fmod(float(i)*.618034f,1.f);}}
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
      require(step<99,"CPU curve paths did not terminate");
      for(auto &[kernel,ids]:queues){
        if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST){original_closest(scene,ids,state,film);for(int i:ids)m.curve_hits+=state[i].isect.prim!=PRIM_NONE&&(state[i].isect.type&PRIMITIVE_CURVE)&&!(state[i].isect.type&PRIMITIVE_LAMP);}
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND)original_background(scene,ids,state,film);
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD)original_light_forward(scene,ids,state,film);
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE){
          for(int i:ids)state[i].shadow.shadow_path.queued_kernel=0;
          original_surface(scene,ids,state,film);cpu_shadows(scene,state,ids,film,m);
        }else throw std::runtime_error("Unexpected CPU curve kernel "+std::string(device_kernel_as_string(kernel)));
      }
    }
  }
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  reference_policy::use_reference_oracle_arithmetic();
  require(argc>=2,"Usage: curve_render_gate OUTPUT_DIRECTORY [CASE...]");
  const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
  std::vector<unsigned> selected;for(int a=2;a<argc;a++)selected.push_back(unsigned(std::stoul(argv[a])));
  if(selected.empty())for(unsigned i=0;i<cases.size();i++)selected.push_back(i);
#ifdef CBC_HAIR_RENDER
  // Reproducer of native/DEVICE.md item 77, run only on request: Chiang at Coat 1 (hair_fixture.h graph 20).
  static const Case coat_one={"chiang-coat-one-linear",{CURVE_THICK_LINEAR},false,{},false,21};
  auto case_at=[&](unsigned image)->const Case&{return image==cases.size()?coat_one:cases[image];};
#else
  auto case_at=[&](unsigned image)->const Case&{return cases[image];};
#endif
  auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");Stats stats;Profiler profiler;
  auto owner=Device::create(infos[0],stats,profiler,true);auto &device=require_webgpu_device(owner.get(),"curve_render_gate device creation");Metrics total;
  std::ostringstream reports;
  for(unsigned image:selected){
    #ifdef CBC_HAIR_RENDER
    require(image<=cases.size(),"Unknown curve case");const Case &c=case_at(image);
#else
    require(image<cases.size(),"Unknown curve case");const Case &c=case_at(image);
#endif
    std::cerr<<"curve case="<<image<<" "<<c.name<<'\n';auto folder=out/("case-"+std::to_string(image));std::filesystem::create_directories(folder);const auto prefix=(folder/"run").string();Metrics metrics;
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=true;params.use_texture_cache=false;
    Scene scene(params,&device);fixture(scene,c);Progress progress;scene.device_update(&device,progress);healthy(device);require(!progress.get_error(),"Curve scene preparation failed");
    const auto &data=scene.dscene.data;const unsigned stride=unsigned(data.film.pass_stride);require(data.bvh.have_curves&&bool(data.integrator.transparent_shadows)==c.transparent,"Unexpected curve scene configuration");
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
        scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Curve reset failed");KernelWorkTile tile{};tile.w=width;tile.h=height;tile.stride=width;tile.num_samples=1;tile.start_sample=sample;
        scheduler.enqueue_work_tiles(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,&tile,1,0,0);require(scheduler.update_queue_counter_and_cache(),"Curve camera failed");
        for(unsigned step=0;step<300;step++){const auto kernel=scheduler.get_most_queued_kernel();if(kernel==DEVICE_KERNEL_NUM)break;require(step<299,"GPU curve paths did not terminate");
          require(scheduler.enqueue_path_iteration(),"Native curve scheduler stopped with pending paths");require(scheduler.update_queue_counter_and_cache(),"Curve stage failed: "+device.error_message());metrics.stages++;}
      }
      if(capture)capture->finish(prefix,buffers->buffer.device_pointer,buffers->buffer.memory_size(),uintptr_t(device.state.path.queued_kernel),scheduler.capacity(),width,height,samples,stride);
      buffers->buffer.copy_from_device();healthy(device);std::vector<float> actual(buffers->buffer.data(),buffers->buffer.data()+buffers->buffer.size());
      RenderCapture::file(prefix+(repeat?".repeat.film.bin":".gpu.film.bin"),actual.data(),actual.size()*4);
      for(size_t i=0;i<actual.size();i++){double error=std::abs(double(actual[i])-reference[i]);metrics.maximum=std::max(metrics.maximum,error);metrics.floats++;
        metrics.strict+=error>1e-7+1e-7*std::abs(double(reference[i]));
        require(std::isfinite(actual[i])&&std::isfinite(reference[i])&&error<=5e-5+2e-4*std::abs(double(reference[i])),"Curve film mismatch case="+std::to_string(image)+" component="+std::to_string(i)+" pixel="+std::to_string(i/stride)+" actual="+std::to_string(actual[i])+" expected="+std::to_string(reference[i]));
        if(repeat){metrics.repeat_differences+=std::memcmp(&actual[i],&first[i],4)!=0;metrics.repeat_maximum=std::max(metrics.repeat_maximum,std::abs(double(actual[i])-first[i]));}}
      if(!repeat)first=std::move(actual);
    }
    require(metrics.curve_hits>500&&metrics.shadows>100,"Missing curve image coverage");
    auto report=[&](std::ostream &stream){stream<<std::setprecision(17)<<"{\"passed\":true,\"case\":"<<image<<",\"name\":\""<<c.name<<"\",\"curveHits\":"<<metrics.curve_hits<<",\"lightShadows\":"<<metrics.shadows<<",\"stages\":"<<metrics.stages<<",\"filmChecks\":"<<metrics.floats<<",\"strictDifferences\":"<<metrics.strict<<",\"maximumAbsoluteError\":"<<metrics.maximum<<",\"repeatBitwiseDifferences\":"<<metrics.repeat_differences<<",\"repeatMaximumError\":"<<metrics.repeat_maximum<<"}";};
    std::ofstream details(folder/"result.json");report(details);details<<'\n';report(std::cerr);std::cerr<<'\n';if(reports.tellp()>0)reports<<',';report(reports);
    total.floats+=metrics.floats;total.strict+=metrics.strict;total.maximum=std::max(total.maximum,metrics.maximum);total.repeat_differences+=metrics.repeat_differences;total.shadows+=metrics.shadows;
  }
  require(stats.mem_used==0,"Curve render gate leaked native allocations");std::ofstream(out/"library.txt")<<device.gpu.library_path;std::ofstream(out/"adapter.txt")<<device.gpu.adapter_name;
  const bool exchange_only=reference_policy::prepare_only()||reference_policy::reference_only();
  if(exchange_only)std::cout<<"{\"passed\":true,\"sceneExchangeOnly\":\""<<(reference_policy::prepare_only()?"dump":"reference")<<"\",\"cases\":"<<selected.size()<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
  else std::cout<<std::setprecision(17)<<"{\"passed\":true,\"nativeSceneAndScheduler\":true,\"independentOriginalCpuImage\":true,\"cases\":"<<selected.size()<<",\"lightShadows\":"<<total.shadows<<",\"filmChecks\":"<<total.floats<<",\"strictDifferences\":"<<total.strict<<",\"maximumAbsoluteError\":"<<total.maximum
    <<",\"repeatBitwiseDifferences\":"<<total.repeat_differences<<",\"caseReports\":["<<reports.str()<<"],\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<",\"pipelineReport\":"<<ccl::pipeline_process_report_json()<<"}\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
