// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Principled Hair, Huang model, image gate: hair_huang_fixture.h's materials on the curve fixture (ribbons, thick curves,
// thick linear curves, baked and instanced) with the same material on the triangle floor, under a point lamp and a world,
// rendered by the original GPU scheduler on the WebGPU device against an independent loop of the unmodified CPU kernels on
// the same prepared scene. Device-scene flow and replay capture as curve_render_gate.cpp.
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
#include "hair_huang_fixture.h"
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
struct Case {const char *name;std::vector<CurveShapeType> shapes;unsigned material;};
static const std::vector<Case> cases={{"ribbon-default",{CURVE_RIBBON},0},{"thick-default",{CURVE_THICK},0},{"linear-circular",{CURVE_THICK_LINEAR},1},{"mixed-reflectance",{CURVE_RIBBON,CURVE_THICK,CURVE_THICK_LINEAR},2},
  {"thick-smooth",{CURVE_THICK},3},{"thick-ellipse-wide",{CURVE_THICK},4},{"ribbon-ellipse-narrow",{CURVE_RIBBON},5},{"thick-lobes",{CURVE_THICK},9},{"thick-random",{CURVE_THICK},10},{"thick-mix-diffuse",{CURVE_THICK},12},{"thick-mix-two-hairs",{CURVE_THICK},13}};
struct Metrics {uint64_t curve_hits=0,floats=0,strict=0,stages=0,shadows=0,repeat_differences=0;double maximum=0,repeat_maximum=0;};
static void fixture(Scene &scene,const Case &c){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);scene.integrator->set_aa_samples(samples);scene.integrator->set_seed(17);
  scene.integrator->set_sampling_pattern(SAMPLING_PATTERN_TABULATED_SOBOL);scene.integrator->set_scrambling_distance(1.0f);scene.integrator->set_max_bounce(3);scene.integrator->set_min_bounce(2);scene.integrator->set_filter_glossy(0.f);
  scene.film->set_use_sample_count(false);scene.camera->set_full_width(width);scene.camera->set_full_height(height);scene.camera->set_screen_size(width,height);scene.camera->set_fov(.9f);
  scene.camera->set_matrix(transform_translate(make_float3(0,0,c.shapes.size()>1?13.f:9.f))*transform_rotate(float(M_PI),make_float3(0,1,0)));scene.camera->compute_auto_viewplane();
  Shader *shader=huang_shader(scene,huang_materials()[c.material]);
  curve_fixture(scene,shader,c.shapes,shader);
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
  require(argc>=2,"Usage: hair_huang_render_gate OUTPUT_DIRECTORY [CASE...]");
  const std::filesystem::path out=argv[1];std::filesystem::create_directories(out);
  std::vector<unsigned> selected;for(int a=2;a<argc;a++)selected.push_back(unsigned(std::stoul(argv[a])));
  if(selected.empty())for(unsigned i=0;i<cases.size();i++)selected.push_back(i);
  auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");Stats stats;Profiler profiler;
  auto owner=Device::create(infos[0],stats,profiler,true);auto &device=require_webgpu_device(owner.get(),"hair_huang_render_gate device creation");Metrics total;
  std::ostringstream reports;
  for(unsigned image:selected){
    require(image<cases.size(),"Unknown hair case");const Case &c=cases[image];
    std::cerr<<"hair case="<<image<<" "<<c.name<<'\n';auto folder=out/("case-"+std::to_string(image));std::filesystem::create_directories(folder);const auto prefix=(folder/"run").string();Metrics metrics;
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=true;params.use_texture_cache=false;
    Scene scene(params,&device);fixture(scene,c);Progress progress;scene.device_update(&device,progress);healthy(device);require(!progress.get_error(),"Hair scene preparation failed");
    const auto &data=scene.dscene.data;const unsigned stride=unsigned(data.film.pass_stride);require(data.bvh.have_curves&&!data.integrator.transparent_shadows,"Unexpected curve scene configuration");
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
    require(metrics.curve_hits>300&&metrics.shadows>100,"Missing hair image coverage");
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
