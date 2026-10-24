// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "wavefront_test.h"
#include "volume_reference.h"
#include "scene/scene.h"
#include "scene/background.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/camera.h"
#include "scene/integrator.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "util/progress.h"
#include "util/task.h"
#include "util/types_rgbe.h"
#include <algorithm>
#include <bit>
#include <cfloat>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iomanip>
using namespace ccl;
using namespace wavefront_test;
#include "surface_fixture.h"
struct Metrics {uint64_t floats=0,integers=0,strict=0,scatter=0,transmit=0,terminated=0;unsigned cases=0,rejected=0;float maximum_actual=0,maximum_expected=0;double maximum=0,ratio=0;std::string field;};
static void number(float a,float e,const std::string &field,Metrics &m){
  const double error=std::abs(double(a)-e);m.floats++;m.strict+=error>1e-7+1e-7*std::abs(double(e));
  if(error>m.maximum){m.maximum=error;m.maximum_actual=a;m.maximum_expected=e;m.field=field;}m.ratio=std::max(m.ratio,error/(5e-5+2e-4*std::abs(double(e))));
  if(!(std::isfinite(a)&&std::isfinite(e)&&error<=5e-5+2e-4*std::abs(double(e)))){std::cerr<<std::setprecision(10)<<field<<" actual="<<a<<" expected="<<e<<'\n';throw std::runtime_error("Volume practical comparison failed");}
}
static void evaluate(WebGPUDevice &d,Scene &scene,const std::vector<Field> &layout,const std::vector<IntegratorStateCPU>&input,
                     const std::vector<int>&ids,Metrics &m,unsigned test){
  const auto &data=scene.dscene.data;std::vector<float> wanted(512*data.film.pass_stride);
  for(unsigned pixel=0;pixel<512;pixel++){
    auto at=pixel*data.film.pass_stride;
    if(pixel%3){wanted[at+data.film.pass_volume_scatter_denoised]=rgb_to_rgbe(make_float3(.13,.27,.09)).f;
      wanted[at+data.film.pass_volume_transmit_denoised]=rgb_to_rgbe(make_float3(.9,.12,.25)).f;
      wanted[at+data.film.pass_volume_majorant]=.9;wanted[at+data.film.pass_volume_majorant_sample_count]=2;}
  }
  auto expected=input;auto initial=wanted;original_volume(scene.dscene,ids,expected,wanted);
  for(auto i:ids){auto &s=expected[i];if(s.path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE){
      s.path.shader_sort_key=(scene.dscene.tri_shader[s.isect.prim]&SHADER_MASK)+data.max_shaders*(i/d.state.sort_partition_divisor);m.transmit++;}
    else if(s.path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND)m.transmit++;
    else if(s.path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST)m.scatter++;
    else if(!s.path.queued_kernel)m.terminated++;
  }
  upload(d,layout,input);device_vector<float> film(&d,"volume_film",MEM_READ_WRITE);film.alloc(wanted.size());std::copy(initial.begin(),initial.end(),film.data());film.copy_to_device();
  device_vector<int> selected(&d,"volume_indices",MEM_READ_WRITE);selected.alloc(ids.size());std::copy(ids.begin(),ids.end(),selected.data());selected.copy_to_device();
  IntegratorQueueCounter count{};count.num_queued[DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME]=ids.size();d.upload(uintptr_t(d.state.queue_counter),&count,sizeof(count));
  for(auto kernel:{DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE})if(d.state.sort_key_counter[kernel])d.zero(uintptr_t(d.state.sort_key_counter[kernel]),d.remaining(uintptr_t(d.state.sort_key_counter[kernel])));
  auto queue=d.gpu_queue_create();int n=ids.size();DeviceKernelArguments args;args.add(&selected.device_pointer);args.add(&film.device_pointer);args.add(&n);
  require(queue->enqueue(DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME,n+63,args),d.error_message());require(queue->synchronize(),d.error_message());
  const auto actual=download(d,layout,input.size());
  for(const auto &field:layout)for(size_t i=0;i<input.size();i++){
    auto a=reinterpret_cast<const char*>(&actual[i])+field.offset;auto e=reinterpret_cast<const char*>(&expected[i])+field.offset;
    const std::string label="case="+std::to_string(test)+" "+field.name+" path="+std::to_string(i);
    if(field.floating)for(size_t c=0;c<field.stride;c+=4){float x,y;std::memcpy(&x,a+c,4);std::memcpy(&y,e+c,4);number(x,y,label+" component="+std::to_string(c/4),m);}
    else{require(!std::memcmp(a,e,field.stride),label+" integer mismatch");m.integers++;}
  }
  film.copy_from_device();for(size_t i=0;i<wanted.size();i++)number(film[i],wanted[i],"case="+std::to_string(test)+" film="+std::to_string(i),m);
  IntegratorQueueCounter expected_count{};for(int i:ids)if(expected[i].path.queued_kernel)expected_count.num_queued[expected[i].path.queued_kernel]++;
  d.download(uintptr_t(d.state.queue_counter),&count,sizeof(count));require(!std::memcmp(&count,&expected_count,sizeof(count)),"Volume queue counter mismatch");m.cases++;
  std::cerr<<"Passed volume case "<<test<<'\n';
}
// Host capability rejections must leave all uploaded path fields, film and queue
// counters intact. Direct calls retain the device for the next independent case.
static unsigned rejections(WebGPUDevice &d,const std::vector<Field>&layout,const std::vector<IntegratorStateCPU>&states){
  upload(d,layout,states);device_vector<float> film(&d,"volume_rejection_film",MEM_READ_WRITE);film.alloc(d.data.film.pass_stride);std::fill_n(film.data(),film.size(),.125f);film.copy_to_device();
  IntegratorQueueCounter before{};before.num_queued[DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME]=1;d.upload(uintptr_t(d.state.queue_counter),&before,sizeof(before));
  const auto original_data=d.data;const auto original_state=d.state;const auto original_shaders=d.shader_records;
  device_ptr indices=0;int n=1;DeviceKernelArguments args;args.add(&indices);args.add(&film.device_pointer);args.add(&n);
  for(unsigned test=0;test<18;test++){
    std::string reason;int work=1;
    switch(test){
      case 0:d.data.kernel_features&=~KERNEL_FEATURE_VOLUME;reason="without native volume feature";break;
      case 1:d.data.bvh.have_curves=1;reason="static triangle boundaries";break;
      case 2:d.data.bvh.have_motion=1;reason="static triangle boundaries";break;
      case 3:d.data.integrator.volume_ray_marching=1;reason="biased volume ray marching";break;
      case 4:d.data.integrator.use_direct_light=1;reason="direct-light/MIS";break;
      case 5:d.data.integrator.use_light_mis=1;reason="direct-light/MIS";break;
      case 6:d.data.background.use_mis=1;reason="direct-light/MIS";break;
      case 7:d.data.kernel_features|=KERNEL_FEATURE_PATH_GUIDING;reason="optional volume integration";break;
      case 8:d.data.kernel_features|=KERNEL_FEATURE_DENOISING;reason="optional volume integration";break;
      case 9:d.data.integrator.has_shadow_catcher=1;reason="shadow catcher with volumes";break;
      case 10:d.data.volume_stack_size=1;reason="volume stack size";break;
      case 11:d.data.max_closures=0;reason="volume closure capacity";break;
      case 12:d.data.film.pass_volume_transmit=PASS_UNUSED;reason="volume film pass";break;
      case 13:d.data.film.pass_volume_direct=0;reason="volume light passes";break;
      case 14:for(auto &shader:d.shader_records)if(shader.flags&SD_HAS_VOLUME)shader.flags|=SD_HETEROGENEOUS_VOLUME;reason="heterogeneous volume";break;
      case 15:d.state.ray.D=nullptr;reason="virtual pointer";break;
      case 16:d.state.volume_stack[0].shader=nullptr;reason="virtual pointer";break;
      case 17:work=0;reason="Incomplete volume dispatch";break;
    }
    std::string error;try{d.enqueue_volume(work,args);}catch(const std::exception &e){error=e.what();}
    d.data=original_data;d.state=original_state;d.shader_records=original_shaders;
    require(error.find(reason)!=std::string::npos,"Volume rejection "+std::to_string(test)+": "+error);
    for(auto &field:layout){std::vector<uint8_t> bytes(states.size()*field.stride);d.download(field.address,bytes.data(),bytes.size());
      for(size_t i=0;i<states.size();i++)require(!std::memcmp(bytes.data()+i*field.stride,reinterpret_cast<const char*>(&states[i])+field.offset,field.stride),"Rejected volume changed state");}
    std::vector<float> after(film.size());d.download(film.device_pointer,after.data(),after.size()*4);require(std::all_of(after.begin(),after.end(),[](float x){return x==.125f;}),"Rejected volume changed film");
    IntegratorQueueCounter counter{};d.download(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));require(!std::memcmp(&counter,&before,sizeof(counter)),"Rejected volume changed queue counters");
  }
  return 18;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2,"Usage: volume_gate OUTPUT_PREFIX");const std::string prefix=argv[1];auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU device");
  Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=require_webgpu_device(owner.get(),"volume_gate device creation");Metrics m;
  const float gs[]={0,.0009f,.001f,-.001f,.45f,-.7f,.999f,-.999f,1.5f,-1.5f};
  for(unsigned test=0;test<24;test++){
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;Scene scene(params,&d);prepare(scene);
    auto graph=make_unique<ShaderGraph>();auto absorption=graph->create_node<AbsorptionVolumeNode>();absorption->set_density(test==20?0:.04f);absorption->set_color(test==21?make_float3(1,1,1):make_float3(.8,.65,.4));
    auto scatter=graph->create_node<ScatterVolumeNode>();scatter->set_density(test==20?0:test==22?.00001f:.035f);scatter->set_color(test==21?make_float3(.8,0,.6):make_float3(.8,.7,.6));scatter->set_anisotropy(gs[test%10]);
    auto add=graph->create_node<AddClosureNode>();graph->connect(absorption->output("Volume"),add->input("Closure1"));graph->connect(scatter->output("Volume"),add->input("Closure2"));ShaderOutput *volume=add->output("Closure");
    if(test>=10&&test<20){auto phase=graph->create_node<ScatterVolumeNode>();phase->set_density(.017f);phase->set_color(make_float3(.2,.9,.7));phase->set_anisotropy(gs[(test+4)%10]);auto mixture=graph->create_node<AddClosureNode>();graph->connect(volume,mixture->input("Closure1"));graph->connect(phase->output("Volume"),mixture->input("Closure2"));volume=mixture->output("Closure");}
    graph->connect(volume,graph->output()->input("Volume"));auto shader=scene.create_node<Shader>();shader->set_graph(std::move(graph));shader->tag_update(&scene);scene.background->set_shader(shader);
    for(auto s:scene.shaders)s->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
    Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Volume scene preparation failed");auto &data=scene.dscene.data;
    data.background.use_mis=0;data.integrator.use_direct_light=0;data.integrator.use_light_mis=0;data.integrator.num_lights=0;data.integrator.use_caustics=0;data.integrator.filter_glossy=FLT_MAX;data.film.pass_flag=0;
    d.const_copy_to("data",&data,sizeof(data));bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);const auto layout=fields(d);
    std::vector<IntegratorStateCPU> states(scheduler.capacity());std::vector<int> ids;
    for(unsigned j=0;j<512;j++){unsigned i=3*j;ids.push_back(i);auto &s=states[i];s.path.render_pixel_index=j;s.path.sample=j*137u;s.path.rng_pixel=0x12345678u+i*0x9e3779b9u;s.path.rng_offset=uint16_t(65500u+i*79u);
      s.path.bounce=j%3;s.path.volume_bounce=j%3;s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME;s.path.visibility=s.path.bounce?PATH_RAY_VISIBILITY_VOLUME_SCATTER:PATH_RAY_VISIBILITY_CAMERA;
      s.path.flag=PATH_RAY_MIS_SKIP|PATH_RAY_TRANSPARENT_BACKGROUND;if(j%13==0)s.path.flag|=PATH_RAY_TERMINATE_AFTER_TRANSPARENT;if(j%17==0)s.path.flag|=PATH_RAY_TERMINATE_IN_NEXT_VOLUME;
      s.path.throughput=make_float3(.6,.8,.4);s.path.continuation_probability=j%23==0?0:.75f;s.path.min_ray_pdf=FLT_MAX;s.path.optical_depth=.125f;
      s.ray.P=make_float3(.1,.2,-2);s.ray.D=normalize(make_float3(.013f*j-.8f,.4f-.007f*j,j%2?1.f:-1.f));s.ray.tmin=.003f*(j%3);s.ray.tmax=FLT_MAX;s.ray.time=float(j%17)/16;s.ray.dP=.003f;s.ray.dD=.0003f*j;
      s.isect.t=s.ray.tmin+.01f+.001f*j*j;s.isect.object=0;s.isect.prim=0;s.isect.type=PRIMITIVE_TRIANGLE;
      if(test==23){s.isect.prim=PRIM_NONE;s.isect.object=OBJECT_NONE;s.isect.type=PRIMITIVE_NONE;}
      s.volume_stack[0].object=data.background.object_index;s.volume_stack[0].shader=data.background.volume_shader;s.volume_stack[1].object=OBJECT_NONE;s.volume_stack[1].shader=SHADER_NONE;
    }
    if(test==0)m.rejected=rejections(d,layout,states);
    evaluate(d,scene,layout,states,ids,m,test);
  }
  require(m.scatter>0&&m.transmit>0&&m.terminated>0,"Missing native volume events");require(stats.mem_used==0,"Volume allocation leak");
  std::ofstream(prefix+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(prefix+".library.txt")<<d.gpu.library_path;
  std::cout<<std::setprecision(17)<<"{\"passed\":true,\"originalCpuVolumeIntegrator\":true,\"cases\":"<<m.cases<<",\"invalidCasesRejected\":"<<m.rejected<<",\"paths\":"<<m.cases*512<<",\"scatterEvents\":"<<m.scatter<<",\"transmitEvents\":"<<m.transmit<<",\"terminatedEvents\":"<<m.terminated<<",\"integerChecks\":"<<m.integers<<",\"floatChecks\":"<<m.floats<<",\"strictFailures\":"<<m.strict<<",\"maximumError\":"<<m.maximum<<",\"maximumErrorActual\":"<<m.maximum_actual<<",\"maximumErrorExpected\":"<<m.maximum_expected<<",\"maximumErrorField\":\""<<m.field<<"\",\"maximumPracticalToleranceRatio\":"<<m.ratio<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
