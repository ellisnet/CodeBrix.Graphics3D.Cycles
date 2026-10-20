// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "scene/image.h"
#include "scene/image_loader.h"
#include "util/image_metadata.h"
#include "util/half.h"
// Exercises Cycles' real ImageManager, conformance, cache allocation and upload.
class FixtureImageLoader final:public ccl::ImageLoader {
  unsigned id; ccl::ImageDataType type;unsigned width,height;
 public:
  FixtureImageLoader(unsigned id,ccl::ImageDataType type,unsigned width=7,unsigned height=5):id(id),type(type),width(width),height(height){}
  bool load_metadata(ccl::ImageMetaData &m,const ccl::ImageLoaderParams &p,ccl::Progress&)override{
    m.width=width;m.height=height;m.type=type;m.channels=(type==ccl::IMAGE_DATA_TYPE_FLOAT4||type==ccl::IMAGE_DATA_TYPE_HALF4||type==ccl::IMAGE_DATA_TYPE_BYTE4||type==ccl::IMAGE_DATA_TYPE_USHORT4)?4:1;
    m.colorspace=p.colorspace;m.finalize(p.alpha_type);return true;
  }
  bool load_pixels(const ccl::ImageMetaData &m,void *pixels)override{
    using namespace ccl;const size_t n=m.width*m.height*m.channels;
    for(size_t i=0;i<n;i++){
      float v=float((i*31+id*7)%251)/250;
      if(m.channels==4&&i%4==3)v=float((i/4)%5)/4;
      if((m.is_float()||m.is_half())&&(m.channels==1||i%4!=3)){if(i%11==0)v=-.3f;else if(i%13==0)v=3.5f;else if(i%17==0)v=1e-6f;}
      if(m.type==IMAGE_DATA_TYPE_FLOAT4||m.type==IMAGE_DATA_TYPE_FLOAT)static_cast<float*>(pixels)[i]=v;
      else if(m.type==IMAGE_DATA_TYPE_HALF4||m.type==IMAGE_DATA_TYPE_HALF)static_cast<half*>(pixels)[i]=float_to_half_image(v);
      else if(m.type==IMAGE_DATA_TYPE_BYTE4||m.type==IMAGE_DATA_TYPE_BYTE)static_cast<uchar*>(pixels)[i]=uchar(v*255);
      else if(m.type==IMAGE_DATA_TYPE_USHORT4||m.type==IMAGE_DATA_TYPE_USHORT)static_cast<uint16_t*>(pixels)[i]=uint16_t(v*65535);
      else return false;
    }
    m.conform_pixels(pixels);return true;
  }
  ccl::string name()const override{return "Native image fixture "+std::to_string(id);}
  bool equals(const ccl::ImageLoader &other)const override{
    auto p=dynamic_cast<const FixtureImageLoader*>(&other);return p&&p->id==id&&p->type==type&&p->width==width&&p->height==height;
  }
};
