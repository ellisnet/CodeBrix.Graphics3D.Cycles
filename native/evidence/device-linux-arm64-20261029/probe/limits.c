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

static WGPULimits get(WGPUInstance in,WGPUAdapter a,WGPULimits*req){WGPUDeviceDescriptor dd={0};dd.requiredLimits=req;WGPURequestDeviceCallbackInfo c={0};
 c.mode=WGPUCallbackMode_AllowProcessEvents;c.callback=cb;done=0;dev=NULL;wgpuAdapterRequestDevice(a,&dd,c);while(!done)wgpuInstanceProcessEvents(in);
 WGPULimits l=WGPU_LIMITS_INIT;if(dev)wgpuDeviceGetLimits(dev,&l);return l;}
int main(void){
 WGPUInstanceExtras ex={0};ex.chain.sType=(WGPUSType)WGPUSType_InstanceExtras;ex.backends=WGPUInstanceBackend_Vulkan;
 WGPUInstanceDescriptor d={0};d.nextInChain=&ex.chain;WGPUInstance in=wgpuCreateInstance(&d);
 WGPUAdapter a[8];wgpuInstanceEnumerateAdapters(in,NULL,a);
 WGPULimits ad=WGPU_LIMITS_INIT;wgpuAdapterGetLimits(a[0],&ad);
 WGPULimits r0=WGPU_LIMITS_INIT;uint64_t cap=1ull<<31;r0.maxBufferSize=ad.maxBufferSize<cap?ad.maxBufferSize:cap;r0.maxStorageBufferBindingSize=ad.maxStorageBufferBindingSize<cap?ad.maxStorageBufferBindingSize:cap;
 r0.maxStorageBuffersPerShaderStage=ad.maxStorageBuffersPerShaderStage<8?ad.maxStorageBuffersPerShaderStage:8;
 r0.maxUniformBufferBindingSize=ad.maxUniformBufferBindingSize<65536?ad.maxUniformBufferBindingSize:65536;
 r0.maxComputeWorkgroupStorageSize=ad.maxComputeWorkgroupStorageSize<16384?ad.maxComputeWorkgroupStorageSize:16384;
 WGPULimits d0=get(in,a[0],&r0);WGPULimits d1=get(in,a[0],NULL);
 printf("%-40s %20s %20s %20s\n","limit","adapter","device(host req)","device(no req)");
  printf("%-40s %20llu %20llu %20llu\n","maxTextureDimension1D",(unsigned long long)ad.maxTextureDimension1D,(unsigned long long)d0.maxTextureDimension1D,(unsigned long long)d1.maxTextureDimension1D);
  printf("%-40s %20llu %20llu %20llu\n","maxTextureDimension2D",(unsigned long long)ad.maxTextureDimension2D,(unsigned long long)d0.maxTextureDimension2D,(unsigned long long)d1.maxTextureDimension2D);
  printf("%-40s %20llu %20llu %20llu\n","maxTextureDimension3D",(unsigned long long)ad.maxTextureDimension3D,(unsigned long long)d0.maxTextureDimension3D,(unsigned long long)d1.maxTextureDimension3D);
  printf("%-40s %20llu %20llu %20llu\n","maxTextureArrayLayers",(unsigned long long)ad.maxTextureArrayLayers,(unsigned long long)d0.maxTextureArrayLayers,(unsigned long long)d1.maxTextureArrayLayers);
  printf("%-40s %20llu %20llu %20llu\n","maxBindGroups",(unsigned long long)ad.maxBindGroups,(unsigned long long)d0.maxBindGroups,(unsigned long long)d1.maxBindGroups);
  printf("%-40s %20llu %20llu %20llu\n","maxBindGroupsPlusVertexBuffers",(unsigned long long)ad.maxBindGroupsPlusVertexBuffers,(unsigned long long)d0.maxBindGroupsPlusVertexBuffers,(unsigned long long)d1.maxBindGroupsPlusVertexBuffers);
  printf("%-40s %20llu %20llu %20llu\n","maxBindingsPerBindGroup",(unsigned long long)ad.maxBindingsPerBindGroup,(unsigned long long)d0.maxBindingsPerBindGroup,(unsigned long long)d1.maxBindingsPerBindGroup);
  printf("%-40s %20llu %20llu %20llu\n","maxDynamicUniformBuffersPerPipelineLayout",(unsigned long long)ad.maxDynamicUniformBuffersPerPipelineLayout,(unsigned long long)d0.maxDynamicUniformBuffersPerPipelineLayout,(unsigned long long)d1.maxDynamicUniformBuffersPerPipelineLayout);
  printf("%-40s %20llu %20llu %20llu\n","maxDynamicStorageBuffersPerPipelineLayout",(unsigned long long)ad.maxDynamicStorageBuffersPerPipelineLayout,(unsigned long long)d0.maxDynamicStorageBuffersPerPipelineLayout,(unsigned long long)d1.maxDynamicStorageBuffersPerPipelineLayout);
  printf("%-40s %20llu %20llu %20llu\n","maxSampledTexturesPerShaderStage",(unsigned long long)ad.maxSampledTexturesPerShaderStage,(unsigned long long)d0.maxSampledTexturesPerShaderStage,(unsigned long long)d1.maxSampledTexturesPerShaderStage);
  printf("%-40s %20llu %20llu %20llu\n","maxSamplersPerShaderStage",(unsigned long long)ad.maxSamplersPerShaderStage,(unsigned long long)d0.maxSamplersPerShaderStage,(unsigned long long)d1.maxSamplersPerShaderStage);
  printf("%-40s %20llu %20llu %20llu\n","maxStorageBuffersPerShaderStage",(unsigned long long)ad.maxStorageBuffersPerShaderStage,(unsigned long long)d0.maxStorageBuffersPerShaderStage,(unsigned long long)d1.maxStorageBuffersPerShaderStage);
  printf("%-40s %20llu %20llu %20llu\n","maxStorageTexturesPerShaderStage",(unsigned long long)ad.maxStorageTexturesPerShaderStage,(unsigned long long)d0.maxStorageTexturesPerShaderStage,(unsigned long long)d1.maxStorageTexturesPerShaderStage);
  printf("%-40s %20llu %20llu %20llu\n","maxUniformBuffersPerShaderStage",(unsigned long long)ad.maxUniformBuffersPerShaderStage,(unsigned long long)d0.maxUniformBuffersPerShaderStage,(unsigned long long)d1.maxUniformBuffersPerShaderStage);
  printf("%-40s %20llu %20llu %20llu\n","maxUniformBufferBindingSize",(unsigned long long)ad.maxUniformBufferBindingSize,(unsigned long long)d0.maxUniformBufferBindingSize,(unsigned long long)d1.maxUniformBufferBindingSize);
  printf("%-40s %20llu %20llu %20llu\n","maxStorageBufferBindingSize",(unsigned long long)ad.maxStorageBufferBindingSize,(unsigned long long)d0.maxStorageBufferBindingSize,(unsigned long long)d1.maxStorageBufferBindingSize);
  printf("%-40s %20llu %20llu %20llu\n","minUniformBufferOffsetAlignment",(unsigned long long)ad.minUniformBufferOffsetAlignment,(unsigned long long)d0.minUniformBufferOffsetAlignment,(unsigned long long)d1.minUniformBufferOffsetAlignment);
  printf("%-40s %20llu %20llu %20llu\n","minStorageBufferOffsetAlignment",(unsigned long long)ad.minStorageBufferOffsetAlignment,(unsigned long long)d0.minStorageBufferOffsetAlignment,(unsigned long long)d1.minStorageBufferOffsetAlignment);
  printf("%-40s %20llu %20llu %20llu\n","maxVertexBuffers",(unsigned long long)ad.maxVertexBuffers,(unsigned long long)d0.maxVertexBuffers,(unsigned long long)d1.maxVertexBuffers);
  printf("%-40s %20llu %20llu %20llu\n","maxBufferSize",(unsigned long long)ad.maxBufferSize,(unsigned long long)d0.maxBufferSize,(unsigned long long)d1.maxBufferSize);
  printf("%-40s %20llu %20llu %20llu\n","maxVertexAttributes",(unsigned long long)ad.maxVertexAttributes,(unsigned long long)d0.maxVertexAttributes,(unsigned long long)d1.maxVertexAttributes);
  printf("%-40s %20llu %20llu %20llu\n","maxVertexBufferArrayStride",(unsigned long long)ad.maxVertexBufferArrayStride,(unsigned long long)d0.maxVertexBufferArrayStride,(unsigned long long)d1.maxVertexBufferArrayStride);
  printf("%-40s %20llu %20llu %20llu\n","maxInterStageShaderVariables",(unsigned long long)ad.maxInterStageShaderVariables,(unsigned long long)d0.maxInterStageShaderVariables,(unsigned long long)d1.maxInterStageShaderVariables);
  printf("%-40s %20llu %20llu %20llu\n","maxColorAttachments",(unsigned long long)ad.maxColorAttachments,(unsigned long long)d0.maxColorAttachments,(unsigned long long)d1.maxColorAttachments);
  printf("%-40s %20llu %20llu %20llu\n","maxColorAttachmentBytesPerSample",(unsigned long long)ad.maxColorAttachmentBytesPerSample,(unsigned long long)d0.maxColorAttachmentBytesPerSample,(unsigned long long)d1.maxColorAttachmentBytesPerSample);
  printf("%-40s %20llu %20llu %20llu\n","maxComputeWorkgroupStorageSize",(unsigned long long)ad.maxComputeWorkgroupStorageSize,(unsigned long long)d0.maxComputeWorkgroupStorageSize,(unsigned long long)d1.maxComputeWorkgroupStorageSize);
  printf("%-40s %20llu %20llu %20llu\n","maxComputeInvocationsPerWorkgroup",(unsigned long long)ad.maxComputeInvocationsPerWorkgroup,(unsigned long long)d0.maxComputeInvocationsPerWorkgroup,(unsigned long long)d1.maxComputeInvocationsPerWorkgroup);
  printf("%-40s %20llu %20llu %20llu\n","maxComputeWorkgroupSizeX",(unsigned long long)ad.maxComputeWorkgroupSizeX,(unsigned long long)d0.maxComputeWorkgroupSizeX,(unsigned long long)d1.maxComputeWorkgroupSizeX);
  printf("%-40s %20llu %20llu %20llu\n","maxComputeWorkgroupSizeY",(unsigned long long)ad.maxComputeWorkgroupSizeY,(unsigned long long)d0.maxComputeWorkgroupSizeY,(unsigned long long)d1.maxComputeWorkgroupSizeY);
  printf("%-40s %20llu %20llu %20llu\n","maxComputeWorkgroupSizeZ",(unsigned long long)ad.maxComputeWorkgroupSizeZ,(unsigned long long)d0.maxComputeWorkgroupSizeZ,(unsigned long long)d1.maxComputeWorkgroupSizeZ);
  printf("%-40s %20llu %20llu %20llu\n","maxComputeWorkgroupsPerDimension",(unsigned long long)ad.maxComputeWorkgroupsPerDimension,(unsigned long long)d0.maxComputeWorkgroupsPerDimension,(unsigned long long)d1.maxComputeWorkgroupsPerDimension);
  printf("%-40s %20llu %20llu %20llu\n","maxImmediateSize",(unsigned long long)ad.maxImmediateSize,(unsigned long long)d0.maxImmediateSize,(unsigned long long)d1.maxImmediateSize);
 return 0;}
