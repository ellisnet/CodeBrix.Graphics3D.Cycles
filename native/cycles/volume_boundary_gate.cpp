// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "wavefront_test.h"
#include "volume_reference.h"
#include "surface_closure.h"
#include "surface_reference.h"
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
#include "volume_boundary_fixture.h"
struct Metrics {uint64_t floats=0,integers=0,strict=0,scatter=0,transmit=0,terminated=0;uint64_t entered=0,exited=0,inside=0,outside=0,overlap=0,only=0,reflected=0,transmitted=0;unsigned cases=0,rejected=0,density=0;float maximum_actual=0,maximum_expected=0;double maximum=0,ratio=0;std::string field;};
static void number(float a,float e,const std::string &field,Metrics &m){
  const double error=std::abs(double(a)-e);m.floats++;m.strict+=error>1e-7+1e-7*std::abs(double(e));
  if(error>m.maximum){m.maximum=error;m.maximum_actual=a;m.maximum_expected=e;m.field=field;}m.ratio=std::max(m.ratio,error/(5e-5+2e-4*std::abs(double(e))));
  if(!(std::isfinite(a)&&std::isfinite(e)&&error<=5e-5+2e-4*std::abs(double(e)))){std::cerr<<std::setprecision(10)<<field<<" actual="<<a<<" expected="<<e<<'\n';throw std::runtime_error("Volume practical comparison failed");}
}
static void evaluate(WebGPUDevice &d,Scene &scene,const std::vector<Field> &layout,const std::vector<IntegratorStateCPU>&input,
                     const std::vector<int>&ids,Metrics &m,unsigned test,DeviceKernel kernel){
  const auto &data=scene.dscene.data;std::vector<float> wanted(512*data.film.pass_stride);
  for(unsigned pixel=0;pixel<512;pixel++){
    auto at=pixel*data.film.pass_stride;
    if(pixel%3){wanted[at+data.film.pass_volume_scatter_denoised]=rgb_to_rgbe(make_float3(.13,.27,.09)).f;
      wanted[at+data.film.pass_volume_transmit_denoised]=rgb_to_rgbe(make_float3(.9,.12,.25)).f;
      wanted[at+data.film.pass_volume_majorant]=.9;wanted[at+data.film.pass_volume_majorant_sample_count]=2;}
  }
  auto expected=input;auto initial=wanted;if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME)original_volume(scene.dscene,ids,expected,wanted);
  else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE)original_surface(scene.dscene,ids,expected,wanted);
  else original_volume_stack(scene.dscene,ids,expected);
  for(auto i:ids){auto &s=expected[i];if(s.path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE){
      s.path.shader_sort_key=(scene.dscene.tri_shader[s.isect.prim]&SHADER_MASK)+data.max_shaders*(i/d.state.sort_partition_divisor);m.transmit++;}
    else if(s.path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND)m.transmit++;
    else if(s.path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST)m.scatter++;
    else if(!s.path.queued_kernel)m.terminated++;
  }
  auto stack_count=[](const IntegratorStateCPU &s){unsigned n=0;while(n<MAX_VOLUME_STACK_SIZE&&s.volume_stack[n].shader!=SHADER_NONE)n++;return n;};
  for(int i:ids){const auto before=stack_count(input[i]),after=stack_count(expected[i]);
    if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK){m.inside+=after>0;m.outside+=after==0;m.overlap+=after>1;}
    if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE){m.entered+=after>before;m.exited+=after<before;m.only+=expected[i].path.volume_bounds_bounce>input[i].path.volume_bounds_bounce;
      if(expected[i].path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST){m.reflected+=expected[i].path.glossy_bounce>input[i].path.glossy_bounce;m.transmitted+=expected[i].path.transmission_bounce>input[i].path.transmission_bounce;}}
  }
  upload(d,layout,input);device_vector<float> film(&d,"volume_film",MEM_READ_WRITE);film.alloc(wanted.size());std::copy(initial.begin(),initial.end(),film.data());film.copy_to_device();
  device_vector<int> selected(&d,"volume_indices",MEM_READ_WRITE);selected.alloc(ids.size());std::copy(ids.begin(),ids.end(),selected.data());selected.copy_to_device();
  IntegratorQueueCounter count{};count.num_queued[kernel]=ids.size();d.upload(uintptr_t(d.state.queue_counter),&count,sizeof(count));
  for(auto kernel:{DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE,DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE_RAYTRACE})if(d.state.sort_key_counter[kernel])d.zero(uintptr_t(d.state.sort_key_counter[kernel]),d.remaining(uintptr_t(d.state.sort_key_counter[kernel])));
  auto queue=d.gpu_queue_create();int n=ids.size();DeviceKernelArguments args;args.add(&selected.device_pointer);if(kernel!=DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK)args.add(&film.device_pointer);args.add(&n);
  require(queue->enqueue(kernel,n+63,args),d.error_message());require(queue->synchronize(),d.error_message());
  if(test==15&&std::getenv("CODEBRIX_VOLUME_STACK_TRACE")){SurfaceClosureRecord records[8];d.download(d.volume_closures+81*MAX_CLOSURE*sizeof(SurfaceClosureRecord),records,sizeof(records));for(auto &r:records)std::cerr<<"GPU phase g="<<r.roughness<<" sample="<<r.sample_weight<<" weight="<<r.weight.x<<","<<r.weight.y<<","<<r.weight.z<<'\n';}
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
static void density_check(WebGPUDevice &d,Scene &scene,const VolumeBoundaryFixture &fixture,Metrics &m){
  constexpr unsigned total=1024;std::vector<KernelShaderEvalInput> input(total*2);std::vector<float> expected(total*2,.125f);
  for(unsigned i=0;i<total;i++){auto object=fixture.objects[i%4];input[i*2].object=object->get_device_index();input[i*2].prim=std::bit_cast<int>(-.7f);input[i*2].u=.3f;input[i*2].v=.2f;
    input[i*2+1].object=i%7?scene.dscene.tri_shader[object->get_geometry()->prim_offset]&SHADER_MASK:SHADER_NONE;input[i*2+1].prim=std::bit_cast<int>(2.f);input[i*2+1].u=3;input[i*2+1].v=1;}
  original_volume_density(scene.dscene,input,expected);int offset=127,count=800;
  device_vector<KernelShaderEvalInput> in(&d,"density_input",MEM_READ_ONLY);in.alloc(input.size());std::copy(input.begin(),input.end(),in.data());in.copy_to_device();
  device_vector<float> out(&d,"density_output",MEM_READ_WRITE);out.alloc(expected.size());std::fill_n(out.data(),out.size(),.125f);out.copy_to_device();
  device_vector<uint> cache(&d,"density_cache_miss",MEM_READ_WRITE);cache.alloc(1);cache[0]=0;cache.copy_to_device();
  auto queue=d.gpu_queue_create();DeviceKernelArguments args(&in.device_pointer,&out.device_pointer,&cache.device_pointer,&offset,&count);
  require(queue->enqueue(DEVICE_KERNEL_SHADER_EVAL_VOLUME_DENSITY,count+63,args),d.error_message());require(queue->synchronize(),d.error_message());out.copy_from_device();cache.copy_from_device();require(cache[0]==0,"Density cache unexpectedly modified");
  for(unsigned i=0;i<expected.size();i++){const float wanted=i>=unsigned(offset*2)&&i<unsigned((offset+count)*2)?expected[i]:.125f;number(out[i],wanted,"native density="+std::to_string(i),m);}m.density=count;
}
static unsigned malformed(WebGPUDevice &d,const std::vector<Field> &layout,const std::vector<IntegratorStateCPU> &valid){
  device_vector<float> film(&d,"malformed_volume_film",MEM_READ_WRITE);film.alloc(d.data.film.pass_stride);std::fill_n(film.data(),film.size(),.125f);film.copy_to_device();
  device_vector<int> selected(&d,"malformed_volume_index",MEM_READ_WRITE);selected.alloc(1);int n=1;
  for(unsigned test=0;test<6;test++){
    auto states=valid;auto &s=states[0];s.path.render_pixel_index=0;auto kernel=test<4?DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME:DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK;s.path.queued_kernel=kernel;
    if(test==0)for(unsigned k=0;k<d.data.volume_stack_size;k++){s.volume_stack[k]=s.volume_stack[0];s.volume_stack[k].shader=(s.volume_stack[k].shader&SHADER_MASK)|(1u<<(24+k));}
    if(test==1){s.volume_stack[1]=s.volume_stack[0];s.volume_stack[2].shader=SHADER_NONE;}
    if(test==2)s.volume_stack[0].shader=SHADER_MASK;
    if(test==3)s.volume_stack[0].object=0x7fffffff;
    if(test==5)s.path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE;
    selected[0]=test==4?states.size():0;selected.copy_to_device();upload(d,layout,states);IntegratorQueueCounter before{};before.num_queued[kernel]=1;d.upload(uintptr_t(d.state.queue_counter),&before,sizeof(before));
    DeviceKernelArguments args;args.add(&selected.device_pointer);if(test<4)args.add(&film.device_pointer);args.add(&n);
    if(test<4)d.enqueue_volume(64,args);else d.enqueue_volume_stack(64,args);
    uint error=0;d.download(d.error_word,&error,4);require(error!=0,"Malformed volume was accepted");d.zero(d.error_word,4);
    const auto actual=download(d,layout,states.size());for(auto &f:layout)for(size_t i=0;i<states.size();i++)require(!std::memcmp(reinterpret_cast<const char*>(&actual[i])+f.offset,reinterpret_cast<const char*>(&states[i])+f.offset,f.stride),"Malformed volume changed path state");
    IntegratorQueueCounter after{};d.download(uintptr_t(d.state.queue_counter),&after,sizeof(after));require(!std::memcmp(&after,&before,sizeof(after)),"Malformed volume changed queues");
    film.copy_from_device();require(std::all_of(film.data(),film.data()+film.size(),[](float v){return v==.125f;}),"Malformed volume changed film");
  }
  return 6;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2,"Usage: volume_boundary_gate OUTPUT_PREFIX");const std::string prefix=argv[1];auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU device");
  Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto &d=require_webgpu_device(owner.get(),"volume_boundary_gate device creation");Metrics m;
  {SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;Scene scene(params,&d);
    scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);scene.integrator->set_aa_samples(16);scene.film->set_use_sample_count(false);
    scene.camera->set_full_width(64);scene.camera->set_full_height(48);scene.camera->set_screen_size(64,48);scene.camera->compute_auto_viewplane();auto fixture=add_volume_boundaries(scene);
    Progress progress;scene.device_update(&d,progress);healthy(d);require(!progress.get_error(),"Closed volume scene preparation failed");auto &data=scene.dscene.data;
    data.background.use_mis=0;data.integrator.use_direct_light=0;data.integrator.use_light_mis=0;data.integrator.num_lights=0;data.integrator.use_caustics=0;data.integrator.filter_glossy=FLT_MAX;data.film.pass_flag=0;
    d.const_copy_to("data",&data,sizeof(data));bool cancelled=false;Scheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);const auto layout=fields(d);const auto original=data;density_check(d,scene,fixture,m);
    std::cerr<<"Closed volume stack="<<data.volume_stack_size<<" closures="<<data.max_closures<<" film="<<data.film.pass_stride<<'\n';
    for(unsigned test=0;test<24;test++){
      if((std::getenv("CODEBRIX_VOLUME_STACK_TRACE")||std::getenv("CODEBRIX_VOLUME_COMPACTION_CASE"))&&test!=15)continue;
      data=original;if(test>=20)data.max_closures=1+(test%2);if(test==9)data.volume_stack_size=2;d.const_copy_to("data",&data,sizeof(data));
      const auto kernel=test<4?DEVICE_KERNEL_INTEGRATOR_INTERSECT_VOLUME_STACK:test<12?DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE:DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME;
      std::vector<IntegratorStateCPU> states(scheduler.capacity());std::vector<int> ids;
      for(unsigned j=0;j<512;j++){unsigned i=3*j;ids.push_back(i);auto &s=states[i];s.path.render_pixel_index=j;s.path.sample=j*137u;s.path.rng_pixel=0x12345678u+i*0x9e3779b9u;s.path.rng_offset=uint16_t(65500u+i*79u);
        s.path.bounce=j%3;s.path.volume_bounce=j%3;s.path.queued_kernel=kernel;s.path.visibility=s.path.bounce?PATH_RAY_VISIBILITY_VOLUME_SCATTER:PATH_RAY_VISIBILITY_CAMERA;
        s.path.flag=PATH_RAY_MIS_SKIP|PATH_RAY_TRANSPARENT_BACKGROUND;s.path.throughput=make_float3(.6,.8,.4);s.path.continuation_probability=.8f;s.path.min_ray_pdf=FLT_MAX;s.path.optical_depth=.125f;
        s.ray.P=make_float3(-3.1f+.0123f*j,-1.3f+.0071f*(j%371),-3.5f+.41f*test+.0117f*j);s.ray.D=normalize(make_float3(.013f*j-.8f,.4f-.007f*j,j%2?1.f:-1.f));s.ray.tmin=.003f*(j%3);s.ray.tmax=FLT_MAX;s.ray.time=float(j%17)/16;s.ray.dP=.003f;s.ray.dD=.0003f*j;
        auto object=fixture.objects[(j/12)%4];s.isect.t=s.ray.tmin+.01f+.001f*j*j;s.isect.object=object->get_device_index();s.isect.prim=object->get_geometry()->prim_offset+j%12;s.isect.type=PRIMITIVE_TRIANGLE;s.isect.u=.23f;s.isect.v=.31f;
        unsigned count=0;
        auto push=[&](Object *obj){s.volume_stack[count].object=obj->get_device_index();s.volume_stack[count].shader=scene.dscene.tri_shader[obj->get_geometry()->prim_offset];count++;};
        if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME){for(unsigned k=0;k<1+(test%4);k++)push(fixture.objects[(k+j)%4]);}
        else if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE){
          if(test==5||test==7||test==8)push(object);
          if(test==6||test==8)push(fixture.objects[((j/12)+1)%4]);
          if(test==9)push(fixture.objects[((j/12)+1)%4]);
          if(test==10)s.path.volume_bounds_bounce=VOLUME_BOUNDS_MAX;
          if(test==11)s.path.continuation_probability=0;
        }
        s.volume_stack[count].object=OBJECT_NONE;s.volume_stack[count].shader=SHADER_NONE;
      }
      if(std::getenv("CODEBRIX_VOLUME_COMPACTION_CASE")){
        upload(d,layout,states);device_vector<float> film(&d,"compaction_guard_film",MEM_READ_WRITE);film.alloc(512*data.film.pass_stride);std::fill_n(film.data(),film.size(),.125f);film.copy_to_device();
        device_vector<int> selected(&d,"compaction_guard_indices",MEM_READ_WRITE);selected.alloc(1);selected[0]=243;selected.copy_to_device();int n=1;IntegratorQueueCounter before{};before.num_queued[DEVICE_KERNEL_INTEGRATOR_SHADE_VOLUME]=1;d.upload(uintptr_t(d.state.queue_counter),&before,sizeof(before));
        DeviceKernelArguments args(&selected.device_pointer,&film.device_pointer,&n);d.enqueue_volume(64,args);uint error=0;d.download(d.error_word,&error,4);require(error==16,"Mixed-phase compaction guard missing");d.zero(d.error_word,4);
        const auto actual=download(d,layout,states.size());for(auto &f:layout)for(size_t i=0;i<states.size();i++)require(!std::memcmp(reinterpret_cast<const char*>(&actual[i])+f.offset,reinterpret_cast<const char*>(&states[i])+f.offset,f.stride),"Compaction guard changed state");
        IntegratorQueueCounter after{};d.download(uintptr_t(d.state.queue_counter),&after,sizeof(after));require(!std::memcmp(&after,&before,sizeof(after)),"Compaction guard changed queues");film.copy_from_device();require(std::all_of(film.data(),film.data()+film.size(),[](float x){return x==.125f;}),"Compaction guard changed film");m.rejected++;continue;
      }
      if(test==12)m.rejected=malformed(d,layout,states);
      evaluate(d,scene,layout,states,ids,m,test,kernel);
    }
  }
  if(!std::getenv("CODEBRIX_VOLUME_COMPACTION_CASE"))require(m.entered&&m.exited&&m.inside&&m.outside&&m.overlap&&m.only&&m.reflected&&m.transmitted,"Missing boundary event coverage");
  require(stats.mem_used==0,"Boundary allocation leak");std::ofstream(prefix+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(prefix+".library.txt")<<d.gpu.library_path;
  std::cout<<std::setprecision(17)<<"{\"passed\":true,\"originalCpuVolumeBoundaries\":true,\"cases\":"<<m.cases<<",\"densityInputs\":"<<m.density<<",\"malformedCasesRejected\":"<<m.rejected<<",\"enteredVolumes\":"<<m.entered<<",\"exitedVolumes\":"<<m.exited<<",\"initializedInside\":"<<m.inside<<",\"initializedOutside\":"<<m.outside<<",\"initializedOverlap\":"<<m.overlap<<",\"volumeOnlyBoundaries\":"<<m.only<<",\"glossyReflections\":"<<m.reflected<<",\"glossyTransmissions\":"<<m.transmitted<<",\"paths\":"<<m.cases*512<<",\"integerChecks\":"<<m.integers<<",\"floatChecks\":"<<m.floats<<",\"strictFailures\":"<<m.strict<<",\"maximumError\":"<<m.maximum<<",\"maximumErrorActual\":"<<m.maximum_actual<<",\"maximumErrorExpected\":"<<m.maximum_expected<<",\"maximumErrorField\":\""<<m.field<<"\",\"maximumPracticalToleranceRatio\":"<<m.ratio<<",\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
