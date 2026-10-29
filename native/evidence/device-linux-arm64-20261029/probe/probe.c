// Scratch diagnostic: mirror cbc::Device adapter selection and device request on Vulkan.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include "webgpu.h"
#include "wgpu.h"
static void pr(const char*k,WGPUStringView v){printf("  %s: %.*s\n",k,(int)(v.length==WGPU_STRLEN?0:v.length),v.data?v.data:"");}
static volatile int done=0;static WGPUDevice dev=NULL;
static void cb(WGPURequestDeviceStatus s,WGPUDevice d,WGPUStringView m,void*a,void*b){(void)a;(void)b;
 printf("  requestDevice status=%d msg=%.*s\n",(int)s,(int)(m.length==WGPU_STRLEN?0:m.length),m.data?m.data:"");dev=d;done=1;}
int main(void){
 WGPUInstanceExtras ex={0};ex.chain.sType=(WGPUSType)WGPUSType_InstanceExtras;ex.backends=WGPUInstanceBackend_Vulkan;
 WGPUInstanceDescriptor d={0};d.nextInChain=&ex.chain;WGPUInstance in=wgpuCreateInstance(&d);
 size_t n=wgpuInstanceEnumerateAdapters(in,NULL,NULL);WGPUAdapter a[8];wgpuInstanceEnumerateAdapters(in,NULL,a);
 printf("adapters %zu\n",n);
 for(size_t i=0;i<n;i++){WGPUAdapterInfo info={0};wgpuAdapterGetInfo(a[i],&info);
  printf("adapter %zu type=%d backend=%d vendor=0x%x device=0x%x\n",i,(int)info.adapterType,(int)info.backendType,info.vendorID,info.deviceID);
  pr("device",info.device);pr("description",info.description);pr("vendor",info.vendor);pr("architecture",info.architecture);
  WGPULimits l=WGPU_LIMITS_INIT;wgpuAdapterGetLimits(a[i],&l);
  printf("  maxStorageBuffersPerShaderStage=%u maxStorageBufferBindingSize=%llu maxBufferSize=%llu maxComputeWorkgroupSizeX=%u maxComputeInvocationsPerWorkgroup=%u maxComputeWorkgroupStorageSize=%u maxBindGroups=%u\n",
   l.maxStorageBuffersPerShaderStage,(unsigned long long)l.maxStorageBufferBindingSize,(unsigned long long)l.maxBufferSize,l.maxComputeWorkgroupSizeX,l.maxComputeInvocationsPerWorkgroup,l.maxComputeWorkgroupStorageSize,l.maxBindGroups);
  if(info.adapterType!=WGPUAdapterType_IntegratedGPU&&info.adapterType!=WGPUAdapterType_DiscreteGPU)continue;
  WGPULimits req=WGPU_LIMITS_INIT;uint64_t cap=1ull<<31;
  req.maxBufferSize=l.maxBufferSize<cap?l.maxBufferSize:cap;req.maxStorageBufferBindingSize=l.maxStorageBufferBindingSize<cap?l.maxStorageBufferBindingSize:cap;
  WGPUDeviceDescriptor dd={0};dd.requiredLimits=&req;WGPURequestDeviceCallbackInfo c={0};c.mode=WGPUCallbackMode_AllowProcessEvents;c.callback=cb;
  done=0;wgpuAdapterRequestDevice(a[i],&dd,c);while(!done)wgpuInstanceProcessEvents(in);
  if(dev){WGPULimits dl=WGPU_LIMITS_INIT;wgpuDeviceGetLimits(dev,&dl);printf("  device maxStorageBuffersPerShaderStage=%u (host requires >=7)\n",dl.maxStorageBuffersPerShaderStage);}
 }
 return 0;}
