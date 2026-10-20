// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device.h"
#include <cstring>
#include <stdexcept>
namespace ccl {
static void require(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
void WebGPUDevice::update_state(const IntegratorStateGPU &value){
#ifdef __INTEGRATOR_GPU_PACKED_STATE__
#error The WebGPU state adapter currently requires the unpacked Cycles SoA layout.
#endif
  require(data.volume_stack_size>=0&&data.volume_stack_size<=MAX_VOLUME_STACK_SIZE,"Invalid volume stack size");
  std::vector<uint32_t> descriptors;uint32_t shadow=0;
  auto field=[&](const void *pointer,size_t stride){if(!pointer)return;
    uint64_t address=reinterpret_cast<uintptr_t>(pointer);validate(address,stride);
    descriptors.insert(descriptors.end(),{uint32_t(address),uint32_t(stride),shadow,uint32_t(remaining(address)/stride)});};
#define KERNEL_STRUCT_BEGIN(name) for(int array_index=0;;array_index++) {
#define KERNEL_STRUCT_BEGIN_PACKED(name,feature) KERNEL_STRUCT_BEGIN(name)
#define KERNEL_STRUCT_MEMBER(parent,type,name,feature) field(value.parent.name,sizeof(type));
#define KERNEL_STRUCT_MEMBER_PACKED KERNEL_STRUCT_MEMBER
#define KERNEL_STRUCT_ARRAY_MEMBER(parent,type,name,feature) field(value.parent[array_index].name,sizeof(type));
#define KERNEL_STRUCT_END(name) (void)array_index;break; }
#define KERNEL_STRUCT_END_ARRAY(name,cpu_size,gpu_size) if(array_index>=gpu_size-1)break; }
#define KERNEL_STRUCT_VOLUME_STACK_SIZE data.volume_stack_size
#include "kernel/integrator/state_template.h"
  shadow=1;
#include "kernel/integrator/shadow_state_template.h"
#undef KERNEL_STRUCT_BEGIN
#undef KERNEL_STRUCT_BEGIN_PACKED
#undef KERNEL_STRUCT_MEMBER
#undef KERNEL_STRUCT_MEMBER_PACKED
#undef KERNEL_STRUCT_ARRAY_MEMBER
#undef KERNEL_STRUCT_END
#undef KERNEL_STRUCT_END_ARRAY
#undef KERNEL_STRUCT_VOLUME_STACK_SIZE
  if(fields){flush();release(fields);fields=0;}
  field_count=uint32_t(descriptors.size()/4);state=value;
  if(!descriptors.empty()){fields=allocate(descriptors.size()*4);upload(fields,descriptors.data(),descriptors.size()*4);}
}
class WebGPUQueue final:public DeviceQueue {
  WebGPUDevice &d;
 public:
  explicit WebGPUQueue(WebGPUDevice *device):DeviceQueue(device),d(*device){}
  int num_concurrent_states(size_t state_size)const override {
    return int(std::min<size_t>(4096,std::max<size_t>(64,(16*1024*1024)/std::max<size_t>(1,state_size))));}
  int num_concurrent_busy_states(size_t s)const override{return num_concurrent_states(s)/2;}
  void init_execution()override{d.guard([&]{debug_init_execution();d.load_image_info();d.sync();});}
  void load_image_info()override{d.guard([&]{d.load_image_info();});}
  bool synchronize()override{d.guard([&]{d.sync();debug_synchronize();});return !d.have_error();}
  void zero_to_device(device_memory &mem)override{d.mem_zero(mem);}
  void copy_to_device(device_memory &mem)override{d.mem_copy_to(mem);}
  void copy_from_device(device_memory &mem)override{d.mem_copy_from(mem,0,mem.memory_size(),1,1);}
  void *copy_from_device_synchronized(device_memory &mem,vector<uint8_t> &storage)override{
    storage.resize(mem.memory_size());d.guard([&]{d.download(mem.device_pointer,storage.data(),storage.size());d.sync();});return d.have_error()?nullptr:storage.data();}
  bool enqueue(DeviceKernel kernel,int work,const DeviceKernelArguments &args)override{
    if(d.have_error())return false;
    d.guard([&]{
      require(work>=0&&work<=int(d.gpu.limits.maxComputeWorkgroupsPerDimension*64),"Invalid WebGPU work size");
      if(kernel==DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA){
        debug_enqueue_begin(kernel,work);d.enqueue_camera(work,args);debug_enqueue_end();return;
      }
      if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST){
        debug_enqueue_begin(kernel,work);d.enqueue_closest(work,args);debug_enqueue_end();return;
      }
      if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_BACKGROUND){
        debug_enqueue_begin(kernel,work);d.enqueue_background(work,args);debug_enqueue_end();return;
      }
      if(kernel==DEVICE_KERNEL_INTEGRATOR_SHADE_SURFACE){
        debug_enqueue_begin(kernel,work);d.enqueue_surface(work,args);debug_enqueue_end();return;
      }
      auto scalar=[&](unsigned i){require(i<args.count&&args.types[i]==DeviceKernelArguments::INT32&&args.sizes[i]==4,"Invalid integer kernel argument");
        int32_t n;std::memcpy(&n,args.values[i],4);require(n>=0,"Negative scheduler argument");return uint32_t(n);};
      auto pointer=[&](unsigned i,size_t size){require(i<args.count&&args.types[i]==DeviceKernelArguments::POINTER&&args.sizes[i]==sizeof(device_ptr),"Invalid pointer kernel argument");
        device_ptr address;std::memcpy(&address,args.values[i],sizeof(address));d.validate(address,size);require(address%4==0,"Unaligned scheduler pointer");return uint32_t(address);};
      auto state_pointer=[&](const void *pointer,size_t size){uint64_t address=reinterpret_cast<uintptr_t>(pointer);d.validate(address,size);return uint32_t(address);};
      std::array<uint32_t,32> p{};p[17]=d.error_word;
      if(kernel==DEVICE_KERNEL_PREFIX_SUM){
        require(args.count==3,"Prefix sum argument count");p[0]=1;p[1]=scalar(2);p[8]=pointer(0,size_t(p[1])*4);p[9]=pointer(1,size_t(p[1])*4);
      }else if(kernel==DEVICE_KERNEL_INTEGRATOR_RESET){
        require(args.count==1,"Reset argument count");p[0]=0;p[1]=scalar(0);require(uint32_t(work)>=p[1],"Incomplete reset dispatch");
        p[2]=state_pointer(d.state.path.queued_kernel,size_t(p[1])*2);p[3]=state_pointer(d.state.shadow_path.queued_kernel,size_t(p[1])*2);
      }else if(kernel==DEVICE_KERNEL_INTEGRATOR_COMPACT_STATES||kernel==DEVICE_KERNEL_INTEGRATOR_COMPACT_SHADOW_STATES){
        require(args.count==4,"Compaction argument count");require(!(d.data.kernel_features&KERNEL_FEATURE_MNEE),"MNEE ownership repair is not implemented");
        p[0]=4;p[1]=scalar(3);p[12]=scalar(1);p[13]=scalar(2);require(uint32_t(work)>=p[1],"Incomplete compaction dispatch");
        p[4]=pointer(0,(size_t(std::max(p[12],p[13]))+p[1])*4);p[14]=d.fields;p[15]=d.field_count;
        p[16]=kernel==DEVICE_KERNEL_INTEGRATOR_COMPACT_SHADOW_STATES;
        p[2]=state_pointer(p[16]?d.state.shadow_path.queued_kernel:d.state.path.queued_kernel,2);
        p[18]=uint32_t(d.remaining(p[2])/2);
      }else{
        bool shadow=false;uint32_t mode=0;
        switch(kernel){
          case DEVICE_KERNEL_INTEGRATOR_QUEUED_PATHS_ARRAY:mode=0;break;
          case DEVICE_KERNEL_INTEGRATOR_QUEUED_SHADOW_PATHS_ARRAY:mode=0;shadow=true;break;
          case DEVICE_KERNEL_INTEGRATOR_ACTIVE_PATHS_ARRAY:mode=1;break;
          case DEVICE_KERNEL_INTEGRATOR_TERMINATED_PATHS_ARRAY:mode=2;break;
          case DEVICE_KERNEL_INTEGRATOR_TERMINATED_SHADOW_PATHS_ARRAY:mode=2;shadow=true;break;
          case DEVICE_KERNEL_INTEGRATOR_COMPACT_PATHS_ARRAY:mode=3;break;
          case DEVICE_KERNEL_INTEGRATOR_COMPACT_SHADOW_PATHS_ARRAY:mode=3;shadow=true;break;
          case DEVICE_KERNEL_INTEGRATOR_SORTED_PATHS_ARRAY:mode=4;break;
          default:throw std::runtime_error("WebGPU kernel not implemented: "+std::string(device_kernel_as_string(kernel)));
        }
        p[0]=mode==4?3:2;p[1]=scalar(0);p[19]=mode;require(uint32_t(work)>=p[1],"Incomplete scheduler dispatch");
        p[2]=state_pointer(shadow?d.state.shadow_path.queued_kernel:d.state.path.queued_kernel,size_t(p[1])*2);
        if(mode==4){require(args.count==7,"Sorted paths argument count");p[11]=scalar(1);p[4]=pointer(2,size_t(std::min(p[1],p[11]))*4);
          p[5]=pointer(3,4);p[8]=pointer(4,4);p[9]=pointer(5,4);p[6]=scalar(6);
          p[10]=state_pointer(d.state.path.shader_sort_key,size_t(p[1])*4);p[20]=uint32_t(std::min(d.remaining(p[8]),d.remaining(p[9]))/4);
        }else{require(args.count==(mode==1?3u:4u),"Paths array argument count");
          p[7]=mode==2?scalar(3):0;p[6]=mode==0||mode==3?scalar(3):0;
          p[4]=pointer(1,(size_t(p[1])+p[7])*4);p[5]=pointer(2,4);
        }
      }
      if(work){debug_enqueue_begin(kernel,work);d.dispatch(p,uint32_t(work));debug_enqueue_end();}
    });return !d.have_error();
  }
};
unique_ptr<DeviceQueue> WebGPUDevice::gpu_queue_create(){return make_unique<WebGPUQueue>(this);}
}
