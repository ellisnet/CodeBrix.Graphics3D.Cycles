// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "scene/scene.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/shader.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "scene/integrator.h"
#include "scene/camera.h"
#include "scene/background.h"
#include "scene/film.h"
#include "device/device.h"
#include "util/progress.h"
#include "util/profiling.h"
#include "util/stats.h"
#include "util/task.h"
#include "path_reference.h"
#include "../../prototype/native/demo_camera.h"
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

int main(int argc,char **argv) {
  if(argc!=2) { std::cerr<<"Usage: path_host OUTPUT_DIRECTORY\n"; return 2; }
  ccl::TaskScheduler::init(1); int result=0;
  try {
    using namespace ccl; using V=std::array<float,3>; using Row=std::array<float,4>;
    constexpr unsigned width=96,height=72,samples=16,bounces=4;
    Stats stats; Profiler profiler; DeviceInfo info; info.type=DEVICE_CPU; info.cpu_threads=1;
    info.id="CodeBrix original Cycles path reference"; info.description=info.id;
    auto device=Device::create(info,stats,profiler,true);
    SceneParams params; params.bvh_layout=BVH_LAYOUT_BVH2; params.bvh_type=BVH_TYPE_STATIC;
    params.use_bvh_unaligned_nodes=false; params.use_texture_cache=false;
    Scene scene(params,device.get());
    auto integrator=scene.integrator;
    integrator->set_min_bounce(bounces); integrator->set_max_bounce(bounces-1);
    integrator->set_max_diffuse_bounce(bounces-1); integrator->set_max_transmission_bounce(bounces-1);
    integrator->set_use_adaptive_sampling(false); integrator->set_use_light_tree(false);
    // No light objects or background importance map are created, so lighting
    // is sampled by forward paths. Keep direct contributions enabled.
    integrator->set_use_direct_light(true); integrator->set_use_guiding(false);
    integrator->set_sample_clamp_direct(0); integrator->set_sample_clamp_indirect(0);
    integrator->set_aa_samples(samples); integrator->set_seed(17);
    scene.film->set_use_sample_count(false);
    const auto cam=demo_camera(width,height);
    scene.camera->set_full_width(width); scene.camera->set_full_height(height);
    scene.camera->set_screen_size(width,height);
    scene.camera->set_fov(2*std::atan(cam.tan_half_fov));
    Transform transform;
    transform.x=make_float4(cam.right[0],cam.up[0],cam.forward[0],cam.origin[0]);
    transform.y=make_float4(cam.right[1],cam.up[1],cam.forward[1],cam.origin[1]);
    transform.z=make_float4(cam.right[2],cam.up[2],cam.forward[2],cam.origin[2]);
    scene.camera->set_matrix(transform); scene.camera->compute_auto_viewplane();
    const V background{.8f,.9f,1.f};
    auto bg_graph=make_unique<ShaderGraph>(); auto bg=bg_graph->create_node<BackgroundNode>();
    bg->set_color(make_float3(background[0],background[1],background[2])); bg->set_strength(1);
    bg_graph->connect(bg->output("Background"),bg_graph->output()->input("Surface"));
    auto bg_shader=scene.create_node<Shader>(); bg_shader->set_graph(std::move(bg_graph));
    bg_shader->tag_update(&scene); scene.background->set_shader(bg_shader);
    const std::array<V,5> colors={V{.65f,.65f,.65f},V{.8f,.08f,.03f},V{.08f,.7f,.13f},V{.06f,.2f,.8f},V{.85f,.8f,.3f}};
    const float roughness[]={0,.3f,.8f,0,0}; array<Node*> shaders;
    for(unsigned i=0;i<colors.size();i++) {
      auto graph=make_unique<ShaderGraph>(); BsdfNode *node;
      if(i==4) node=graph->create_node<TranslucentBsdfNode>();
      else { auto d=graph->create_node<DiffuseBsdfNode>(); d->set_roughness(roughness[i]); node=d; }
      node->set_color(make_float3(colors[i][0],colors[i][1],colors[i][2]));
      graph->connect(node->output("BSDF"),graph->output()->input("Surface"));
      auto shader=scene.create_node<Shader>(); shader->set_graph(std::move(graph)); shader->tag_update(&scene);
      shaders.push_back_slow(shader);
    }
    std::vector<V> points; std::vector<int> materials;
    auto triangle=[&](V a,V b,V c,int material) { points.insert(points.end(),{a,b,c}); materials.push_back(material); };
    auto quad=[&](V a,V b,V c,V d,int material) { triangle(a,b,c,material); triangle(a,c,d,material); };
    quad({-4,-3,0},{4,-3,0},{4,5,0},{-4,5,0},0);
    quad({-4,5,0},{4,5,0},{4,5,4},{-4,5,4},0);
    quad({-4,-3,0},{-4,5,0},{-4,5,4},{-4,-3,4},1);
    auto box=[&](float x,float y,float sx,float sy,float z,int material) {
      V a{x,y,0},b{x+sx,y,0},c{x+sx,y+sy,0},d{x,y+sy,0};
      V e{x,y,z},f{x+sx,y,z},g{x+sx,y+sy,z},h{x,y+sy,z};
      quad(a,d,c,b,material); quad(e,f,g,h,material); quad(a,b,f,e,material);
      quad(b,c,g,f,material); quad(c,d,h,g,material); quad(d,a,e,h,material);
    };
    box(-1.6f,-.3f,1.2f,1.2f,1.6f,1); box(.6f,.1f,1.1f,1.1f,.8f,3);
    box(-.2f,2.1f,.8f,.8f,2.4f,2);
    quad({1.6f,2.8f,.2f},{2.8f,2.8f,.2f},{2.8f,2.8f,2.2f},{1.6f,2.8f,2.2f},4);
    auto mesh=scene.create_node<Mesh>(); mesh->set_used_shaders(shaders); mesh->resize_mesh(points.size(),materials.size());
    for(size_t i=0;i<points.size();i++) {
      mesh->get_position_for_write()[i]=make_float3(points[i][0],points[i][1],points[i][2]); mesh->get_triangles()[i]=int(i);
    }
    std::ranges::fill(mesh->get_smooth(),false);
    std::copy(materials.begin(),materials.end(),mesh->get_shader().begin()); mesh->tag_triangles_modified();
    auto object=scene.create_node<Object>(); object->set_geometry(mesh); object->set_tfm(transform_identity());
    Progress progress; scene.device_update(device.get(),progress);
    if(device->have_error()||progress.get_error()) throw std::runtime_error(device->error_message());
    auto &d=scene.dscene;
    if(d.data.bvh.root!=0||d.bvh_nodes.size()%4||d.tri_shader.size()!=materials.size())
      throw std::runtime_error("Unsupported prepared scene layout");
    const size_t leaf=1+d.bvh_nodes.size(),indices=leaf+d.bvh_leaf_nodes.size(),vertices=indices+d.prim_index.size();
    const size_t metadata=vertices+4*materials.size(),code_base=(metadata+2)*4,words=d.svm_nodes.size();
    std::vector<Row> packed(metadata+2+(words+3)/4);
    packed[0]={float(materials.size()),float(leaf),float(indices),float(vertices)};
    std::memcpy(packed.data()+1,d.bvh_nodes.data(),d.bvh_nodes.size()*16);
    for(size_t i=1;i<leaf;i+=4) for(int j=2;j<4;j++) {
      int child=std::bit_cast<int>(packed[i][j]); if(child>=0) packed[i][j]=std::bit_cast<float>(child+1);
    }
    std::memcpy(packed.data()+leaf,d.bvh_leaf_nodes.data(),d.bvh_leaf_nodes.size()*16);
    for(size_t i=0;i<d.prim_index.size();i++) {
      if(d.prim_index[i]<0) throw std::runtime_error("Unexpected instance");
      packed[indices+i][0]=float(d.prim_index[i]);
    }
    for(size_t i=0;i<materials.size();i++) {
      auto ix=d.tri_vindex[i]; unsigned ids[]={ix.x,ix.y,ix.z};
      for(int j=0;j<3;j++) { auto p=d.tri_verts[ids[j]]; packed[vertices+3*i+j]={p.x,p.y,p.z,0}; }
      packed[vertices+3*materials.size()+i][3]=float(d.tri_shader[i]&SHADER_MASK);
    }
    const uint32_t header[]={uint32_t(words),uint32_t(code_base),bounces,5};
    std::memcpy(packed[metadata].data(),header,16);
    packed[metadata+1]={background[0],background[1],background[2],0};
    std::memcpy(packed.data()+metadata+2,d.svm_nodes.data(),words*4);
    const std::string directory=argv[1];
    std::ofstream file(directory+"/pathtrace.scene.bin",std::ios::binary);
    file.write(reinterpret_cast<const char*>(packed.data()),packed.size()*16); file.close();
    if(!file) throw std::runtime_error("Cannot save scene");
    render_path_reference(device.get(),directory,width,height,samples,bounces);
    std::cout<<"{\"width\":"<<width<<",\"height\":"<<height<<",\"samples\":"<<samples<<",\"bounces\":"<<bounces
      <<",\"triangles\":"<<materials.size()<<",\"svmWords\":"<<words<<",\"bytes\":"<<packed.size()*16
      <<",\"sharedNativeCameraAndSampler\":true,\"nextEventEstimation\":false,\"russianRoulette\":false}\n";
  } catch(const std::exception &e) { std::cerr<<e.what()<<'\n'; result=1; }
  ccl::TaskScheduler::exit(); return result;
}
