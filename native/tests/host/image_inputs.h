// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "scene/image_oiio.h"
#include "util/image_metadata.h"
#include "util/progress.h"
#include <OpenColorIO/OpenColorIO.h>
#include <OpenImageIO/imageio.h>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
#include <string>
#include <memory>
#include <cstring>
using namespace ccl;
namespace fs = std::filesystem;
namespace OCIO = OCIO_NAMESPACE;
static void require(bool okay,const std::string &message){if(!okay)throw std::runtime_error(message);}

struct FileImage {
  fs::path path;
  OIIO::TypeDesc type;
  int channels, width, height;
  bool lossless;
  std::vector<float> pixels;
};

// Deterministic fixture pixels for one format; the files below are written from them.
static std::vector<float> fixture_pixels(OIIO::TypeDesc type, int channels, int width, int height, unsigned seed) {
  std::vector<float> pixels(size_t(width)*height*channels);
  for (int y=0;y<height;y++) for (int x=0;x<width;x++) for (int c=0;c<channels;c++) {
    float value=float((x*17+y*31+c*47+seed*13)%256)/255.0f;
    if (type.is_floating_point()) value=float((x*7+y*11+c*19+seed)%97)/16.0f-.5f;
    if ((channels==2||channels==4)&&c==channels-1) value=float((x+y+seed)%5)/4;
    pixels[(size_t(y)*width+x)*channels+c]=value;
  }
  return pixels;
}

static FileImage write_image(const fs::path &path, OIIO::TypeDesc type,
                            int channels, int width, int height, unsigned seed) {
  FileImage image{path,type,channels,width,height,path.extension()!=".jpg",{}};
  image.pixels=fixture_pixels(type,channels,width,height,seed);
  auto output=OIIO::ImageOutput::create(path.string());
  require(bool(output),"Missing reviewed OIIO writer: "+path.string()+" "+OIIO::geterror());
  OIIO::ImageSpec spec(width,height,channels,type);
  spec.attribute("oiio:UnassociatedAlpha",1);
  spec.attribute("oiio:ColorSpace",type.is_floating_point()?"Linear":"sRGB");
  spec.attribute("CompressionQuality",100);
  require(output->open(path.string(),spec),"Cannot open fixture output: "+output->geterror());
  require(output->write_image(OIIO::TypeFloat,image.pixels.data()),"Cannot encode fixture: "+output->geterror());
  require(output->close(),"Cannot close fixture: "+output->geterror());
  return image;
}

