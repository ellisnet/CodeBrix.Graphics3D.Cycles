// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

// Deliberately narrow test interchange, not a public asset API. BinaryWriter
// writes little endian; the native prototype currently runs on little endian.
inline std::vector<std::array<float,3>> import_test_triangles(const char *path) {
  std::ifstream in(path,std::ios::binary|std::ios::ate);
  if(!in) throw std::runtime_error("Cannot open imported triangles");
  const auto bytes=in.tellg(); in.seekg(0);
  char magic[8]{}; uint32_t count=0;
  in.read(magic,8); in.read(reinterpret_cast<char*>(&count),4);
  if(std::memcmp(magic,"CBCTRI1\0",8)!=0 || count==0 || count>500000 || bytes!=12+uint64_t(count)*36)
    throw std::runtime_error("Invalid triangle interchange header/length");
  std::vector<std::array<float,3>> points(size_t(count)*3);
  in.read(reinterpret_cast<char*>(points.data()),count*36);
  if(!in) throw std::runtime_error("Truncated triangle interchange");
  std::array<float,3> lo,hi;
  lo.fill(std::numeric_limits<float>::infinity()); hi.fill(-std::numeric_limits<float>::infinity());
  for(auto &p:points) for(int j=0;j<3;j++) {
    if(!std::isfinite(p[j])) throw std::runtime_error("Nonfinite imported position");
    lo[j]=std::min(lo[j],p[j]); hi[j]=std::max(hi[j],p[j]);
  }
  const float extent=std::max({hi[0]-lo[0],hi[1]-lo[1],hi[2]-lo[2]});
  if(!std::isfinite(extent)||extent<=0) throw std::runtime_error("Invalid imported bounds");
  // Uniformly fit the model to four units, ground it, and rotate Y-up to Z-up.
  // Rotation (x,y,z)->(x,-z,y) preserves handedness and triangle winding.
  const float cx=(lo[0]+hi[0])*.5f,cz=(lo[2]+hi[2])*.5f;
  // Use the original multiply/divide order for the checkpoint's float rounding.
  for(auto &p:points) {
    p={(p[0]-cx)*4/extent,-(p[2]-cz)*4/extent+1,(p[1]-lo[1])*4/extent};
    for(float value:p) if(!std::isfinite(value)) throw std::runtime_error("Imported presentation transform overflowed");
  }
  return points;
}
