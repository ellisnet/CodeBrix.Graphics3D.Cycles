// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "wavefront_test.h"
#include "closest_reference.h"
#include "background_reference.h"
#include "light_reference.h"
#include "scene/scene.h"
#include "scene/background.h"
#include "scene/camera.h"
#include "scene/integrator.h"
#include "scene/light.h"
#include "scene/object.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/progress.h"
#include "util/task.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
using namespace ccl;
using namespace wavefront_test;
struct Metrics {uint64_t cases=0,paths=0,lamp_hits=0,continued=0,terminated=0,exact=0,numeric=0,film=0,strict=0;double maximum=0;};
static void numeric(float actual,float expected,Metrics &m,const std::string &where){
  const double error=std::abs(double(actual)-expected);m.numeric++;m.maximum=std::max(m.maximum,error);m.strict+=error>1e-7+1e-7*std::abs(double(expected));
  require(std::isfinite(actual)&&std::isfinite(expected)&&error<=5e-5+2e-4*std::abs(double(expected)),where+" got="+std::to_string(actual)+" expected="+std::to_string(expected));
}
static void fixture(Scene &scene){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);scene.film->set_use_sample_count(false);
  scene.camera->set_full_width(64);scene.camera->set_full_height(1);scene.camera->set_screen_size(64,1);scene.camera->compute_auto_viewplane();
  auto world_graph=make_unique<ShaderGraph>();auto bg=world_graph->create_node<BackgroundNode>();bg->set_color(zero_float3());world_graph->connect(bg->output("Background"),world_graph->output()->input("Surface"));
  auto world=scene.create_node<Shader>();world->set_graph(std::move(world_graph));world->tag_update(&scene);scene.background->set_shader(world);
  auto graph=make_unique<ShaderGraph>();auto emission=graph->create_node<EmissionNode>();emission->set_color(make_float3(.8,.5,.25));emission->set_strength(1);
  graph->connect(emission->output("Emission"),graph->output()->input("Surface"));auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));shader->tag_update(&scene);
  for(int i=0;i<4;i++){
#ifdef CODEBRIX_BROAD_LIGHT_GATE
    Light *light;if(i<2){auto spot=scene.create_node<SpotLight>();spot->set_radius(i==0?1.25f:.8f);spot->set_is_sphere(i==0);spot->set_angle(.9f);spot->set_smooth(.2f);light=spot;}
    else{auto area=scene.create_node<AreaLight>();area->set_sizeu(1.5f);area->set_sizev(i==2?.8f:1.5f);area->set_ellipse(i==3);area->set_spread(i==2?M_PI_F:.8f);area->set_normalize(i==3);light=area;}
#else
    Light *light;if(i<2){auto point=scene.create_node<PointLight>();point->set_radius(i==0?1.25f:.8f);point->set_is_sphere(i==0);light=point;}
    else{auto sun=scene.create_node<SunLight>();sun->set_angle(i==2?.6f:0.f);sun->set_normalize(false);light=sun;}
#endif
    light->set_use_mis(true);light->set_strength(make_float3(2,3,4));array<Node*> slots;slots.push_back_slow(shader);light->set_used_shaders(slots);
    auto object=scene.create_node<Object>();object->set_geometry(light);object->set_tfm(transform_translate(make_float3(i*3.f,0,4)));
  }
  for(auto shader:scene.shaders)shader->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
}
static void film_layout(KernelData &data){
  data.kernel_features|=KERNEL_FEATURE_LIGHT_PASSES;auto &f=data.film;f.pass_stride=48;f.pass_combined=0;f.pass_emission=4;f.pass_adaptive_aux_buffer=7;
  f.pass_diffuse_direct=11;f.pass_glossy_direct=14;f.pass_transmission_direct=17;f.pass_diffuse_indirect=20;f.pass_glossy_indirect=23;f.pass_transmission_indirect=26;
  f.pass_lightgroup=29;f.pass_background=38;f.pass_flag=0;f.light_pass_flag=PASSMASK(COMBINED)|PASSMASK(EMISSION)|PASSMASK(BACKGROUND);
  data.background.use_mis=0;data.background.lightgroup=LIGHTGROUP_NONE;data.integrator.transparent_max_bounce=4;
}
static void run_stage(WebGPUDevice &d,Scheduler &scheduler,DeviceScene &scene,const std::vector<Field> &layout,
                      DeviceKernel kernel,std::vector<IntegratorStateCPU> before,const std::vector<int> &ids,
                      device_vector<float> &film,Metrics &m,bool direct){
  scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"State reset failed");
  std::vector<float> expected_film(film.size(),.125f);std::copy(expected_film.begin(),expected_film.end(),film.data());film.copy_to_device();
  upload(d,layout,before);IntegratorQueueCounter counters{};counters.num_queued[kernel]=ids.size();d.upload(uintptr_t(d.state.queue_counter),&counters,sizeof(counters));
  auto expected=before;
  if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST)original_closest(scene,ids,expected,expected_film);
  else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD)original_light_forward(scene,ids,expected,expected_film);
  else original_background(scene,ids,expected,expected_film);
  device_vector<int> selected(&d,"mis_state_indices",MEM_READ_WRITE);selected.alloc(ids.size());std::copy(ids.begin(),ids.end(),selected.data());selected.copy_to_device();
  device_ptr indices=direct?0:selected.device_pointer;int count=ids.size();DeviceKernelArguments args;args.add(&indices);args.add(&film.device_pointer);args.add(&count);auto queue=d.gpu_queue_create();
  require(queue->enqueue(kernel,count,args)&&queue->synchronize(),"Light state dispatch failed: "+d.error_message());healthy(d);
  const auto actual=download(d,layout,before.size());
  for(const auto &field:layout)for(size_t i=0;i<actual.size();i++){
    const auto *a=reinterpret_cast<const char*>(&actual[i])+field.offset;const auto *e=reinterpret_cast<const char*>(&expected[i])+field.offset;
    const bool varying=field.name=="isect.t"||field.name=="isect.u"||field.name=="isect.v"||field.name=="ray.tmin";
    if(varying){float av,ev;std::memcpy(&av,a,4);std::memcpy(&ev,e,4);numeric(av,ev,m,"State "+field.name+" index="+std::to_string(i));}
    else{m.exact++;require(!std::memcmp(a,e,field.stride),"State differs "+field.name+" index="+std::to_string(i));}
  }
  film.copy_from_device();healthy(d);for(size_t i=0;i<film.size();i++){numeric(film[i],expected_film[i],m,"Film "+std::to_string(i));m.film++;}
  IntegratorQueueCounter observed{};d.download(uintptr_t(d.state.queue_counter),&observed,sizeof(observed));
  std::array<int,DEVICE_GPU_KERNEL_INTEGRATOR_NUM> wanted{};for(const auto &s:expected)if(s.path.queued_kernel)wanted.at(s.path.queued_kernel)++;
  for(size_t k=0;k<wanted.size();k++)require(observed.num_queued[k]==wanted[k],"Light state queue mismatch");
  require(!observed.cache_miss,"Unexpected light cache miss");
  for(int i:ids){m.lamp_hits+=expected[i].path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD;
    if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD){m.continued+=expected[i].path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST;m.terminated+=expected[i].path.queued_kernel==0;}}
  m.paths+=ids.size();m.cases++;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2,"Usage: light_state_gate OUTPUT_PREFIX");auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");
  Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=require_webgpu_device(owner.get(),"light_state_gate device creation");Metrics metrics;
  {
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.use_texture_cache=false;Scene scene(params,&d);fixture(scene);Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Light state scene preparation failed");
    auto &data=scene.dscene.data;film_layout(data);require(scene.dscene.lights.size()==4,"Expected two points and two suns");
    for(size_t i=0;i<scene.dscene.lights.size();i++)scene.dscene.objects[scene.dscene.lights[i].object_id].lightgroup=i%3;
    scene.dscene.objects.copy_to_device();d.const_copy_to("data",&data,sizeof(data));const auto original=data;
    std::vector<KernelLight> lights(scene.dscene.lights.data(),scene.dscene.lights.data()+scene.dscene.lights.size());
    bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);const auto layout=fields(d);
    device_vector<float> film(&d,"mis_state_film",MEM_READ_WRITE);film.alloc(64*48);
    for(int mode=0;mode<20;mode++){
      data=original;std::copy(lights.begin(),lights.end(),scene.dscene.lights.data());
      if(mode==1){for(size_t lamp=0;lamp<scene.dscene.lights.size();lamp++)scene.dscene.lights[lamp].shader_id|=SHADER_EXCLUDE_CAMERA;}
      if(mode==2){for(size_t lamp=0;lamp<scene.dscene.lights.size();lamp++)scene.dscene.lights[lamp].shader_id|=SHADER_EXCLUDE_DIFFUSE;}
      if(mode==3){for(size_t lamp=0;lamp<scene.dscene.lights.size();lamp++)scene.dscene.lights[lamp].shader_id|=SHADER_EXCLUDE_GLOSSY;}
      if(mode==4){for(size_t lamp=0;lamp<scene.dscene.lights.size();lamp++)scene.dscene.lights[lamp].shader_id|=SHADER_EXCLUDE_TRANSMIT;}
      if(mode==5){data.integrator.filter_closures|=FILTER_CLOSURE_DIRECT_LIGHT;}
      if(mode==6){data.integrator.sample_clamp_direct=.07f;data.integrator.sample_clamp_indirect=.03f;}
      if(mode==7){data.film.light_pass_flag=0;}
      if(mode==8){data.film.pass_adaptive_aux_buffer=PASS_UNUSED;data.film.pass_lightgroup=PASS_UNUSED;}
      if(mode==9){for(size_t lamp=0;lamp<scene.dscene.lights.size();lamp++)scene.dscene.lights[lamp].shader_id&=~SHADER_USE_MIS;}
      if(mode==10){data.integrator.distribution_pdf_lights=.0001f;}
      if(mode==11){data.integrator.distribution_pdf_lights=.95f;scene.dscene.lights[3].shader_id|=SHADER_USE_MIS;}
      // A light's cast_shadow off and its max_bounces below the path bounce: both act on light
      // sampling only (shadow ray tmin, light_select_reached_max_bounces), so the closest, forward
      // and distant (background) stages must be unchanged by them.
      if(mode==18){for(size_t lamp=0;lamp<scene.dscene.lights.size();lamp++)scene.dscene.lights[lamp].shader_id&=~SHADER_CAST_SHADOW;}
      if(mode==19){for(size_t lamp=0;lamp<scene.dscene.lights.size();lamp++)scene.dscene.lights[lamp].max_bounces=lamp%2?0.f:1.f;}
      scene.dscene.lights.copy_to_device();d.const_copy_to("data",&data,sizeof(data));
      const bool direct=mode>=12&&mode<18;const int count=direct?std::array{1,63,64,65,193,256}[mode-12]:193;
      std::vector<int> ids;std::vector<IntegratorStateCPU> initial(scheduler.capacity());
      for(int j=0;j<count;j++){
        const int i=direct?j:1+j*3;ids.push_back(i);auto &s=initial[i];const int lamp=j%
#ifdef CODEBRIX_BROAD_LIGHT_GATE
        4;
#else
        2;
#endif
const auto &light=lights[lamp];
        s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST;s.path.render_pixel_index=j%64;s.path.sample=j/16;s.path.bounce=1+j%3;
        if(mode==5)s.path.bounce=1;
        s.path.transparent_bounce=j%4;s.path.throughput=make_float3(.4,.6,.2);s.path.continuation_probability=1;s.path.mis_ray_pdf=.03f+.15f*(j%5);
        s.path.flag=j%4==0?PATH_RAY_MIS_SKIP:PATH_RAY_SURFACE_PASS;s.path.flag|=PATH_RAY_REFLECT;
        if(j%5==0)s.path.flag|=PATH_RAY_MIS_HAD_TRANSMISSION;
        s.path.visibility=std::array{PATH_RAY_VISIBILITY_CAMERA,PATH_RAY_VISIBILITY_DIFFUSE,PATH_RAY_VISIBILITY_GLOSSY,PATH_RAY_VISIBILITY_TRANSMIT}[j%4];
        s.path.pass_diffuse_weight=make_float3(.3,.2,.1);s.path.pass_glossy_weight=make_float3(.1,.2,.3);s.path.mis_origin_n=make_float3(0,0,1);
        s.ray.P=light.co+make_float3(.02f*(j%7),.01f*(j%3),j%3==0?-.25f:-3.f);s.ray.D=normalize(light.co-s.ray.P);s.ray.tmax=100;s.ray.time=.5;
        s.isect.prim=PRIM_NONE;s.isect.object=OBJECT_NONE;s.isect.type=PRIMITIVE_NONE;
        if(!direct&&j%11==0){s.isect.prim=lamp;s.isect.object=light.object_id;s.isect.type=PRIMITIVE_LAMP;}
        if(!direct&&j%13==0)s.ray.D=-s.ray.D;
      }
      std::cerr<<"light state mode="<<mode<<" closest\n";run_stage(d,scheduler,scene.dscene,layout,DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST,initial,ids,film,metrics,direct);
      // Forward state inputs are derived solely by the independent original closest kernel.
      std::vector<float> unused(film.size());original_closest(scene.dscene,ids,initial,unused);std::vector<int> forward;
      for(int i:ids)if(initial[i].path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD)forward.push_back(i);else initial[i].path.queued_kernel=0;
      if(!forward.empty()){std::cerr<<"light state mode="<<mode<<" forward\n";run_stage(d,scheduler,scene.dscene,layout,DEVICE_KERNEL_INTEGRATOR_SHADE_LIGHT_FORWARD,initial,forward,film,metrics,direct&&forward.size()==ids.size());}
      std::fill(initial.begin(),initial.end(),IntegratorStateCPU{});
      for(int j=0;j<count;j++){
        const int i=ids[j];auto &s=initial[i];s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND;s.path.render_pixel_index=j%64;s.path.sample=j/16;s.path.bounce=1+j%3;
        s.path.throughput=make_float3(.4,.6,.2);s.path.mis_ray_pdf=.02f+.13f*(j%5);s.path.flag=j%4==0?PATH_RAY_MIS_SKIP:PATH_RAY_SURFACE_PASS;
        s.path.flag|=PATH_RAY_REFLECT;s.path.visibility=std::array{PATH_RAY_VISIBILITY_CAMERA,PATH_RAY_VISIBILITY_DIFFUSE,PATH_RAY_VISIBILITY_GLOSSY,PATH_RAY_VISIBILITY_TRANSMIT}[j%4];
        s.path.pass_diffuse_weight=make_float3(.3,.2,.1);s.path.pass_glossy_weight=make_float3(.1,.2,.3);
        s.ray.D=normalize(make_float3(.04f*(j%11),0,1));s.ray.tmax=FLT_MAX;s.isect.prim=PRIM_NONE;
      }
      std::cerr<<"light state mode="<<mode<<" sun\n";run_stage(d,scheduler,scene.dscene,layout,DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND,initial,ids,film,metrics,direct);
    }
  }
  require(stats.mem_used==0&&metrics.lamp_hits>100&&metrics.continued>100&&metrics.terminated>10,"Missing light-state coverage or leaked allocations");
  std::ofstream(std::string(argv[1])+".library.txt")<<d.gpu.library_path;
  std::cout<<std::setprecision(17)<<"{\"passed\":true,\"cases\":"<<metrics.cases<<",\"paths\":"<<metrics.paths<<",\"lampHits\":"<<metrics.lamp_hits<<",\"continued\":"<<metrics.continued<<",\"terminated\":"<<metrics.terminated<<",\"exactStateChecks\":"<<metrics.exact<<",\"numericChecks\":"<<metrics.numeric<<",\"filmChecks\":"<<metrics.film<<",\"strictDifferences\":"<<metrics.strict<<",\"maximumAbsoluteError\":"<<metrics.maximum<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
