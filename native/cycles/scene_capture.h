// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
// Replay capture of an oracle scene (native/tools/oracle_replay.py): CODEBRIX_CYCLES_RENDER_CAPTURE=DIR and
// CODEBRIX_CYCLES_RENDER_CAPTURE_SIZE=WxHxN[xS]. The render's native scheduler is recorded by RenderCapture from the
// first camera kernel after S whole samples (default 1: lazily allocated records exist by then; an arena that still
// grows is refused by RenderCapture) to the first read of the render buffer after the last sample's paths are done, so
// that the scheduler's filters at the end of the update are in the capture. Unset, a queue never creates this object.
#include "render_capture.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
namespace ccl {
class SceneCapture {
  WebGPUDevice &d;std::string dir;unsigned w=0,h=0,n=0,start=1;uint64_t queued_work=0;device_ptr film=0;std::unique_ptr<RenderCapture> capture;
  static bool &done(){static bool value=false;return value;}
  void note(const std::string &text){std::ofstream(dir+"/capture-error.txt")<<text<<'\n';}
 public:
  explicit SceneCapture(WebGPUDevice &device):d(device),dir(std::getenv("CODEBRIX_CYCLES_RENDER_CAPTURE")){
    const char *size=std::getenv("CODEBRIX_CYCLES_RENDER_CAPTURE_SIZE");
    if(!size||std::sscanf(size,"%ux%ux%ux%u",&w,&h,&n,&start)<3||!w||!h||!n)throw std::runtime_error("CODEBRIX_CYCLES_RENDER_CAPTURE_SIZE must be WxHxN[xS]");
    d.capture_sizing=true;
  }
  void enqueue(DeviceKernel kernel,int work,const DeviceKernelArguments &args){
    if(done())return;
    if(kernel==DEVICE_KERNEL_INTEGRATOR_INTERSECT_CLOSEST&&args.count>=2)std::memcpy(&film,args.values[1],sizeof(film));
    if(kernel==DEVICE_KERNEL_INTEGRATOR_INIT_FROM_CAMERA){
      if(!capture&&queued_work>=uint64_t(w)*h*start){d.flush();capture=std::make_unique<RenderCapture>(d,dir+"/run");}
      queued_work+=uint64_t(work);
    }
  }
  // The scheduler reads the render buffer for the first time once the update is over.
  void before_read(const device_memory &mem){
    if(done()||!capture||queued_work<uint64_t(w)*h*n||mem.device_pointer!=film)return;
    done()=true;
    const unsigned stride=d.data.film.pass_stride;const uint32_t queued=uint32_t(uintptr_t(d.state.path.queued_kernel));
    d.flush();
    try{capture->finish(dir+"/run",uint32_t(film),size_t(w)*h*stride*4,queued,unsigned(d.remaining(queued)/2),w,h,n,stride);}
    catch(const std::exception &e){note(e.what());}
    capture.reset();
  }
};
}
