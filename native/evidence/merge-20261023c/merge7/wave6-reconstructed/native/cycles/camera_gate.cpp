// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device.h"
#include "camera_reference.h"
#include "camera_motion_fixture.h"
#include "integrator/path_trace_work_gpu.h"
#include "scene/scene.h"
#include "scene/camera.h"
#include "scene/film.h"
#include "scene/integrator.h"
#include "util/task.h"
#include "util/progress.h"
#include "../../prototype/native/demo_camera.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
using namespace ccl;
static void require(bool ok,const std::string &message){if(!ok)throw std::runtime_error(message);}
static void healthy(WebGPUDevice &d){require(!d.have_error(),d.error_message());}
struct CameraScheduler:PathTraceWorkGPU {
  using PathTraceWorkGPU::PathTraceWorkGPU;
  using PathTraceWorkGPU::enqueue_reset;
  using PathTraceWorkGPU::enqueue_work_tiles;
  using PathTraceWorkGPU::update_queue_counter_and_cache;
  using PathTraceWorkGPU::get_most_queued_kernel;
  int capacity()const{return max_num_paths_;}
};
struct Field {std::string name;uint32_t address;size_t stride,offset;bool floating;};
static std::vector<Field> fields(WebGPUDevice &d){std::vector<Field> out;
#define KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) if(d.state.parent.name)out.push_back({#parent "." #name,uint32_t(uintptr_t(d.state.parent.name)),sizeof(type),offsetof(IntegratorStateCPU,parent.name),std::is_same_v<type,float>||std::is_same_v<type,packed_float3>||std::is_same_v<type,PackedSpectrum>});
#define KERNEL_STRUCT_MEMBER_PACKED KERNEL_STRUCT_MEMBER
#define KERNEL_STRUCT_ARRAY_MEMBER(parent,type,name,feature)
#define KERNEL_STRUCT_END(name)
#define KERNEL_STRUCT_END_ARRAY(name,cpu_size,gpu_size)
#include "kernel/integrator/state_template.h"
#undef KERNEL_STRUCT_BEGIN
#undef KERNEL_STRUCT_BEGIN_PACKED
#undef KERNEL_STRUCT_MEMBER
#undef KERNEL_STRUCT_MEMBER_PACKED
#undef KERNEL_STRUCT_ARRAY_MEMBER
#undef KERNEL_STRUCT_END
#undef KERNEL_STRUCT_END_ARRAY
  return out;
}
struct Metrics {uint64_t paths=0,integer_checks=0,float_checks=0,guard_bytes=0,strict_float_failures=0;double max_ray_error=0,max_differential_error=0,max_tmax_error=0;unsigned cases=0,retries=0;};
static void compare(WebGPUDevice &d,const std::vector<Field> &fields,const std::vector<IntegratorStateCPU> &expected,Metrics &m){
  for(const auto &f:fields){std::vector<uint8_t> actual(f.stride*expected.size());d.download(f.address,actual.data(),actual.size());
    for(size_t i=0;i<expected.size();i++){
      auto *want=reinterpret_cast<const uint8_t*>(&expected[i])+f.offset;auto *got=actual.data()+i*f.stride;
      // Only ray fields have approximate arithmetic. Every other state byte,
      // including untouched ray guards, must agree exactly with the CPU oracle.
      bool ray=f.name.starts_with("ray.")&&f.name!="ray.previous_dt"&&expected[i].path.queued_kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST;
      if(ray&&f.floating){for(size_t k=0;k<f.stride;k+=4){float a,e;std::memcpy(&a,got+k,4);std::memcpy(&e,want+k,4);double error=std::abs(double(a)-e);
        require(std::isfinite(a)&&error<=5e-5+2e-4*std::abs(double(e)),"Camera ray mismatch: "+f.name+" path="+std::to_string(i)+" got="+std::to_string(a)+" expected="+std::to_string(e));
        if(error>1e-7+1e-7*std::abs(double(e)))m.strict_float_failures++;
        if(f.name=="ray.dP"||f.name=="ray.dD")m.max_differential_error=std::max(m.max_differential_error,error);
        else if(f.name=="ray.tmax")m.max_tmax_error=std::max(m.max_tmax_error,error);
        else m.max_ray_error=std::max(m.max_ray_error,error);
        m.float_checks++;
      }}else{require(std::memcmp(got,want,f.stride)==0,"Camera state/guard mismatch: "+f.name+" path="+std::to_string(i));
        m.integer_checks++;if(!expected[i].path.queued_kernel)m.guard_bytes+=f.stride;}
    }
  }
  IntegratorQueueCounter counter{};d.download(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
  std::vector<int> counts(DEVICE_GPU_KERNEL_INTEGRATOR_NUM);for(auto &s:expected)if(s.path.queued_kernel)counts.at(s.path.queued_kernel)++;
  for(unsigned i=0;i<counts.size();i++)require(counter.num_queued[i]==counts[i],"Camera queue counter mismatch");
  require(!counter.cache_miss,"Unexpected camera cache miss");healthy(d);
}
static KernelWorkTile tile(unsigned x,unsigned y,unsigned w,unsigned h,unsigned samples,unsigned sample){
  KernelWorkTile t{};t.x=x;t.y=y;t.w=w;t.h=h;t.num_samples=samples;t.start_sample=sample;t.stride=64;t.sample_offset=11;return t;
}
static void reorder_atomic_samples(WebGPUDevice &d,std::vector<IntegratorStateCPU> &expected){
  // Atomic sample reservation may assign samples to lanes in any order. Prove
  // one-to-one membership in the independently computed CPU (pixel,sample)
  // set before comparing the complete state at each lane.
  std::map<std::pair<uint32_t,uint32_t>,IntegratorStateCPU> records;
  for(const auto &state:expected)if(state.path.queued_kernel)
    require(records.emplace(std::pair{state.path.render_pixel_index,state.path.sample},state).second,"Duplicate CPU pixel/sample");
  std::vector<uint32_t> samples(expected.size());d.download(uintptr_t(d.state.path.sample),samples.data(),samples.size()*4);
  for(size_t i=0;i<expected.size();i++)if(expected[i].path.queued_kernel){
    auto key=std::pair{expected[i].path.render_pixel_index,samples[i]};auto found=records.find(key);
    require(found!=records.end(),"GPU sample reservation missing, duplicated, or outside the CPU sample set");
    expected[i]=found->second;records.erase(found);
  }
  require(records.empty(),"GPU sample reservation omitted a CPU sample");
}
static unsigned rejections(const DeviceInfo &info,bool motion){
  unsigned rejected=0;
  for(unsigned test=0;test<(motion?19u:12u);test++){
    Stats stats;Profiler profiler;auto owner=Device::create(info,stats,profiler,true);auto *backend=dynamic_cast<WebGPUDevice*>(owner.get());require(backend,"Negative camera device creation failed");auto &d=*backend;
    SceneParams params;params.use_texture_cache=false;Scene scene(params,&d);
    scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);scene.film->set_use_sample_count(false);
    Progress progress;scene.device_update(&d,progress);healthy(d);bool cancelled=false;
    CameraScheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);scheduler.alloc_work_memory();scheduler.init_execution();scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Negative camera setup reset failed");
    auto &data=scene.dscene.data;data.film.pass_stride=12;data.film.pass_sample_count=PASS_UNUSED;data.film.pass_adaptive_aux_buffer=PASS_UNUSED;
    std::string reason;
    if(test==0){data.cam.type=CAMERA_ORTHOGRAPHIC;reason="perspective pinhole";}
    if(test==1){data.cam.aperturesize=1;reason="perspective pinhole";}
    if(test==2){data.integrator.sampling_pattern=SAMPLING_PATTERN_SOBOL_BURLEY;reason="tabulated Sobol";}
    if(test==3){data.kernel_features|=KERNEL_FEATURE_DENOISING;reason="optional path features";}
    if(test==4){data.film.pass_sample_count=12;reason="film pass offset";}
    if(test==12){data.cam.num_motion_steps=1;reason="motion step count";}
    if(test==13){data.cam.num_motion_steps=2;reason="native camera motion table";}
    if(test==14){data.cam.shuttertime=1;data.cam.shutter_table_offset=-1;reason="shutter table offset";}
    if(test==15){data.cam.rolling_shutter_duration=-.1f;reason="rolling shutter";}
    if(test==16){data.cam.height=0;reason="camera height";}
    if(test==17){data.cam.shuttertime=std::nanf("");reason="shutter time";}
    if(test==18){data.cam.rolling_shutter_type=9;reason="rolling shutter";}
    d.const_copy_to("data",&data,sizeof(data));healthy(d);
    device_vector<float> film(&d,"negative_camera_film",MEM_READ_WRITE);film.alloc(12);film.zero_to_device();
    device_vector<KernelWorkTile> tiles(&d,"negative_camera_tile",MEM_READ_WRITE);tiles.alloc(2);tiles[0]=tile(0,0,1,1,1,0);tiles[0].work_size=1;tiles[1]=tiles[0];
    if(test==6){tiles[0].path_index_offset=scheduler.capacity();reason="reported invalid data";}
    if(test==7){tiles[0].x=4;reason="reported invalid data";}
    if(test==8){tiles[0].work_size=2;reason="reported invalid data";}
    if(test==10){tiles[0].w=0xffffffffu;reason="reported invalid data";}
    tiles.copy_to_device();device_ptr film_pointer=film.device_pointer,tile_pointer=tiles.device_pointer;
    int one=1,num_tiles=test==11?2:1;
    if(test==5){film_pointer=0;reason="virtual pointer";}
    device_vector<int> indices(&d,"negative_camera_indices",MEM_READ_WRITE);indices.alloc(1);indices[0]=scheduler.capacity();indices.copy_to_device();
    device_ptr index_pointer=0;int retry_count=0;
    if(test==9){tile_pointer=0;num_tiles=0;index_pointer=indices.device_pointer;retry_count=1;reason="reported invalid data";}
    if(test==11)reason="Incomplete camera tile dispatch";
    auto queue=d.gpu_queue_create();DeviceKernelArguments args(&tile_pointer,&num_tiles,&film_pointer,&one,&index_pointer,&retry_count);
    bool accepted=queue->enqueue(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,1,args);if(accepted)accepted=queue->synchronize();
    require(!accepted&&d.error_message().find(reason)!=std::string::npos,"Camera negative case "+std::to_string(test)+": "+d.error_message());
    IntegratorQueueCounter counter{};d.download(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
    require(std::all_of(std::begin(counter.num_queued),std::end(counter.num_queued),[](int n){return n==0;}),"Rejected camera dispatch changed queue counters");
    std::vector<float> unchanged(12);d.download(film.device_pointer,unchanged.data(),48);require(std::all_of(unchanged.begin(),unchanged.end(),[](float n){return n==0;}),"Rejected camera dispatch changed film");rejected++;
  }
  return rejected;
}
int main(int argc,char **argv){TaskScheduler::init(1);int status=0;try{
  require(argc==2||(argc==3&&std::string(argv[2])=="--motion"),"Usage: camera_gate OUTPUT_PREFIX [--motion]");const bool motion=argc==3;auto infos=Device::available_devices(DEVICE_MASK_WEBGPU);require(infos.size()==1,"Missing WebGPU device");
  Stats stats;Profiler profiler;auto owner=Device::create(infos[0],stats,profiler,true);auto *device=dynamic_cast<WebGPUDevice*>(owner.get());require(device,"WebGPU creation failed");auto &d=*device;Metrics m;
  {
    SceneParams params;params.bvh_layout=BVH_LAYOUT_BVH2;params.bvh_type=BVH_TYPE_STATIC;params.use_texture_cache=false;
    Scene scene(params,&d);scene.integrator->set_use_guiding(false);scene.integrator->set_use_light_tree(false);scene.integrator->set_use_adaptive_sampling(false);
    scene.integrator->set_aa_samples(64);scene.integrator->set_seed(17);scene.film->set_use_sample_count(false);
    const auto cam=demo_camera(64,48);scene.camera->set_full_width(64);scene.camera->set_full_height(48);scene.camera->set_screen_size(64,48);
    scene.camera->set_fov(2*std::atan(cam.tan_half_fov));Transform transform;
    transform.x=make_float4(cam.right[0],cam.up[0],cam.forward[0],cam.origin[0]);
    transform.y=make_float4(cam.right[1],cam.up[1],cam.forward[1],cam.origin[1]);
    transform.z=make_float4(cam.right[2],cam.up[2],cam.forward[2],cam.origin[2]);
    scene.camera->set_matrix(transform);scene.camera->compute_auto_viewplane();Progress progress;scene.device_update(&d,progress);healthy(d);
    bool cancelled=false;CameraScheduler scheduler(&d,scene.film,&scene.dscene,&cancelled);
    scheduler.alloc_work_memory();scheduler.init_execution();healthy(d);const auto layout=fields(d);require(layout.size()>=35,"Missing native camera state fields");
    BufferParams buffer_params;buffer_params.width=64;buffer_params.height=48;buffer_params.pass_stride=12;
    auto buffers=scheduler.get_render_buffers();buffers->reset(buffer_params);buffers->zero();healthy(d);
    auto original=scene.dscene.data;
    for(unsigned test=0;test<24;test++){
      auto &data=scene.dscene.data;
      if(motion){camera_motion_fixture(scene,transform,test%8,test<8?1.f:test<16?4.f:8.5f);scene.device_update(&d,progress);healthy(d);original=scene.dscene.data;
        require(original.cam.num_motion_steps>=2&&scene.dscene.camera_motion.size()==size_t(original.cam.num_motion_steps),"Native camera motion preparation failed");}
      data=original;data.integrator.scrambling_distance=std::array{.125f,.75f,.9f,1.f}[test%4];
      data.integrator.seed=test==15?0xffffffffu:17u+test*43;data.cam.nearclip=test%2?.1f:1e-5f;data.cam.cliplength=1000;
      const bool adaptive=(test>=12&&test<16)||test>=20;
      data.film.pass_stride=12;data.film.pass_sample_count=test>=8?8:PASS_UNUSED;data.film.pass_adaptive_aux_buffer=adaptive?4:PASS_UNUSED;
      if(test>=4&&test<8)data.integrator.pixel_jitter=make_float2(.125f,-.25f);
      d.const_copy_to("data",&data,sizeof(data));healthy(d);
      const auto n=scheduler.capacity();std::vector<IntegratorStateCPU> expected(n);std::vector<float> film(64*48*12);
      for(const auto &f:layout){std::vector<uint8_t> bytes(size_t(n)*f.stride,0x3e);
        for(int i=0;i<n;i++)std::memcpy(reinterpret_cast<uint8_t*>(&expected[i])+f.offset,bytes.data()+i*f.stride,f.stride);
        d.upload(f.address,bytes.data(),bytes.size());}
      scheduler.enqueue_reset();require(scheduler.update_queue_counter_and_cache(),"Scheduler reset failed");
      for(auto &s:expected)s.path.queued_kernel=0;
      if(test>=8)for(unsigned pixel=0;pixel<64*48;pixel++)film[pixel*12+8]=std::bit_cast<float>(uint32_t(pixel%7));
      if(adaptive)for(unsigned pixel=0;pixel<64*48;pixel++)if(pixel%5==0)film[pixel*12+7]=1;
      std::memcpy(buffers->buffer.data(),film.data(),film.size()*4);buffers->buffer.copy_to_device();
      const unsigned samples=test<8||test>=16?8:1;
      std::vector<KernelWorkTile> tiles{tile(3,2,13,11,samples,test%3==0?0:13),tile(31,19,7,5,samples,test%3==0?0:5),tile(60,46,1,1,1,0)};
      if(motion){tiles.back().x=0;tiles.back().y=0;}
      constexpr unsigned first=3;
      original_camera(scene.dscene,tiles,first,expected,film);
      scheduler.enqueue_work_tiles(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,tiles.data(),int(tiles.size()),first,0);
      require(scheduler.update_queue_counter_and_cache(),"Camera scheduler dispatch/sync: "+d.error_message());healthy(d);
      require(scheduler.get_most_queued_kernel()==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST,"Scheduler did not select intersection next");
      if(test>=16)reorder_atomic_samples(d,expected);
      compare(d,layout,expected,m);buffers->buffer.copy_from_device();healthy(d);require(std::memcmp(buffers->buffer.data(),film.data(),film.size()*4)==0,"Camera film sample counts/adaptive guards differ");
      for(const auto &s:expected)m.paths+=s.path.queued_kernel!=0;
      m.cases++;
      // The cache retry path uses the existing state and an arbitrary index
      // array. It must decrement the camera count and preserve film counts.
      if(test==15){std::vector<int> indices;for(int i=0;i<n&&indices.size()<65;i++)if(expected[i].path.queued_kernel&&i%2)indices.push_back(i);
        IntegratorQueueCounter counter{};d.download(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
        for(int i:indices){expected[i].path.queued_kernel=DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA;uint16_t value=DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA;
          // Upload pairs to respect the backend's four-byte write granularity.
          uint32_t pair;auto address=uint32_t(uintptr_t(d.state.path.queued_kernel))+(uint32_t(i)&~1u)*2;
          d.download(address,&pair,4);pair=(pair&0x0000ffffu)|(uint32_t(value)<<16);d.upload(address,&pair,4);}
        counter.num_queued[DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA]=int(indices.size());counter.num_queued[DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST]-=int(indices.size());d.upload(uintptr_t(d.state.queue_counter),&counter,sizeof(counter));
        original_camera_retry(scene.dscene,indices,expected,film);device_vector<int> index_buffer(&d,"camera_retry_indices",MEM_READ_WRITE);index_buffer.alloc(indices.size());std::copy(indices.begin(),indices.end(),index_buffer.data());index_buffer.copy_to_device();
        auto queue=d.gpu_queue_create();device_ptr null=0;int zero=0,count=int(indices.size());
        DeviceKernelArguments args(&null,&zero,&buffers->buffer.device_pointer,&zero,&index_buffer.device_pointer,&count);
        require(queue->enqueue(DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA,count,args),"Camera retry enqueue failed");require(queue->synchronize(),"Camera retry synchronization failed");
        compare(d,layout,expected,m);buffers->buffer.copy_from_device();require(std::memcmp(buffers->buffer.data(),film.data(),film.size()*4)==0,"Retry changed film sample counts");m.retries=indices.size();
      }
    }
  }
  require(stats.mem_used==0,"Camera gate leaked Cycles allocations");
  const unsigned rejected=rejections(infos[0],motion);
  std::string output=argv[1];std::ofstream(output+".adapter.txt")<<d.gpu.adapter_name;std::ofstream(output+".library.txt")<<d.gpu.library_path;
  std::cout<<"{\"passed\":true,\"originalSchedulerTileDispatch\":true,\"directNativeSceneTables\":true,\"originalCpuCameraOracle\":true,\"rayDifferentials\":true,\"cases\":"<<m.cases
    <<",\"initializedPaths\":"<<m.paths<<",\"retryPaths\":"<<m.retries<<",\"integerAndGuardChecks\":"<<m.integer_checks<<",\"floatChecks\":"<<m.float_checks<<",\"untouchedGuardBytes\":"<<m.guard_bytes
    <<",\"strictFloatFailures\":"<<m.strict_float_failures<<",\"maximumRayErrorExcludingTmax\":"<<m.max_ray_error<<",\"maximumDifferentialError\":"<<m.max_differential_error
    <<",\"cameraMotionCases\":"<<(motion?m.cases:0)<<",\"maximumTmaxError\":"<<m.max_tmax_error
    <<",\"invalidCasesRejected\":"<<rejected<<",\"absoluteTolerance\":0.00005,\"relativeTolerance\":0.0002,\"strictAbsoluteTolerance\":0.0000001,\"strictRelativeTolerance\":0.0000001,\"cyclesAllocationBytesAfterCleanup\":"<<stats.mem_used<<"}\n";
}catch(const std::exception &e){std::cerr<<e.what()<<'\n';status=1;}TaskScheduler::exit();return status;}
