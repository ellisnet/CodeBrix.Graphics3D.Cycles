// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "wavefront_test.h"
#include "background_reference.h"
#include "scene/scene.h"
#include "scene/background.h"
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
#include <numeric>
using namespace ccl;
using namespace wavefront_test;
struct Metrics {uint64_t paths=0,state_checks=0,film_checks=0,strict_failures=0;unsigned cases=0;double maximum_error=0;};
static void prepare(Scene &scene){
  scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);
  scene.integrator->set_aa_samples(64);scene.integrator->set_seed(17);scene.film->set_use_sample_count(false);
  scene.camera->set_full_width(64);scene.camera->set_full_height(48);scene.camera->set_screen_size(64,48);scene.camera->set_fov(1);
  scene.camera->compute_auto_viewplane();
  auto graph=make_unique<ShaderGraph>();auto bg=graph->create_node<BackgroundNode>();bg->set_color(make_float3(.25f,.5f,.75f));bg->set_strength(2);
  graph->connect(bg->output("Background"),graph->output()->input("Surface"));auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));
  shader->tag_update(&scene);scene.background->set_shader(shader);
}
static void configure(KernelData &data){
  data.background.use_mis=0;data.background.lightgroup=1;data.integrator.num_lights=0;data.integrator.use_light_mis=0;data.integrator.use_caustics=0;
  data.kernel_features|=KERNEL_FEATURE_LIGHT_PASSES;
  auto &f=data.film;f.pass_stride=48;f.pass_combined=0;f.pass_background=4;f.pass_adaptive_aux_buffer=7;
  f.pass_diffuse_direct=11;f.pass_glossy_direct=14;f.pass_transmission_direct=17;
  f.pass_diffuse_indirect=20;f.pass_glossy_indirect=23;f.pass_transmission_indirect=26;
  f.pass_lightgroup=29;f.pass_depth=38;f.pass_position=39;f.pass_object_id=42;f.pass_material_id=43;
  f.pass_flag=PASSMASK(DEPTH)|PASSMASK(POSITION)|PASSMASK(OBJECT_ID)|PASSMASK(MATERIAL_ID);
  f.light_pass_flag=PASSMASK(COMBINED)|PASSMASK(BACKGROUND);
}
static void compare(WebGPUDevice &d,const std::vector<Field> &layout,const std::vector<IntegratorStateCPU> &expected,
                    device_vector<float> &film,const std::vector<float> &wanted,unsigned paths,Metrics &m){
  auto actual=download(d,layout,expected.size());
  for(auto &f:layout)for(size_t i=0;i<expected.size();i++){
    require(!std::memcmp(reinterpret_cast<const char*>(&expected[i])+f.offset,reinterpret_cast<const char*>(&actual[i])+f.offset,f.stride),"Background changed state "+f.name);m.state_checks++;
  }
  film.copy_from_device();healthy(d);require(film.size()==wanted.size(),"Film size differs");
  for(size_t i=0;i<wanted.size();i++){
    double error=std::abs(double(film[i])-wanted[i]);m.maximum_error=std::max(m.maximum_error,error);m.film_checks++;
    if(error>1e-7+1e-7*std::abs(double(wanted[i])))m.strict_failures++;
    require(std::isfinite(film[i])&&error<=5e-5+2e-4*std::abs(double(wanted[i])),"Background film mismatch at "+std::to_string(i)+" actual="+std::to_string(film[i])+" expected="+std::to_string(wanted[i]));
  }
  IntegratorQueueCounter counter{};d.download(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
  require(!counter.cache_miss,"Background unexpected cache miss");for(int count:counter.num_queued)require(count==0,"Background did not terminate every path");
  m.paths+=paths;m.cases++;
}
static unsigned rejections(const DeviceInfo &info){
  unsigned count=0;
  for(unsigned test=0;test<12;test++){
    Stats stats;Profiler profiler;auto owner=Device::create(info,stats,profiler,true);auto &d=require_webgpu_device(owner.get(),"background_gate device creation");
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.use_texture_cache=false;Scene scene(params,&d);prepare(scene);Progress progress;scene.device_update(&d,progress);healthy(d);
    auto &data=scene.dscene.data;configure(data);d.const_copy_to("data",&data,sizeof(data));
    bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Negative background setup failed");
    auto layout=fields(d);std::vector<IntegratorStateCPU> states(scheduler.capacity());auto &s=states[0];s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND;s.path.throughput=one_float3();s.path.visibility=PATH_RAY_VISIBILITY_CAMERA;s.isect.prim=PRIM_NONE;
    std::string reason="reported invalid data";
    // Background MIS is implemented (environment gate); portals stay refused.
    if(test==0){data.background.portal_weight=1;data.background.use_mis=1;reason="portals";}
    if(test==1){data.integrator.num_lights=1;reason="native light table";}
    if(test==2){data.kernel_features|=KERNEL_FEATURE_VOLUME;data.integrator.volume_ray_marching=1;reason="biased volume ray marching";}
    if(test==3){data.film.pass_flag|=PASSMASK(MOTION);reason="motion pass";}
    if(test==4){data.film.pass_combined=data.film.pass_stride-3;reason="film pass offset";}
    if(test==5)s.path.render_pixel_index=1;
    if(test==6)s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST;
    if(test==7)s.path.flag=PATH_RAY_DENOISING_FEATURES;
    if(test==8){auto shader=data.background.surface_shader&SHADER_MASK;scene.dscene.shaders[shader].flags&=~SD_HAS_CONSTANT_EMISSION;scene.dscene.shaders.copy_to_device();scene.dscene.svm_nodes[shader*4]=NODE_NUM;scene.dscene.svm_nodes.copy_to_device();}
    if(test==9)data.background.surface_shader=SHADER_MASK;
    if(test==10)reason="Incomplete background";
    d.const_copy_to("data",&data,sizeof(data));upload(d,layout,states);healthy(d);
    device_vector<float> film(&d,"negative_background_film",MEM_READ_WRITE);film.alloc(48);std::fill_n(film.data(),48,3.25f);film.copy_to_device();
    device_vector<int> indices(&d,"negative_background_indices",MEM_READ_WRITE);indices.alloc(1);indices[0]=test==11?scheduler.capacity():0;indices.copy_to_device();
    IntegratorQueueCounter counter{};counter.num_queued[s.path.queued_kernel]=1;d.upload(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
    int n=test==10?2:1;DeviceKernelArguments args;args.add(&indices.device_pointer);args.add(&film.device_pointer);args.add(&n);auto queue=d.gpu_queue_create();
    bool accepted=queue->enqueue(DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND,1,args);if(accepted)accepted=queue->synchronize();
    require(!accepted&&d.error_message().find(reason)!=std::string::npos,"Background negative case "+std::to_string(test)+": "+d.error_message());
    std::vector<float> after(48);d.download(film.device_pointer,after.data(),48*4);require(std::all_of(after.begin(),after.end(),[](float v){return v==3.25f;}),"Rejected background changed film");
    IntegratorQueueCounter counters{};d.download(uintptr_t(d.state.queue_counter),&counters,sizeof(counters));require(!std::memcmp(&counter,&counters,sizeof(counter)),"Rejected background changed counters");count++;
  }
  return count;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2,"Usage: background_gate OUTPUT_PREFIX");auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU adapter");
  Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=require_webgpu_device(owner.get(),"background_gate device creation");Metrics m;
  {
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_bvh_unaligned_nodes=false;params.use_texture_cache=false;
    Scene scene(params,&d);prepare(scene);Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Background scene preparation failed");
    auto &data=scene.dscene.data;configure(data);d.const_copy_to("data",&data,sizeof(data));const auto original=data;
    bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);auto layout=fields(d);
    BufferParams bp;bp.width=64;bp.height=48;bp.pass_stride=data.film.pass_stride;auto buffers=scheduler.get_render_buffers();buffers->reset(bp);buffers->zero();
    std::vector<float> wanted(buffers->buffer.size());
    // End-to-end native scene -> original camera/closest/background scheduler -> film.
    for(unsigned sample=0;sample<8;sample++){
      scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Background reset failed");
      KernelWorkTile tile{};tile.w=64;tile.h=48;tile.stride=64;tile.num_samples=1;tile.start_sample=sample;
      scheduler.enqueue_work_tiles(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,&tile,1,0,0);require(scheduler.update_queue_counter_and_cache(),"Background camera failed");
      scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST);require(scheduler.update_queue_counter_and_cache(),"Background closest failed: "+d.error_message());
      auto expected=download(d,layout,scheduler.capacity());std::vector<int> indices;
      for(size_t i=0;i<expected.size();i++)if(expected[i].path.queued_kernel){require(expected[i].path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND,"World ray did not miss");indices.push_back(i);}
      require(indices.size()==3072,"World camera path count differs");original_background(scene.dscene,indices,expected,wanted);
      scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND);require(scheduler.update_queue_counter_and_cache(),"Scheduled background failed: "+d.error_message());
      compare(d,layout,expected,buffers->buffer,wanted,indices.size(),m);
    }
    std::ofstream image(std::string(argv[1])+".ppm",std::ios::binary);image<<"P6\n64 48\n255\n";
    for(unsigned pixel=0;pixel<3072;pixel++)for(unsigned channel=0;channel<3;channel++){
      const float v=buffers->buffer[pixel*48+channel]/8;const unsigned char byte=unsigned(std::lround(std::min(1.f,v)*255));image.write(reinterpret_cast<const char*>(&byte),1);
      require(v==(.5f+.5f*channel),"World image color differs");
    }
    // Noncontiguous paths, indirect/direct pass routing, transparency, AO,
    // visibility, data passes and contention on a single pixel.
    for(unsigned test=0;test<18;test++){
      data=original;data.background.transparent=(test==1||test==2);if(test==2)data.film.light_pass_flag=PASSMASK(COMBINED);
      if(test==3)data.film.light_pass_flag=0;
      if(test==4)data.background.lightgroup=LIGHTGROUP_NONE;
      if(test==5){data.integrator.ao_bounces=1;data.integrator.ao_bounces_factor=.35f;}
      if(test>=6&&test<=9)data.background.surface_shader|=test==6?SHADER_EXCLUDE_CAMERA:test==7?SHADER_EXCLUDE_DIFFUSE:test==8?SHADER_EXCLUDE_GLOSSY:SHADER_EXCLUDE_TRANSMIT;
      if(test==10){data.integrator.sample_clamp_direct=.2f;data.integrator.sample_clamp_indirect=.1f;}
      if(test==11)data.film.pass_adaptive_aux_buffer=PASS_UNUSED;
      if(test==12)data.film.pass_background=PASS_UNUSED;
      d.const_copy_to("data",&data,sizeof(data));
      scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Synthetic background reset failed");
      auto expected=std::vector<IntegratorStateCPU>(scheduler.capacity());std::vector<int> indices;
      const int n=test==17?4096:test==16?193:test==15?65:test==14?64:test==13?63:3072;
      const bool direct=test>=13;for(int i=0;i<n;i++){
        if(!direct&&i%3==0)continue;
        indices.push_back(i);auto &s=expected[i];s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND;
        s.path.render_pixel_index=test==17?0:i%193;s.path.sample=i/193;s.isect.prim=PRIM_NONE;
        s.path.throughput=make_float3(.5f+.01f*(i%7),.75f,.3f);s.path.bounce=i%4;
        s.path.flag=i%2?PATH_RAY_SURFACE_PASS:PATH_RAY_TRANSPARENT_BACKGROUND;
        if(i%7==0)s.path.flag|=PATH_RAY_SINGLE_PASS_DONE;
        if(i%5==0)s.path.flag|=PATH_RAY_REFLECT;
        if(test==1||test==2)s.path.flag=PATH_RAY_TRANSPARENT_BACKGROUND;
        s.path.visibility=(test==7?PATH_RAY_VISIBILITY_DIFFUSE:test==8?PATH_RAY_VISIBILITY_GLOSSY:test==9?PATH_RAY_VISIBILITY_TRANSMIT:PATH_RAY_VISIBILITY_CAMERA);
        s.path.pass_diffuse_weight=make_float3(.2,.1,.3);s.path.pass_glossy_weight=make_float3(.1,.3,.4);
      }
      upload(d,layout,expected);IntegratorQueueCounter counter{};counter.num_queued[DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND]=indices.size();d.upload(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
      scheduler.set_active(n);require(scheduler.update_queue_counter_and_cache(),"Synthetic background counts failed");
      wanted.assign(buffers->buffer.size(),0);for(size_t i=0;i<wanted.size();i++)if(i%48>=38)wanted[i]=.125f;
      std::copy(wanted.begin(),wanted.end(),buffers->buffer.data());buffers->buffer.copy_to_device();
      original_background(scene.dscene,indices,expected,wanted);
      scheduler.enqueue_path_iteration(DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND);require(scheduler.update_queue_counter_and_cache(),"Synthetic background failed: "+d.error_message());
      compare(d,layout,expected,buffers->buffer,wanted,indices.size(),m);
    }
  }
  require(stats.mem_used==0,"Background Cycles allocations leaked");unsigned rejected=rejections(infos[0]);
  std::ofstream(std::string(argv[1])+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(std::string(argv[1])+".library.txt")<<d.gpu.library_path;
  std::cout<<"{\"passed\":true,\"originalCameraClosestBackgroundScheduler\":true,\"cases\":"<<m.cases<<",\"paths\":"<<m.paths
    <<",\"exactStateChecks\":"<<m.state_checks<<",\"filmFloatChecks\":"<<m.film_checks<<",\"strictFloatFailures\":"<<m.strict_failures<<",\"maximumAbsoluteError\":"<<m.maximum_error
    <<",\"invalidCasesRejected\":"<<rejected<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
