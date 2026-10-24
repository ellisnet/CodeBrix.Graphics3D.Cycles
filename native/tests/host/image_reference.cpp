// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "device/device.h"
#include "scene/scene.h"
#include "scene/background.h"
#include "scene/camera.h"
#include "scene/film.h"
#include "scene/integrator.h"
#include "scene/mesh.h"
#include "scene/object.h"
#include "scene/pass.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "scene/attribute.h"
#include "image_inputs.h"
#include <filesystem>
#include "session/output_driver.h"
#include "session/session.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using namespace ccl;
static constexpr int width = 32, height = 24, samples = 8;

struct Output {
  std::vector<float> pixels;
  unsigned writes = 0;
  bool valid = false;
};

class SessionImageOutput final : public OutputDriver {
  std::shared_ptr<Output> output;
 public:
  explicit SessionImageOutput(std::shared_ptr<Output> value) : output(std::move(value)) {}
  void write_render_tile(const Tile &tile) override {
    output->writes++;
    output->pixels.assign(width * height * 4, -1000.0f);
    output->valid = tile.offset.x == 0 && tile.offset.y == 0 &&
                    tile.size.x == width && tile.size.y == height &&
                    tile.get_pass_pixels("Combined", 4, output->pixels.data());
  }
};

struct Material {
  Shader *shader;
  DiffuseBsdfNode *diffuse;
};

static Material prepare(Scene &scene) {
  scene.integrator->set_use_guiding(false);
  scene.integrator->set_use_light_tree(false);
  scene.integrator->set_use_adaptive_sampling(false);
  scene.integrator->set_use_denoise(false);
  scene.integrator->set_sampling_pattern(SAMPLING_PATTERN_TABULATED_SOBOL);
  scene.integrator->set_min_bounce(2);
  scene.integrator->set_max_bounce(4);
  scene.integrator->set_max_diffuse_bounce(4);
  scene.integrator->set_caustics_reflective(false);
  scene.integrator->set_caustics_refractive(false);
  scene.camera->set_full_width(width);
  scene.camera->set_full_height(height);
  scene.camera->set_screen_size(width, height);
  scene.camera->set_fov(1.1f);
  scene.camera->set_matrix(transform_translate(make_float3(0, 0, -4)));
  scene.camera->compute_auto_viewplane();
  for (auto pass : scene.passes) {
    if (pass->get_type() == PASS_COMBINED) pass->set_name(ustring("Combined"));
  }

  auto graph = make_unique<ShaderGraph>();
  auto diffuse = graph->create_node<DiffuseBsdfNode>();
  diffuse->set_color(make_float3(.7f, .2f, .1f));
  diffuse->set_roughness(.3f);
  graph->connect(diffuse->output("BSDF"), graph->output()->input("Surface"));
  auto shader = scene.create_node<Shader>();
  shader->set_graph(std::move(graph));
  shader->tag_update(&scene);

  auto mesh = scene.create_node<Mesh>();
  array<Node *> materials;
  materials.push_back_slow(shader);
  mesh->set_used_shaders(materials);
  mesh->resize_mesh(3, 1);
  mesh->get_position_for_write()[0] = make_float3(-1.5f, -1, 0);
  mesh->get_position_for_write()[1] = make_float3(1.5f, -1, 0);
  mesh->get_position_for_write()[2] = make_float3(0, 1.5f, 0);
  for (int i = 0; i != 3; ++i) mesh->get_triangles()[i] = i;
  mesh->get_shader()[0] = 0;
  mesh->get_smooth()[0] = false;
  mesh->tag_triangles_modified();
  scene.create_node<Object>()->set_geometry(mesh);

  auto world = make_unique<ShaderGraph>();
  auto background = world->create_node<BackgroundNode>();
  background->set_color(make_float3(.25f, .4f, .6f));
  background->set_strength(.8f);
  world->connect(background->output("Background"), world->output()->input("Surface"));
  auto world_shader = scene.create_node<Shader>();
  world_shader->set_graph(std::move(world));
  world_shader->tag_update(&scene);
  scene.background->set_shader(world_shader);
  for (auto item : scene.shaders) item->set_emission_sampling_method(EMISSION_SAMPLING_NONE);
  return {shader, diffuse};
}

// The original native file loader decodes pixels and performs final conformance.
// Clear file-only hints to model caller-provided raw memory for the first cases.
class RawFileLoader final : public ImageLoader {
  std::string path;
  OIIOImageLoader original;
 public:
  explicit RawFileLoader(const fs::path &name):path(name.string()),original(path){}
  bool load_metadata(ImageMetaData &metadata,const ImageLoaderParams &params,Progress &progress) override {
    if(!original.load_metadata(metadata,params,progress))return false;
    metadata.colorspace_file_hint="";metadata.colorspace_file_format="";metadata.is_unassociated_alpha=false;return true;
  }
  bool load_pixels(const ImageMetaData &metadata,void *pixels) override {return original.load_pixels(metadata,pixels);}
  string name() const override {return path;}
  bool equals(const ImageLoader &other) const override {auto p=dynamic_cast<const RawFileLoader *>(&other);return p&&p->path==path;}
};

#ifdef CBC_HOST_NO_IMAGE_FILES
// Builds without image file readers (osx-arm64): the same fixture pixels, converted to each
// format as the file writers convert them, are served from memory exactly as the C host's
// caller images are (metadata from the caller, then native conformance).
class RawBytesLoader final : public ImageLoader {
  ImageDataType type;int channels;std::vector<uint8_t> bytes;std::string label;
 public:
  RawBytesLoader(ImageDataType t,int c,std::vector<uint8_t> b,std::string l):type(t),channels(c),bytes(std::move(b)),label(std::move(l)){}
  bool load_metadata(ImageMetaData &metadata,const ImageLoaderParams &params,Progress &) override {
    metadata.width=7;metadata.height=5;metadata.type=type;metadata.channels=channels;metadata.colorspace=params.colorspace;return true;
  }
  bool load_pixels(const ImageMetaData &metadata,void *pixels) override {
    // As the C host's memory loader: the stored type may differ from the caller's (for
    // example after sRGB finalization), in which case the pixels are converted.
    if(metadata.width!=7||metadata.height!=5||metadata.channels!=channels||metadata.is_rgba()!=(channels==4))return false;
    if(metadata.type==type)std::memcpy(pixels,bytes.data(),bytes.size());
    else {
      ImageMetaData source;source.type=type;
      if(!OIIO::convert_image(channels,7,5,1,bytes.data(),source.typedesc(),OIIO::AutoStride,OIIO::AutoStride,OIIO::AutoStride,
                              pixels,metadata.typedesc(),OIIO::AutoStride,OIIO::AutoStride,OIIO::AutoStride))return false;
    }
    metadata.conform_pixels(pixels);return true;
  }
  string name() const override {return label;}
  bool equals(const ImageLoader &other) const override {auto p=dynamic_cast<const RawBytesLoader *>(&other);return p&&p->label==label;}
};
#endif

int main(int argc,char **argv) {
  try {
    require(argc==3,"Usage: image_reference CONFIG OUTPUT_DIRECTORY");const fs::path out=argv[2];fs::create_directories(out);
    auto config=OCIO::Config::CreateFromFile(argv[1]);config->validate();OCIO::SetCurrentConfig(config);
    fs::copy_file(argv[1],out/"config.ocio");
    const OIIO::TypeDesc formats[]={OIIO::TypeFloat,OIIO::TypeHalf,OIIO::TypeUInt8,OIIO::TypeUInt16};
    const ustring spaces[]={u_colorspace_data,u_colorspace_scene_linear,u_colorspace_scene_linear_srgb,u_colorspace_srgb,u_colorspace_auto,ustring("cbc_scaled")};
    std::vector<fs::path> files;
#ifdef CBC_HOST_NO_IMAGE_FILES
    const ImageDataType types[]={IMAGE_DATA_TYPE_FLOAT,IMAGE_DATA_TYPE_FLOAT4,IMAGE_DATA_TYPE_HALF,IMAGE_DATA_TYPE_HALF4,
                                 IMAGE_DATA_TYPE_BYTE,IMAGE_DATA_TYPE_BYTE4,IMAGE_DATA_TYPE_USHORT,IMAGE_DATA_TYPE_USHORT4};
    std::vector<std::vector<uint8_t>> inputs;
    for(unsigned format=0;format<8;++format) {
      const int channels=format%2?4:1;const OIIO::TypeDesc type=formats[format/2];
      auto values=fixture_pixels(type,channels,7,5,format+17);std::vector<uint8_t> bytes(values.size()*type.size());
      require(OIIO::convert_pixel_values(OIIO::TypeFloat,values.data(),type,bytes.data(),int(values.size())),"Fixture conversion");
      std::ofstream raw(out/("input."+std::to_string(format)+".bin"),std::ios::binary);raw.write(reinterpret_cast<const char *>(bytes.data()),bytes.size());require(raw.good(),"Raw pixel write");
      inputs.push_back(std::move(bytes));
    }
    auto loader=[&](unsigned format)->unique_ptr<ImageLoader>{
      return make_unique<RawBytesLoader>(types[format],format%2?4:1,inputs[format],"CodeBrix fixture image "+std::to_string(format));};
#else
    Progress progress;std::atomic<int> failures=0,tx_failures=0;
    ImageLoaderParams loading{.use_texture_cache=false,.auto_texture_cache=false,.texture_cache_path="",.colorspace=u_colorspace_data,.alpha_type=IMAGE_ALPHA_CHANNEL_PACKED,.load_failure_num=failures,.tx_failure_num=tx_failures};
    for(unsigned format=0;format<8;++format) {
      const unsigned channels=format%2?4:1;
      auto path=out/("input."+std::to_string(format)+(format<4?".exr":".png"));
      write_image(path,formats[format/2],channels,7,5,format+17);files.push_back(path);
      OIIOImageLoader loader(path.string());ImageMetaData metadata;metadata.colorspace=u_colorspace_data;
      require(loader.load_metadata(metadata,loading,progress),"Raw metadata");metadata.finalize(IMAGE_ALPHA_CHANNEL_PACKED);
      std::vector<uint8_t> pixels(metadata.memory_size());require(loader.load_pixels(metadata,pixels.data()),"Raw native pixels");
      std::ofstream raw(out/("input."+std::to_string(format)+".bin"),std::ios::binary);raw.write(reinterpret_cast<const char *>(pixels.data()),pixels.size());require(raw.good(),"Raw pixel write");
    }
    auto loader=[&](unsigned format)->unique_ptr<ImageLoader>{return make_unique<RawFileLoader>(files[format]);};
#endif
    auto devices=Device::available_devices(DEVICE_MASK_CPU);require(!devices.empty(),"CPU unavailable");
    unsigned completed=0;
    auto render=[&](unsigned format,ustring colorspace,ImageAlphaType alpha,int environment,bool filename) {
      SessionParams params;params.device=devices[0];params.denoise_device=params.device;params.headless=true;params.background=true;
      params.samples=samples;params.threads=1;params.use_auto_tile=false;params.use_resolution_divider=false;
      SceneParams scene_params;scene_params.bvh_layout=BVH_LAYOUT_BVH2;scene_params.bvh_type=BVH_TYPE_STATIC;scene_params.use_bvh_unaligned_nodes=false;scene_params.use_texture_cache=false;
      Session session(params,scene_params);auto material=prepare(*session.scene);auto &scene=*session.scene;
      if(environment<0) {
        auto &graph=*material.shader->graph;auto uv=graph.create_node<UVMapNode>();auto texture=graph.create_node<ImageTextureNode>();
        texture->set_colorspace(colorspace);texture->set_alpha_type(alpha);texture->set_interpolation(INTERPOLATION_LINEAR);texture->set_extension(EXTENSION_REPEAT);
        if(filename)texture->set_filename(ustring(files[format].string()));
        else texture->handle=scene.image_manager->add_image(loader(format),texture->image_params(),false);
        graph.connect(uv->output("UV"),texture->input("Vector"));graph.connect(texture->output("Color"),material.diffuse->input("Color"));
        auto mesh=static_cast<Mesh *>(scene.geometry[0]);auto attr=mesh->attributes.add(ATTR_STD_UV);
        const float2 coords[]={make_float2(-.3f,-.2f),make_float2(1.7f,.1f),make_float2(.1f,1.4f)};
        std::copy(std::begin(coords),std::end(coords),attr->data_for_write<float2>());
        material.shader->tag_update(&scene);
      } else {
        auto shader=scene.background->get_shader();auto &graph=*shader->graph;
        auto texture=graph.create_node<EnvironmentTextureNode>();texture->set_colorspace(colorspace);texture->set_alpha_type(alpha);
        texture->set_interpolation(INTERPOLATION_LINEAR);texture->set_projection(NodeEnvironmentProjection(environment));
        texture->handle=scene.image_manager->add_image(loader(format),texture->image_params(),false);
        auto background=graph.create_node<BackgroundNode>();background->set_strength(.8f);
        graph.connect(texture->output("Color"),background->input("Color"));
        graph.disconnect(graph.output()->input("Surface"));graph.connect(background->output("Background"),graph.output()->input("Surface"));shader->tag_update(&scene);
      }
      auto output=std::make_shared<Output>();session.set_output_driver(make_unique<SessionImageOutput>(output));
      BufferParams buffers;buffers.width=buffers.full_width=buffers.window_width=width;buffers.height=buffers.full_height=buffers.window_height=height;
      session.reset(params,buffers);session.start();session.wait();
      require(!session.progress.get_error(),session.progress.get_error_message());require(!session.device->have_error(),session.device->error_message());
      require(output->writes==1&&output->valid,"Missing native image film");
      for(auto value:output->pixels)require(std::isfinite(value),"Nonfinite reference");
      std::ofstream film(out/("case."+std::to_string(completed)+".rgba.bin"),std::ios::binary);
      film.write(reinterpret_cast<const char *>(output->pixels.data()),output->pixels.size()*sizeof(float));require(film.good(),"Reference film write");++completed;
    };
    for(unsigned format=0;format<8;++format)for(auto space:spaces)for(unsigned alpha=0;alpha<IMAGE_ALPHA_NUM_TYPES;++alpha)
      render(format,space,ImageAlphaType(alpha),-1,false);
    for(unsigned format=0;format<8;++format)for(int projection=0;projection<2;++projection)
      render(format,ustring("cbc_scaled"),IMAGE_ALPHA_CHANNEL_PACKED,projection,false);
#ifdef CBC_HOST_NO_IMAGE_FILES
    // No image file readers: the four file-and-rebind cases do not apply.
    constexpr unsigned file_cases=0;
#else
    constexpr unsigned file_cases=4;
    for(unsigned format:{5u,7u}){render(format,ustring("cbc_scaled"),IMAGE_ALPHA_CHANNEL_PACKED,-1,true);render(format,ustring("cbc_scaled"),IMAGE_ALPHA_CHANNEL_PACKED,-1,false);}
#endif
    require(completed==256+file_cases,"Reference case count");
    std::cout<<"{\"passed\":true,\"originalNativeCpuSession\":true,\"imageFormats\":8,\"colorspaces\":6,\"alphaModes\":5,\"environmentCases\":16,\"fileAndRebindCases\":"<<file_cases<<",\"cases\":"<<completed<<"}\n";
  }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
}

