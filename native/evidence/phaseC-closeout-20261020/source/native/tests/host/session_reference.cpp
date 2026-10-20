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
#ifndef CBC_HOST_REFERENCE_SAMPLES
#  define CBC_HOST_REFERENCE_SAMPLES 8
#endif
static constexpr int width = 32, height = 24, samples = CBC_HOST_REFERENCE_SAMPLES;
static void require(bool ok, const std::string &message) {
  if (!ok) throw std::runtime_error(message);
}

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

int main(int argc, char **argv) {
  try {
    require(argc == 3, "Usage: session_probe CPU|WEBGPU OUTPUT_PREFIX");
    const std::string requested = argv[1], prefix = argv[2];
    require(requested == "CPU" || requested == "WEBGPU", "Explicit device type required");
    auto type = Device::type_from_string(requested.c_str());
    require(type != DEVICE_NONE, "Unknown device type");
    auto devices = Device::available_devices(DeviceTypeMask(1 << type));
    require(!devices.empty(), "Requested device unavailable");
    SessionParams params;
    params.device = devices[0];
    params.denoise_device = params.device;
    params.headless = true;
    params.background = true;
    params.samples = samples;
    params.threads = 1;
    params.use_auto_tile = false;
    params.use_resolution_divider = false;
    SceneParams scene_params;
    scene_params.bvh_layout = BVH_LAYOUT_BVH2;
    scene_params.bvh_type = BVH_TYPE_STATIC;
    scene_params.use_bvh_unaligned_nodes = false;
    scene_params.use_texture_cache = false;
    auto output = std::make_shared<Output>();
    std::vector<std::vector<float>> images;
    {
      Session session(params, scene_params);
      require(!session.progress.get_error(), session.progress.get_error_message());
      auto material = prepare(*session.scene);
      session.set_output_driver(make_unique<SessionImageOutput>(output));
      BufferParams buffers;
      buffers.width = buffers.full_width = buffers.window_width = width;
      buffers.height = buffers.full_height = buffers.window_height = height;
      for (unsigned round = 0; round != 3; ++round) {
        if (round == 1) {
          material.diffuse->set_color(make_float3(.1f, .65f, .3f));
          material.shader->tag_update(session.scene.get());
        }
        output->writes = 0;
        output->valid = false;
        session.reset(params, buffers);
        session.start();
        session.wait();
        require(!session.progress.get_error(), session.progress.get_error_message());
        require(!session.device->have_error(), session.device->error_message());
        require(output->writes == 1 && output->valid, "Missing complete Combined output tile");
        require(std::all_of(output->pixels.begin(), output->pixels.end(), [](float v) { return std::isfinite(v); }), "Nonfinite output");
        for (size_t i = 3; i < output->pixels.size(); i += 4) require(output->pixels[i] == 1.0f, "Unexpected alpha");
        images.push_back(output->pixels);
        std::ofstream file(prefix + "." + std::to_string(round) + ".rgba.bin", std::ios::binary);
        file.write(reinterpret_cast<const char *>(output->pixels.data()), output->pixels.size() * sizeof(float));
        require(file.good(), "Cannot write reference output");
        std::cerr << "Completed Session round " << round << '\n';
      }
      require(images[0] != images[1], "Material edit did not change output");
      require(images[1] == images[2], "Unedited Session reset changed output");
    }
    std::cout << "{\"passed\":true,\"device\":\"" << requested << "\",\"actualSession\":true,\"renders\":3,\"width\":" << width
              << ",\"height\":" << height << ",\"samples\":" << samples << ",\"materialEditChangesFilm\":true,\"repeatBitwiseIdentical\":true,\"teardownReturned\":true}\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
