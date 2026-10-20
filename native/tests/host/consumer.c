// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#define _POSIX_C_SOURCE 200809L
#include "cycles_host.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void check(int result, const char *call) {
  if (result == CBC_OK) return;
  char error[4096]; uint64_t required = 0;
  cbc_host_error(error, sizeof(error), &required);
  fprintf(stderr, "%s returned %d: %s\n", call, result, error); exit(1);
}
#define CHECK(call) check((call), #call)
#define REQUIRE(test) do { if (!(test)) { fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #test); exit(1); } } while (0)
static CBCNode create(CBCHost host, const char *type) {
  CBCNode node = 0; CHECK(cbc_host_create_node(host, type, &node)); return node;
}
static CBCNode singleton(CBCHost host, const char *name) {
  CBCNode node = 0; CHECK(cbc_host_scene_node(host, name, &node)); return node;
}
static CBCNode shader_node(CBCHost host, CBCNode shader, const char *type) {
  CBCNode node = 0; CHECK(cbc_host_shader_node(host, shader, type, &node)); return node;
}
static void integer(CBCHost host, CBCNode node, const char *name, int64_t value) {
  CHECK(cbc_host_set_ints(host, node, name, &value, 1));
}
static void scalar(CBCHost host, CBCNode node, const char *name, float value) {
  CHECK(cbc_host_set_floats(host, node, name, &value, 1));
}
static void color(CBCHost host, CBCNode node, const char *name, float r, float g, float b) {
  float value[] = {r, g, b}; CHECK(cbc_host_set_floats(host, node, name, value, 3));
}
static CBCNode fixture(CBCHost host, CBCNode *mesh_out) {
  CBCNode integrator = singleton(host, "integrator");
  integer(host, integrator, "use_guiding", 0); integer(host, integrator, "use_light_tree", 0);
  integer(host, integrator, "use_adaptive_sampling", 0); integer(host, integrator, "use_denoise", 0);
  integer(host, integrator, "caustics_reflective", 0); integer(host, integrator, "caustics_refractive", 0);
  integer(host, integrator, "min_bounce", 2); integer(host, integrator, "max_bounce", 4);
  integer(host, integrator, "max_diffuse_bounce", 4);
  CHECK(cbc_host_set_string(host, integrator, "sampling_pattern", "tabulated_sobol"));
  CBCNode camera = singleton(host, "camera"); scalar(host, camera, "fov", 1.1f);
  float transform[] = {1,0,0,0, 0,1,0,0, 0,0,1,-4};
  CHECK(cbc_host_set_floats(host, camera, "matrix", transform, 12));

  CBCNode material = create(host, "shader"), output;
  CHECK(cbc_host_shader_output(host, material, &output));
  CBCNode diffuse = shader_node(host, material, "diffuse_bsdf");
  CBCNode value = shader_node(host, material, "color");
  color(host, value, "value", .7f, .2f, .1f); scalar(host, diffuse, "roughness", .3f);
  CHECK(cbc_host_connect(host, value, "Color", diffuse, "Color"));
  CHECK(cbc_host_connect(host, diffuse, "BSDF", output, "Surface"));
  CHECK(cbc_host_set_string(host, material, "emission_sampling_method", "none"));
  CBCNode mesh = create(host, "mesh");
  CHECK(cbc_host_set_nodes(host, mesh, "used_shaders", &material, 1));
  float xyz[] = {-1.5f,-1,0, 1.5f,-1,0, 0,1.5f,0}; uint32_t indices[] = {0,1,2};
  CHECK(cbc_host_mesh(host, mesh, xyz, 3, indices, 1));
  CBCNode object = create(host, "object"); CHECK(cbc_host_set_nodes(host, object, "geometry", &mesh, 1));
  CBCNode world = create(host, "shader"), background = shader_node(host, world, "background_shader");
  CHECK(cbc_host_shader_output(host, world, &output));
  color(host, background, "color", .25f, .4f, .6f); scalar(host, background, "strength", .8f);
  CHECK(cbc_host_connect(host, background, "Background", output, "Surface"));
  CHECK(cbc_host_set_string(host, world, "emission_sampling_method", "none"));
  CBCNode scene_background = singleton(host, "background");
  CHECK(cbc_host_set_nodes(host, scene_background, "shader", &world, 1));
  *mesh_out = mesh; return value;
}
enum { FLOAT_COUNT = 32 * 24 * 4 };
typedef struct RenderThread { CBCHost host; CBCRenderConfig config; float pixels[FLOAT_COUNT]; int status; } RenderThread;
static void *render_worker(void *data) {
  RenderThread *thread = data;
  thread->status = cbc_host_render(thread->host, &thread->config, thread->pixels, FLOAT_COUNT); return NULL;
}
static void wait_active(CBCHost host) {
  const struct timespec delay = {0, 1000000};
  for (unsigned tries = 0; tries < 30000; ++tries) {
    uint32_t active = 0; CHECK(cbc_host_is_rendering(host, &active)); if (active) return; nanosleep(&delay, NULL);
  }
  REQUIRE(0 && "Render did not become active");
}
int main(int argc, char **argv) {
  REQUIRE(argc == 2 || (argc == 3 && strcmp(argv[2], "--exit-live") == 0));
  CBCHostConfig config = {sizeof(config), CBC_HOST_ABI, CBC_HOST_CPU_REFERENCE, 1, {0}};
  CBCHost host = 99, other = 0;
  config.abi_version++;
  REQUIRE(cbc_host_create(&config, &host) == CBC_INVALID && host == 0);
  config.abi_version--; CHECK(cbc_host_create(&config, &host)); CHECK(cbc_host_create(&config, &other));
  CBCNode mesh = 0, value = fixture(host, &mesh);
  CBCNode foreign_camera = singleton(other, "camera"); float fov = 1;
  REQUIRE(cbc_host_set_floats(host, foreign_camera, "fov", &fov, 1) == CBC_INVALID);
  CHECK(cbc_host_destroy(other));
  REQUIRE(cbc_host_scene_node(other, "camera", &foreign_camera) == CBC_INVALID);
  REQUIRE(cbc_host_cancel(host) == CBC_NOT_READY);
  float invalid_xyz[] = {0,0,0}; uint32_t invalid_index[] = {0,0,1};
  REQUIRE(cbc_host_mesh(host, mesh, invalid_xyz, 1, invalid_index, 1) == CBC_INVALID);
  int64_t bad_enum = 99999;
  REQUIRE(cbc_host_set_ints(host, singleton(host, "integrator"), "sampling_pattern", &bad_enum, 1) == CBC_INVALID);
  CBCRenderConfig render = {sizeof(render), 32, 24, 8, {0}};
  float first[FLOAT_COUNT], previous[FLOAT_COUNT], pixels[FLOAT_COUNT];
  for (unsigned round = 0; round < 3; ++round) {
    if (round == 1) color(host, value, "value", .1f, .65f, .3f);
    for (unsigned i = 0; i < FLOAT_COUNT; ++i) pixels[i] = -99;
    REQUIRE(cbc_host_render(host, &render, pixels, FLOAT_COUNT-1) == CBC_INVALID);
    for (unsigned i = 0; i < FLOAT_COUNT; ++i) REQUIRE(pixels[i] == -99);
    CHECK(cbc_host_render(host, &render, pixels, FLOAT_COUNT));
    for (unsigned i = 0; i < FLOAT_COUNT; ++i) REQUIRE(isfinite(pixels[i]));
    if (round == 0) memcpy(first, pixels, sizeof(pixels));
    if (round == 1) REQUIRE(memcmp(first, pixels, sizeof(pixels)) != 0);
    if (round == 2) REQUIRE(memcmp(previous, pixels, sizeof(pixels)) == 0);
    memcpy(previous, pixels, sizeof(pixels));
    char path[4096]; REQUIRE(snprintf(path, sizeof(path), "%s.%u.rgba.bin", argv[1], round) > 0);
    FILE *file = fopen(path, "wb"); REQUIRE(file); REQUIRE(fwrite(pixels, sizeof(float), FLOAT_COUNT, file) == FLOAT_COUNT); REQUIRE(fclose(file) == 0);
  }
  CBCRenderConfig increased = render; increased.samples = 64;
  CHECK(cbc_host_render(host, &increased, pixels, FLOAT_COUNT));
  CHECK(cbc_host_render(host, &render, pixels, FLOAT_COUNT));
  REQUIRE(memcmp(previous, pixels, sizeof(pixels)) == 0);
  if (argc == 3) {
    puts("{\"passed\":true,\"processExitWithLiveContext\":true}");
    return 0;
  }
  RenderThread thread = {host, render, {0}, -1}; thread.config.samples = 1000000;
  for (unsigned i = 0; i < FLOAT_COUNT; ++i) thread.pixels[i] = -99;
  pthread_t worker; REQUIRE(pthread_create(&worker, NULL, render_worker, &thread) == 0);
  wait_active(host); CHECK(cbc_host_cancel(host)); REQUIRE(pthread_join(worker, NULL) == 0);
  REQUIRE(thread.status == CBC_CANCELLED);
  for (unsigned i = 0; i < FLOAT_COUNT; ++i) REQUIRE(thread.pixels[i] == -99);
  CHECK(cbc_host_render(host, &render, pixels, FLOAT_COUNT));
  REQUIRE(memcmp(previous, pixels, sizeof(pixels)) == 0);
  REQUIRE(pthread_create(&worker, NULL, render_worker, &thread) == 0);
  wait_active(host); CHECK(cbc_host_destroy(host)); REQUIRE(pthread_join(worker, NULL) == 0);
  REQUIRE(thread.status == CBC_CANCELLED);
  for (unsigned i = 0; i < FLOAT_COUNT; ++i) REQUIRE(thread.pixels[i] == -99);
  REQUIRE(cbc_host_destroy(host) == CBC_INVALID);
  puts("{\"passed\":true,\"pureCConsumer\":true,\"explicitCpuReference\":true,\"materialEditAfterCompilation\":true,\"repeatBitwiseIdentical\":true,\"sampleBudgetDecrease\":true,\"failedRenderPreservesOutput\":true,\"cancelPreservesOutput\":true,\"renderAfterCancel\":true,\"destroyDuringRender\":true,\"foreignAndStaleHandlesRejected\":true}");
  return 0;
}
