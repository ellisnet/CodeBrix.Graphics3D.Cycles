// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Device-tier consumer for native/tools/tier_gate.py: applies the public tier
// configuration, prints the device capability report before and after one
// small WebGPU render, and saves the film. Adapter identity, crashes and canary
// failures are simulated only through the documented gate environment hooks.
#define _POSIX_C_SOURCE 200809L
#include "cycles_host.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
static char *report(CBCHost host) {
  uint64_t required = 0;
  REQUIRE(cbc_host_device_report_json(host, NULL, 0, &required) == CBC_SMALL_BUFFER && required > 1);
  char small[2];
  REQUIRE(cbc_host_device_report_json(host, small, sizeof(small), &required) == CBC_SMALL_BUFFER);
  char *json = malloc(required); REQUIRE(json);
  CHECK(cbc_host_device_report_json(host, json, required, &required));
  REQUIRE(strlen(json) + 1 == required);
  return json;
}
/* argv: render|create, auto|full|conservative|none, record directory or -, flags (c canary, o off, r read-only, - none), film path */
int main(int argc, char **argv) {
  REQUIRE(argc == 6);
  CBCTierConfig tier = {sizeof(tier), CBC_HOST_ABI, CBC_TIER_AUTO, 0, {0}};
  /* Size, version, reserved, tier and flag validation. */
  tier.struct_size--; REQUIRE(cbc_host_tier_config(&tier, NULL) == CBC_INVALID); tier.struct_size++;
  tier.abi_version++; REQUIRE(cbc_host_tier_config(&tier, NULL) == CBC_INVALID); tier.abi_version--;
  tier.reserved[0] = 1; REQUIRE(cbc_host_tier_config(&tier, NULL) == CBC_INVALID); tier.reserved[0] = 0;
  tier.tier = 3; REQUIRE(cbc_host_tier_config(&tier, NULL) == CBC_INVALID); tier.tier = CBC_TIER_AUTO;
  tier.flags = 8; REQUIRE(cbc_host_tier_config(&tier, NULL) == CBC_INVALID); tier.flags = 0;
  REQUIRE(cbc_host_tier_config(NULL, NULL) == CBC_INVALID);
  if (strcmp(argv[2], "none") != 0) {
    tier.tier = strcmp(argv[2], "full") == 0 ? CBC_TIER_FULL : strcmp(argv[2], "conservative") == 0 ? CBC_TIER_CONSERVATIVE : CBC_TIER_AUTO;
    if (strchr(argv[4], 'c')) tier.flags |= CBC_TIER_CANARY;
    if (strchr(argv[4], 'o')) tier.flags |= CBC_TIER_RECORD_OFF;
    if (strchr(argv[4], 'r')) tier.flags |= CBC_TIER_RECORD_READ_ONLY;
    CHECK(cbc_host_tier_config(&tier, strcmp(argv[3], "-") == 0 ? NULL : argv[3]));
  }
  CBCHostConfig config = {sizeof(config), CBC_HOST_ABI, CBC_HOST_WEBGPU, 1, {0}};
  CBCHost host = 0; CHECK(cbc_host_create(&config, &host));
  REQUIRE(cbc_host_device_report_json(host + 1000, NULL, 0, NULL) == CBC_INVALID);
  char *before = report(host), *after = NULL;
  if (strcmp(argv[1], "render") == 0) {
    CBCNode mesh = 0; fixture(host, &mesh);
    CBCRenderConfig render = {sizeof(render), 32, 24, 8, {0}};
    static float pixels[FLOAT_COUNT];
    CHECK(cbc_host_render(host, &render, pixels, FLOAT_COUNT));
    FILE *file = fopen(argv[5], "wb"); REQUIRE(file);
    REQUIRE(fwrite(pixels, sizeof(float), FLOAT_COUNT, file) == FLOAT_COUNT); REQUIRE(fclose(file) == 0);
    after = report(host);
  }
  printf("{\"before\":%s,\"after\":%s}\n", before, after ? after : "null");
  free(before); free(after);
  CHECK(cbc_host_destroy(host));
  return 0;
}
