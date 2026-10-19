// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "scene/scene.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "device/device.h"
#include "util/progress.h"
#include "util/profiling.h"
#include "util/stats.h"
#include "util/task.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>
#include "../../parity/native/import_triangles.h"
using V=std::array<float,3>;
using Row=std::array<float,4>;
static void require(bool ok,const char *message) { if(!ok) throw std::runtime_error(message); }
int main(int argc,char **argv) {
  if(argc!=2 && argc!=3) { std::cerr<<"Usage: codebrix_cycles_host_probe OUTPUT_SCENE [IMPORTED_TRIANGLES]\n"; return 2; }
  ccl::TaskScheduler::init(4);
  int result=0;
  try {
    ccl::Stats stats; ccl::Profiler profiler; ccl::DeviceInfo info;
    info.type=ccl::DEVICE_CPU; info.id="CodeBrix host preparation reference"; info.description=info.id;
    auto device=ccl::Device::create(info,stats,profiler,true);
    ccl::SceneParams params; params.bvh_layout=ccl::BVH_LAYOUT_BVH2;
    params.bvh_type=ccl::BVH_TYPE_STATIC;
    params.use_bvh_unaligned_nodes=false; params.use_texture_cache=false;
    ccl::Scene scene(params,device.get());
    std::vector<V> points,colors;
    auto triangle=[&](V a,V b,V c,V color) { points.insert(points.end(),{a,b,c}); colors.push_back(color); };
    auto quad=[&](V a,V b,V c,V d,V color) { triangle(a,b,c,color); triangle(a,c,d,color); };
    quad({-5,-4,0},{5,-4,0},{5,5,0},{-5,5,0},{.65f,.65f,.65f});
    auto box=[&](float x,float y,float z,float sx,float sy,float sz,V color) {
      V a{x,y,z},b{x+sx,y,z},c{x+sx,y+sy,z},d{x,y+sy,z};
      V e{x,y,z+sz},f{x+sx,y,z+sz},g{x+sx,y+sy,z+sz},h{x,y+sy,z+sz};
      quad(a,d,c,b,color); quad(e,f,g,h,color); quad(a,b,f,e,color);
      quad(b,c,g,f,color); quad(c,d,h,g,color); quad(d,a,e,h,color);
    };
    if(argc==3) {
      const auto imported=import_test_triangles(argv[2]);
      for(size_t i=0;i<imported.size();i+=3)
        triangle(imported[i],imported[i+1],imported[i+2],{.6f,.6f,.6f});
    } else {
      box(-1.7f,-.4f,0,1.4f,1.4f,1.8f,{.75f,.12f,.06f});
      box(.5f,.2f,0,1.2f,1.2f,.8f,{.08f,.32f,.8f});
      box(-.25f,2,0,.75f,.75f,2.4f,{.15f,.65f,.2f});
    }
    auto mesh=scene.create_node<ccl::Mesh>();
    ccl::array<ccl::Node*> shaders; shaders.push_back_slow(scene.default_surface); mesh->set_used_shaders(shaders);
    mesh->resize_mesh(points.size(),colors.size());
    for(size_t i=0;i<points.size();i++) {
      mesh->get_position_for_write()[i]=ccl::make_float3(points[i][0],points[i][1],points[i][2]);
      mesh->get_triangles()[i]=int(i);
    }
    std::ranges::fill(mesh->get_smooth(),false); std::ranges::fill(mesh->get_shader(),0);
    mesh->tag_triangles_modified();
    auto object=scene.create_node<ccl::Object>(); object->set_geometry(mesh); object->set_tfm(ccl::transform_identity());
    ccl::Progress progress; scene.device_update(device.get(),progress);
    require(!device->have_error()&&!progress.get_error(),device->error_message().c_str());
    auto &d=scene.dscene;
    require(d.data.bvh.root==0,"Prototype requires an internal BVH root at zero");
    require(d.bvh_nodes.size()%4==0,"Prototype requires aligned BVH2 nodes");
    require(d.tri_vindex.size()==colors.size()&&d.tri_verts.size()==points.size(),"Unexpected Cycles geometry packing");
    size_t leaf=1+d.bvh_nodes.size(), index=leaf+d.bvh_leaf_nodes.size(), vertex=index+d.prim_index.size();
    std::vector<Row> packed(vertex+4*colors.size());
    packed[0]={float(colors.size()),float(leaf),float(index),float(vertex)};
    // Preserve Cycles' BVH and leaf bytes; only relocate positive child offsets
    // by one row to make space for this experiment's table directory.
    std::memcpy(packed.data()+1,d.bvh_nodes.data(),d.bvh_nodes.size()*16);
    for(size_t i=1;i<leaf;i+=4) for(int j=2;j<4;j++) {
      int child=std::bit_cast<int>(packed[i][j]);
      if(child>=0) packed[i][j]=std::bit_cast<float>(child+1);
    }
    std::memcpy(packed.data()+leaf,d.bvh_leaf_nodes.data(),d.bvh_leaf_nodes.size()*16);
    for(size_t i=0;i<d.prim_index.size();i++) {
      require(d.prim_index[i]>=0,"Instancing is outside this prototype");
      packed[index+i][0]=float(d.prim_index[i]);
    }
    for(size_t i=0;i<colors.size();i++) {
      auto indices=d.tri_vindex[i]; unsigned ix[]={indices.x,indices.y,indices.z};
      for(int j=0;j<3;j++) {
        require(ix[j]<d.tri_verts.size(),"Invalid Cycles vertex offset");
        auto p=d.tri_verts[ix[j]]; packed[vertex+3*i+j]={p.x,p.y,p.z,0};
      }
      packed[vertex+3*colors.size()+i]={colors[i][0],colors[i][1],colors[i][2],0};
    }
    std::ofstream output(argv[1],std::ios::binary); output.write(reinterpret_cast<const char*>(packed.data()),packed.size()*16);
    require(bool(output),"Cannot write packed scene");
    std::cout<<"{\"originalCyclesScene\":true,\"bvhRoot\":"<<d.data.bvh.root
      <<",\"nodeRows\":"<<d.bvh_nodes.size()<<",\"leaves\":"<<d.bvh_leaf_nodes.size()
      <<",\"primitiveSlots\":"<<d.prim_index.size()<<",\"triangles\":"<<colors.size()
      <<",\"bytes\":"<<packed.size()*16<<"}\n";
  } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; result=1; }
  ccl::TaskScheduler::exit(); return result;
}
