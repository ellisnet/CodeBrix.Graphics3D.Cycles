// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Minimal consumer of the public C ABI (native/host/ABI.md "Example").
// Builds a lit triangle, renders it on the CPU reference or the platform GPU
// backend, reads the Combined and Depth passes and prints a summary line.
// Usage: example cpu|gpu
#include "cycles_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(const char *call) {
  char message[2048]; uint64_t required = 0;
  cbc_host_error(message, sizeof(message), &required);
  fprintf(stderr, "%s failed: %s\n", call, message);
  return 1;
}
#define TRY(call) do { if ((call) != CBC_OK) return fail(#call); } while (0)

static int on_event(void *user_data, const CBCEvent *event) {
  unsigned *progress = user_data;
  if (event->kind == CBC_EVENT_PROGRESS) *progress += 1;
  return 0; /* nonzero would cancel the render */
}

int main(int argc, char **argv) {
  const int gpu = argc > 1 && strcmp(argv[1], "gpu") == 0;
  CBCVersionInfo version = {sizeof(version), 0, {0}, {0}, {0}};
  TRY(cbc_host_version(&version));

  CBCHostConfig config = {sizeof(config), CBC_HOST_ABI, gpu ? CBC_HOST_GPU : CBC_HOST_CPU_REFERENCE, 0, {0}};
  CBCHost host = 0;
  TRY(cbc_host_create(&config, &host));
  uint32_t backend = 0;
  TRY(cbc_host_backend(host, &backend));

  /* Camera four units back, looking down +Z. */
  CBCNode camera, integrator, background;
  TRY(cbc_host_scene_node(host, "camera", &camera));
  TRY(cbc_host_scene_node(host, "integrator", &integrator));
  TRY(cbc_host_scene_node(host, "background", &background));
  const float matrix[12] = {1,0,0,0, 0,1,0,0, 0,0,1,-4};
  TRY(cbc_host_set_floats(host, camera, "matrix", matrix, 12));
  TRY(cbc_host_set_string(host, integrator, "sampling_pattern", "tabulated_sobol"));
  /* Integrator features the WebGPU backend does not implement (light trees,
   * denoising, adaptive sampling and path guiding all add passes or kernels). */
  const int64_t off = 0;
  TRY(cbc_host_set_ints(host, integrator, "use_light_tree", &off, 1));
  TRY(cbc_host_set_ints(host, integrator, "use_denoise", &off, 1));
  TRY(cbc_host_set_ints(host, integrator, "use_adaptive_sampling", &off, 1));
  TRY(cbc_host_set_ints(host, integrator, "use_guiding", &off, 1));

  /* Diffuse material and a constant world. */
  CBCNode material, world, diffuse, sky, output;
  TRY(cbc_host_create_node(host, "shader", &material));
  TRY(cbc_host_shader_node(host, material, "diffuse_bsdf", &diffuse));
  const float orange[3] = {.8f, .4f, .1f};
  TRY(cbc_host_set_floats(host, diffuse, "color", orange, 3));
  TRY(cbc_host_shader_output(host, material, &output));
  TRY(cbc_host_connect(host, diffuse, "BSDF", output, "Surface"));
  TRY(cbc_host_create_node(host, "shader", &world));
  TRY(cbc_host_shader_node(host, world, "background_shader", &sky));
  const float blue[3] = {.2f, .3f, .5f};
  TRY(cbc_host_set_floats(host, sky, "color", blue, 3));
  TRY(cbc_host_shader_output(host, world, &output));
  TRY(cbc_host_connect(host, sky, "Background", output, "Surface"));
  TRY(cbc_host_set_nodes(host, background, "shader", &world, 1));

  /* One triangle, one object, one point light. */
  CBCNode mesh, object, light, holder, emission, emitter;
  const float xyz[9] = {-1.5f,-1,0, 1.5f,-1,0, 0,1.5f,0};
  const uint32_t triangle[3] = {0, 1, 2};
  TRY(cbc_host_create_node(host, "mesh", &mesh));
  TRY(cbc_host_set_nodes(host, mesh, "used_shaders", &material, 1));
  TRY(cbc_host_mesh(host, mesh, xyz, 3, triangle, 1));
  TRY(cbc_host_create_node(host, "object", &object));
  TRY(cbc_host_set_nodes(host, object, "geometry", &mesh, 1));
  TRY(cbc_host_create_node(host, "shader", &emission));
  TRY(cbc_host_shader_node(host, emission, "emission", &emitter));
  TRY(cbc_host_shader_output(host, emission, &output));
  TRY(cbc_host_connect(host, emitter, "Emission", output, "Surface"));
  TRY(cbc_host_create_node(host, "pointlight", &light));
  TRY(cbc_host_set_nodes(host, light, "used_shaders", &emission, 1));
  const float strength[3] = {40, 40, 40};
  TRY(cbc_host_set_floats(host, light, "strength", strength, 3));
  TRY(cbc_host_create_node(host, "object", &holder));
  TRY(cbc_host_set_nodes(host, holder, "geometry", &light, 1));
  const float above[12] = {1,0,0,1, 0,1,0,2, 0,0,1,-2};
  TRY(cbc_host_set_floats(host, holder, "tfm", above, 12));

  /* Ask for a depth pass where the device writes one (the capability report
   * lists pass support; the WebGPU device writes Combined only), follow
   * progress, render. */
  if (backend == CBC_HOST_CPU_REFERENCE) {
    CBCNode depth;
    TRY(cbc_host_create_node(host, "pass", &depth));
    TRY(cbc_host_set_string(host, depth, "type", "depth"));
    TRY(cbc_host_set_string(host, depth, "name", "Depth"));
  }
  unsigned progress = 0;
  TRY(cbc_host_set_event_callback(host, on_event, &progress));
  enum { WIDTH = 32, HEIGHT = 24 };
  static float rgba[WIDTH * HEIGHT * 4], depth_values[WIDTH * HEIGHT];
  CBCRenderConfig render = {sizeof(render), WIDTH, HEIGHT, 16, {0}};
  TRY(cbc_host_render(host, &render, rgba, WIDTH * HEIGHT * 4));

  uint64_t required = 0; uint32_t components = 0;
  const int depth_status = cbc_host_pass_floats(host, "Depth", depth_values, WIDTH * HEIGHT, &required, &components);
  const float *center = rgba + 4 * (WIDTH * (HEIGHT / 2) + WIDTH / 2);
  printf("{\"passed\":true,\"library\":\"%s\",\"backend\":%u,\"progressEvents\":%u,\"center\":[%g,%g,%g],"
         "\"depthStatus\":%d,\"centerDepth\":%g}\n", version.library_version, backend, progress,
         center[0], center[1], center[2], depth_status,
         depth_status == CBC_OK ? depth_values[WIDTH * (HEIGHT / 2) + WIDTH / 2] : -1.0);
  TRY(cbc_host_destroy(host));
  return 0;
}
