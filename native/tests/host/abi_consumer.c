// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Phase D ABI consumer for native/tools/host_gate.py: exercises every export
// added to ABI 1 (version, backend-neutral selection, capability report,
// deletion, image updates, deformation/object/camera motion, lights, cameras,
// integrator/film settings, events and pass readback), their malformed-input
// and lifetime rules and struct size rejection. argv: output prefix, then
// "cpu" or "gpu". Writes <prefix>.capabilities*.json, <prefix>.passes.json and
// raw films; prints one JSON result line.
#define _POSIX_C_SOURCE 200809L
#include "cycles_host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check(int result, const char *call, int line) {
  if (result == CBC_OK) return;
  char error[4096]; uint64_t required = 0;
  cbc_host_error(error, sizeof(error), &required);
  fprintf(stderr, "line %d: %s returned %d: %s\n", line, call, result, error); exit(1);
}
#define CHECK(call) check((call), #call, __LINE__)
#define REQUIRE(test) do { if (!(test)) { fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #test); exit(1); } } while (0)
#define W 32
#define H 24
enum { FLOATS = W * H * 4 };
static CBCHost host;
static CBCNode create(const char *type) { CBCNode node = 0; CHECK(cbc_host_create_node(host, type, &node)); return node; }
static CBCNode singleton(const char *name) { CBCNode node = 0; CHECK(cbc_host_scene_node(host, name, &node)); return node; }
static CBCNode graph_node(CBCNode shader, const char *type) { CBCNode node = 0; CHECK(cbc_host_shader_node(host, shader, type, &node)); return node; }
static CBCNode output_of(CBCNode shader) { CBCNode node = 0; CHECK(cbc_host_shader_output(host, shader, &node)); return node; }
static void integer(CBCNode node, const char *name, int64_t value) { CHECK(cbc_host_set_ints(host, node, name, &value, 1)); }
static void scalar(CBCNode node, const char *name, float value) { CHECK(cbc_host_set_floats(host, node, name, &value, 1)); }
static void triple(CBCNode node, const char *name, float a, float b, float c) { float v[] = {a, b, c}; CHECK(cbc_host_set_floats(host, node, name, v, 3)); }
static void text(CBCNode node, const char *name, const char *value) { CHECK(cbc_host_set_string(host, node, name, value)); }
static void nodes(CBCNode node, const char *name, CBCNode value) { CHECK(cbc_host_set_nodes(host, node, name, &value, 1)); }
static void translate(float *m, float x, float y, float z) { float v[] = {1,0,0,x, 0,1,0,y, 0,0,1,z}; memcpy(m, v, sizeof(v)); }
static CBCNode emission_shader(float strength) {
  CBCNode shader = create("shader"), e = graph_node(shader, "emission");
  triple(e, "color", 1, 1, 1); scalar(e, "strength", strength);
  CHECK(cbc_host_connect(host, e, "Emission", output_of(shader), "Surface")); return shader;
}
static double render_count = 0;
static void render_at(float *pixels, int line) {
  fprintf(stderr, "render %.0f (line %d)\n", render_count, line);
  CBCRenderConfig config = {sizeof(config), W, H, 8, {0}};
  CHECK(cbc_host_render(host, &config, pixels, FLOATS)); render_count++;
  for (unsigned i = 0; i < FLOATS; ++i) REQUIRE(isfinite(pixels[i]));
}
#define render(pixels) render_at((pixels), __LINE__)
static int same(const float *a, const float *b) { return memcmp(a, b, sizeof(float) * FLOATS) == 0; }
static void save(const char *prefix, const char *name, const void *data, size_t bytes) {
  char path[4096]; REQUIRE(snprintf(path, sizeof(path), "%s.%s", prefix, name) > 0);
  FILE *file = fopen(path, "wb"); REQUIRE(file); REQUIRE(fwrite(data, 1, bytes, file) == bytes); REQUIRE(fclose(file) == 0);
}
static char *json_of(int (*call)(CBCHost, char *, uint64_t, uint64_t *), CBCHost target) {
  uint64_t required = 0;
  REQUIRE(call(target, NULL, 0, &required) == CBC_SMALL_BUFFER && required > 2);
  char small[1]; REQUIRE(call(target, small, sizeof(small), &required) == CBC_SMALL_BUFFER);
  REQUIRE(call(target, NULL, 5, &required) == CBC_INVALID && call(target, small, 1, NULL) == CBC_INVALID);
  char *json = malloc(required); REQUIRE(json);
  CHECK(call(target, json, required, &required)); REQUIRE(strlen(json) + 1 == required); return json;
}

/* Events: count kinds, verify reentrancy refusal and optional cancellation. */
typedef struct Events { unsigned status, progress, error, refused, version_ok; uint64_t last_done, total; int cancel_at; } Events;
static int on_event(void *data, const CBCEvent *event) {
  Events *events = data;
  if (event->struct_size < sizeof(CBCEvent)) return 1;
  if (event->kind == CBC_EVENT_STATUS) events->status++;
  else if (event->kind == CBC_EVENT_PROGRESS) { events->progress++; events->last_done = event->samples_done; events->total = event->samples_total; }
  else if (event->kind == CBC_EVENT_ERROR) events->error++;
  if (!event->message) return 1;
  float value = 1; uint32_t rendering = 0;
  if (cbc_host_set_floats(host, 1, "fov", &value, 1) == CBC_NOT_READY && cbc_host_cancel(host) == CBC_NOT_READY &&
      cbc_host_is_rendering(host, &rendering) == CBC_NOT_READY) events->refused++;
  CBCVersionInfo version = {sizeof(version), 0, {0}, {0}, {0}};
  if (cbc_host_version(&version) == CBC_OK) events->version_ok++;
  return events->cancel_at && event->kind == CBC_EVENT_PROGRESS && event->samples_done >= (uint64_t)events->cancel_at;
}

int main(int argc, char **argv) {
  REQUIRE(argc == 3 && (strcmp(argv[2], "cpu") == 0 || strcmp(argv[2], "gpu") == 0));
  const int gpu = strcmp(argv[2], "gpu") == 0;
  const char *prefix = argv[1];

  /* Version: size-checked output structure, callable without a host. */
  CBCVersionInfo version; memset(&version, 0x5a, sizeof(version));
  version.struct_size = sizeof(version) - 1; REQUIRE(cbc_host_version(&version) == CBC_INVALID);
  version.struct_size = sizeof(version) + 4; REQUIRE(cbc_host_version(&version) == CBC_INVALID);
  REQUIRE(cbc_host_version(NULL) == CBC_INVALID);
  version.struct_size = sizeof(version); CHECK(cbc_host_version(&version));
  REQUIRE(version.abi_version == CBC_HOST_ABI && version.abi_version == cbc_host_abi_version());
  REQUIRE(memchr(version.library_version, 0, sizeof(version.library_version)) && memchr(version.cycles_revision, 0, sizeof(version.cycles_revision)));
  REQUIRE(strlen(version.cycles_revision) == 40 && version.reserved[0] == 0 && version.reserved[1] == 0);

  /* Backend-neutral selection. */
  uint32_t resolved = 99;
  CHECK(cbc_host_resolve_backend(CBC_HOST_GPU, &resolved)); const uint32_t gpu_backend = resolved;
  REQUIRE(gpu_backend == CBC_HOST_WEBGPU || gpu_backend == CBC_HOST_METAL);
  CHECK(cbc_host_resolve_backend(CBC_HOST_CPU_REFERENCE, &resolved)); REQUIRE(resolved == CBC_HOST_CPU_REFERENCE);
  REQUIRE(cbc_host_resolve_backend(gpu_backend == CBC_HOST_WEBGPU ? CBC_HOST_METAL : CBC_HOST_WEBGPU, &resolved) == CBC_UNSUPPORTED && resolved == 0);
  REQUIRE(cbc_host_resolve_backend(0, &resolved) == CBC_INVALID && cbc_host_resolve_backend(5, &resolved) == CBC_INVALID);
  REQUIRE(cbc_host_resolve_backend(CBC_HOST_GPU, NULL) == CBC_INVALID);
  REQUIRE(cbc_host_backend(12345, &resolved) == CBC_INVALID);

  char *binary_report = json_of(cbc_host_capabilities_json, 0);
  save(prefix, "capabilities-binary.json", binary_report, strlen(binary_report)); free(binary_report);
  REQUIRE(cbc_host_capabilities_json(987654, NULL, 0, &(uint64_t){0}) == CBC_INVALID);

  CBCHostConfig config = {sizeof(config), CBC_HOST_ABI, gpu ? CBC_HOST_GPU : CBC_HOST_CPU_REFERENCE, 1, {0}};
  config.backend = 5; REQUIRE(cbc_host_create(&config, &host) == CBC_INVALID && host == 0);
  config.backend = gpu ? CBC_HOST_GPU : CBC_HOST_CPU_REFERENCE;
  CHECK(cbc_host_create(&config, &host));
  uint32_t backend = 0; CHECK(cbc_host_backend(host, &backend)); REQUIRE(backend == (gpu ? gpu_backend : CBC_HOST_CPU_REFERENCE));
  REQUIRE(cbc_host_backend(host, NULL) == CBC_INVALID);
  char *host_report = json_of(cbc_host_capabilities_json, host);
  save(prefix, "capabilities-host.json", host_report, strlen(host_report)); free(host_report);

  /* Passes before any render. */
  float pass[W * H * 4]; uint64_t required = 0; uint32_t components = 0;
  REQUIRE(cbc_host_pass_floats(host, "Combined", pass, FLOATS, &required, &components) == CBC_NOT_READY);
  REQUIRE(cbc_host_render_passes_json(host, NULL, 0, &required) == CBC_NOT_READY);

  /* Scene: integrator/film/camera through the generic socket layer. */
  CBCNode integrator = singleton("integrator"), film = singleton("film"), camera = singleton("camera");
  integer(integrator, "use_guiding", 0); integer(integrator, "use_light_tree", 0); integer(integrator, "use_adaptive_sampling", 0);
  integer(integrator, "use_denoise", 0); integer(integrator, "caustics_reflective", 0); integer(integrator, "caustics_refractive", 0);
  integer(integrator, "min_bounce", 2); integer(integrator, "max_bounce", 4); integer(integrator, "seed", 0);
  text(integrator, "sampling_pattern", "tabulated_sobol"); scalar(film, "exposure", 1); text(film, "filter_type", "box");
  text(camera, "camera_type", "perspective"); scalar(camera, "fov", 1.1f);
  float m[12]; translate(m, 0, 0, -4); CHECK(cbc_host_set_floats(host, camera, "matrix", m, 12));
  CBCNode world = create("shader"), bg = graph_node(world, "background_shader");
  triple(bg, "color", .25f, .4f, .6f); scalar(bg, "strength", .8f);
  CHECK(cbc_host_connect(host, bg, "Background", output_of(world), "Surface")); text(world, "emission_sampling_method", "none");
  nodes(singleton("background"), "shader", world);
  CBCNode material = create("shader"), diffuse = graph_node(material, "diffuse_bsdf"), value = graph_node(material, "color");
  triple(value, "value", .7f, .2f, .1f); CHECK(cbc_host_connect(host, value, "Color", diffuse, "Color"));
  CHECK(cbc_host_connect(host, diffuse, "BSDF", output_of(material), "Surface")); text(material, "emission_sampling_method", "none");
  CBCNode mesh = create("mesh"); nodes(mesh, "used_shaders", material);
  float xyz[] = {-1.5f,-1,0, 1.5f,-1,0, 0,1.5f,0}; uint32_t tri[] = {0,1,2};
  CHECK(cbc_host_mesh(host, mesh, xyz, 3, tri, 1));
  CBCNode object = create("object"); nodes(object, "geometry", mesh);

  float base[FLOATS], film_b[FLOATS], film_c[FLOATS];
  render(base); save(prefix, "base.rgba.bin", base, sizeof(base));
  /* Combined pass equals the render output. */
  CHECK(cbc_host_pass_floats(host, "Combined", pass, FLOATS, &required, &components));
  REQUIRE(components == 4 && required == FLOATS && same(pass, base));
  REQUIRE(cbc_host_pass_floats(host, "Combined", pass, FLOATS - 1, &required, &components) == CBC_SMALL_BUFFER && required == FLOATS);
  REQUIRE(cbc_host_pass_floats(host, "Missing", pass, FLOATS, &required, &components) == CBC_INVALID);
  REQUIRE(cbc_host_pass_floats(host, "Combined", pass, FLOATS, &required, NULL) == CBC_INVALID);

  /* Film exposure (generic socket) scales the scene-linear result by 2^1. */
  scalar(film, "exposure", 2); render(film_b); scalar(film, "exposure", 1);
  unsigned exposure_ok = 0;
  for (unsigned i = 0; i < FLOATS; ++i) if (i % 4 == 3 || fabsf(film_b[i] - 2 * base[i]) <= 5e-5f + 2e-4f * fabsf(2 * base[i])) exposure_ok++;
  REQUIRE(exposure_ok == FLOATS);

  /* Lights: every light node type the registry exposes. */
  const char *kinds[] = {"pointlight", "spotlight", "arealight", "sunlight", "backgroundlight"};
  unsigned light_types = 0, lit_renders = 0;
  for (unsigned k = 0; k < 5; ++k) {
    CBCNode emitter = emission_shader(1), light = create(kinds[k]), holder = create("object");
    nodes(light, "used_shaders", emitter); integer(light, "use_mis", 1); integer(light, "cast_shadow", 1);
    triple(light, "strength", k == 3 ? 2.f : 60.f, k == 3 ? 2.f : 60.f, k == 3 ? 2.f : 60.f);
    if (k == 0) { scalar(light, "radius", .2f); integer(light, "is_sphere", 1); }
    if (k == 1) { scalar(light, "radius", .2f); scalar(light, "angle", .9f); scalar(light, "smooth", .2f); }
    if (k == 2) { scalar(light, "sizeu", 1); scalar(light, "sizev", .5f); integer(light, "ellipse", 0); scalar(light, "spread", 3.14159f); }
    if (k == 3) scalar(light, "angle", .1f);
    /* Sun and area lights emit along the holder's -Z; turn those toward the triangle. */
    float t[] = {1,0,0,0, 0,1,0,0, 0,0,1,-2}; if (k == 2 || k == 3) { t[5] = -1; t[10] = -1; }
    nodes(holder, "geometry", light);
    CHECK(cbc_host_set_floats(host, holder, "tfm", t, 12));
    char type[64]; CHECK(cbc_host_node_type(host, light, type, sizeof(type), &required)); light_types++;
    /* Point, spot, area and sun render on both backends in one device lifetime
     * (the Phase D3 spot-after-point case; the area light adds the LIGHT_AREA
     * specialization and recompiles the light stage). Each film is saved and
     * the host gate compares the WebGPU film with the CPU film. */
    if (k < 4) {
      render(film_c); REQUIRE(!same(film_c, base)); lit_renders++;
      char name[64]; snprintf(name, sizeof(name), "light-%s.rgba.bin", kinds[k]); save(prefix, name, film_c, sizeof(film_c));
    }
    /* Lifetime: a referenced light cannot be deleted; delete holder, then light, then emitter. */
    REQUIRE(cbc_host_delete_node(host, light) == CBC_INVALID);
    REQUIRE(cbc_host_delete_node(host, emitter) == CBC_INVALID);
    CHECK(cbc_host_delete_node(host, holder)); CHECK(cbc_host_delete_node(host, light));
    CHECK(cbc_host_delete_node(host, emitter));
    REQUIRE(cbc_host_delete_node(host, light) == CBC_INVALID);
    REQUIRE(cbc_host_set_floats(host, light, "strength", m, 3) == CBC_INVALID);
  }
  render(film_c); const int light_deletion_restores = same(film_c, base);
  if (!gpu) REQUIRE(light_deletion_restores);

  /* Deletion rules. */
  REQUIRE(cbc_host_delete_node(host, camera) == CBC_INVALID && cbc_host_delete_node(host, integrator) == CBC_INVALID);
  REQUIRE(cbc_host_delete_node(host, output_of(material)) == CBC_INVALID);
  REQUIRE(cbc_host_delete_node(host, mesh) == CBC_INVALID && cbc_host_delete_node(host, material) == CBC_INVALID);
  REQUIRE(cbc_host_delete_node(host, 0) == CBC_INVALID && cbc_host_delete_node(host + 99, object) == CBC_INVALID);
  /* Graph node deletion: removing the color node restores diffuse's own color. */
  CHECK(cbc_host_delete_node(host, value)); REQUIRE(cbc_host_set_floats(host, value, "value", m, 3) == CBC_INVALID);
  render(film_c); REQUIRE(!same(film_c, base));
  value = graph_node(material, "color"); triple(value, "value", .7f, .2f, .1f);
  CHECK(cbc_host_connect(host, value, "Color", diffuse, "Color"));
  render(film_c); const int graph_rebuild_restores = same(film_c, base);
  if (!gpu) REQUIRE(graph_rebuild_restores);

  /* Motion. Edits after a render are applied (ABI.md "Edits after a render"):
   * deformation motion added to an already rendered mesh is accepted; the
   * rounds below also exercise delete/recreate lifetimes. Camera motion uses
   * the generic "motion" socket. */
  integer(integrator, "motion_blur", 1); scalar(camera, "shuttertime", .6f);
  float cam3[36]; translate(cam3, -.2f, 0, -4); translate(cam3 + 12, 0, 0, -4); translate(cam3 + 24, .2f, 0, -4);
  CHECK(cbc_host_set_floats(host, camera, "motion", cam3, 36));
  render(film_b); REQUIRE(!same(film_b, base));
  CHECK(cbc_host_set_floats(host, camera, "motion", NULL, 0));
  REQUIRE(cbc_host_motion_positions(host, mesh, 3, xyz, 9) == CBC_INVALID);
  float motion[27]; memcpy(motion + 9, xyz, sizeof(xyz));
  for (unsigned i = 0; i < 9; ++i) { motion[i] = xyz[i] + (i % 3 == 0 ? -.25f : 0); motion[18 + i] = xyz[i] + (i % 3 == 0 ? .25f : 0); }
  CHECK(cbc_host_motion_positions(host, mesh, 3, motion, 27)); CHECK(cbc_host_motion_positions(host, mesh, 1, NULL, 0));
  for (unsigned round = 0; round < 3; ++round) {
    CHECK(cbc_host_delete_node(host, object)); CHECK(cbc_host_delete_node(host, mesh));
    mesh = create("mesh"); nodes(mesh, "used_shaders", material); CHECK(cbc_host_mesh(host, mesh, xyz, 3, tri, 1));
    object = create("object"); nodes(object, "geometry", mesh);
    if (round == 0) {
      float steps3[36]; translate(steps3, -.3f, 0, 0); translate(steps3 + 12, 0, 0, 0); translate(steps3 + 24, .3f, .1f, 0);
      CHECK(cbc_host_set_floats(host, object, "motion", steps3, 36));
      render(film_b); REQUIRE(!same(film_b, base)); save(prefix, "object-motion.rgba.bin", film_b, sizeof(film_b));
    } else if (round == 1) {
      REQUIRE(cbc_host_motion_positions(host, mesh, 2, motion, 18) == CBC_INVALID);
      REQUIRE(cbc_host_motion_positions(host, mesh, 3, motion, 26) == CBC_INVALID);
      REQUIRE(cbc_host_motion_positions(host, mesh, 131, motion, 27) == CBC_INVALID);
      REQUIRE(cbc_host_motion_positions(host, mesh, 1, motion, 27) == CBC_INVALID);
      REQUIRE(cbc_host_motion_positions(host, object, 3, motion, 27) == CBC_INVALID);
      motion[9] += 1e-3f; REQUIRE(cbc_host_motion_positions(host, mesh, 3, motion, 27) == CBC_INVALID); motion[9] -= 1e-3f;
      motion[0] = NAN; REQUIRE(cbc_host_motion_positions(host, mesh, 3, motion, 27) == CBC_INVALID); motion[0] = xyz[0] - .25f;
      CHECK(cbc_host_motion_positions(host, mesh, 3, motion, 27));
      int64_t blur = 0; CHECK(cbc_host_get_ints(host, mesh, "use_motion_blur", &blur, 1, &required)); REQUIRE(blur == 1);
      uint64_t motion_steps = 0; CHECK(cbc_host_get_uints(host, mesh, "motion_steps", &motion_steps, 1, &required)); REQUIRE(motion_steps == 3);
      render(film_b); REQUIRE(!same(film_b, base)); save(prefix, "deforming.rgba.bin", film_b, sizeof(film_b));
      /* After a render, motion can be replaced and removed. */
      CHECK(cbc_host_motion_positions(host, mesh, 3, motion, 27));
      CHECK(cbc_host_motion_positions(host, mesh, 1, NULL, 0));
    } else {
      integer(integrator, "motion_blur", 0); scalar(camera, "shuttertime", 1);
      render(film_c);
    }
  }
  const int motion_removal_restores = same(film_c, base);
  if (!gpu) REQUIRE(motion_removal_restores);

  /* Cameras: projection types through the generic layer. */
  unsigned projections = 0, error_events = 0; int after_failure = -1;
  text(camera, "camera_type", "orthograph");
  /* The WebGPU device rejects non-perspective cameras (capability report);
   * its render-time failure path is exercised at the end of this test. */
  if (!gpu) { render(film_b); REQUIRE(!same(film_b, base)); projections++; }
  if (!gpu) { text(camera, "camera_type", "panorama"); text(camera, "panorama_type", "equirectangular"); render(film_b); REQUIRE(!same(film_b, base)); projections++; }
  text(camera, "camera_type", "perspective");
  render(film_c); if (!gpu) REQUIRE(same(film_c, base));

  /* Image updates: re-upload caller-decoded pixels to a bound handle. */
  uint8_t red[4 * 4 * 4], green[4 * 4 * 4];
  for (unsigned i = 0; i < 16; ++i) { uint8_t r[] = {230, 20, 20, 255}, g[] = {20, 230, 20, 255}; memcpy(red + 4 * i, r, 4); memcpy(green + 4 * i, g, 4); }
  CBCImageConfig image_config = {sizeof(image_config), 4, 4, CBC_IMAGE_BYTE4, {0}};
  CBCImage image = 0; CHECK(cbc_host_image_create(host, &image_config, red, sizeof(red), &image));
  CBCNode texture = graph_node(material, "image_texture"), coords = graph_node(material, "texture_coordinate");
  text(texture, "colorspace", "__builtin_srgb"); CHECK(cbc_host_image_bind(host, texture, image));
  CHECK(cbc_host_connect(host, coords, "Generated", texture, "Vector"));
  CHECK(cbc_host_connect(host, texture, "Color", diffuse, "Color"));
  float first[FLOATS], second[FLOATS], third[FLOATS];
  render(first); CHECK(cbc_host_image_update(host, image, &image_config, green, sizeof(green)));
  render(second); REQUIRE(!same(first, second));
  CBCImageConfig bad = image_config; bad.struct_size--; REQUIRE(cbc_host_image_update(host, image, &bad, red, sizeof(red)) == CBC_INVALID);
  bad = image_config; bad.reserved[1] = 1; REQUIRE(cbc_host_image_update(host, image, &bad, red, sizeof(red)) == CBC_INVALID);
  REQUIRE(cbc_host_image_update(host, image, &image_config, red, sizeof(red) - 1) == CBC_INVALID);
  REQUIRE(cbc_host_image_update(host, image + 1000, &image_config, red, sizeof(red)) == CBC_INVALID);
  /* A size/format change on the same handle. */
  float gray[2 * 2]; for (unsigned i = 0; i < 4; ++i) gray[i] = .5f;
  CBCImageConfig gray_config = {sizeof(gray_config), 2, 2, CBC_IMAGE_FLOAT, {0}};
  CHECK(cbc_host_image_update(host, image, &gray_config, gray, sizeof(gray))); render(third); REQUIRE(!same(third, second));
  CHECK(cbc_host_image_update(host, image, &image_config, red, sizeof(red))); render(third);
  const int image_update_restores = same(first, third); if (!gpu) REQUIRE(image_update_restores);
  /* Released handles cannot be updated; the binding keeps its pixels. */
  CHECK(cbc_host_image_release(host, image));
  REQUIRE(cbc_host_image_update(host, image, &image_config, green, sizeof(green)) == CBC_INVALID);
  render(third); if (!gpu) REQUIRE(same(first, third));
  CHECK(cbc_host_delete_node(host, texture)); CHECK(cbc_host_delete_node(host, coords));
  CHECK(cbc_host_connect(host, value, "Color", diffuse, "Color"));

  /* Passes/AOVs: enable through "pass" nodes and read back as copies. */
  CBCNode depth = create("pass"), normal = 0, aov = 0, aov_output = 0;
  text(depth, "type", "depth"); text(depth, "name", "Depth");
  if (!gpu) {
    normal = create("pass"); aov = create("pass");
    text(normal, "type", "normal"); text(normal, "name", "Normal");
    text(aov, "type", "aov_value"); text(aov, "name", "Mask");
    aov_output = graph_node(material, "aov_output"); text(aov_output, "name", "Mask"); scalar(aov_output, "value", .25f);
  } else CHECK(cbc_host_delete_node(host, depth));
  float combined[FLOATS], depth_values[W * H], normal_values[W * H * 3], mask[W * H];
  int depth_status = CBC_FAILED, normal_status = CBC_FAILED, mask_status = CBC_FAILED;
  CBCRenderConfig pass_config = {sizeof(pass_config), W, H, 8, {0}};
  /* WebGPU rejects surface data passes; that failing render runs last below. */
  const int data_pass_render = gpu ? CBC_FAILED : cbc_host_render(host, &pass_config, combined, FLOATS);
  if (!gpu) CHECK(data_pass_render);
  if (data_pass_render == CBC_OK) {
    char *passes = json_of(cbc_host_render_passes_json, host); save(prefix, "passes.json", passes, strlen(passes)); free(passes);
    REQUIRE(cbc_host_pass_floats(host, "Depth", NULL, 0, &required, &components) == CBC_SMALL_BUFFER && required == W * H && components == 1);
    depth_status = cbc_host_pass_floats(host, "Depth", depth_values, W * H, &required, &components);
    normal_status = cbc_host_pass_floats(host, "Normal", normal_values, W * H * 3, &required, &components);
    mask_status = cbc_host_pass_floats(host, "Mask", mask, W * H, &required, &components);
    if (!gpu) REQUIRE(depth_status == CBC_OK && normal_status == CBC_OK && mask_status == CBC_OK);
    if (depth_status == CBC_OK) save(prefix, "depth.f32", depth_values, sizeof(depth_values));
    if (normal_status == CBC_OK) save(prefix, "normal.f32", normal_values, sizeof(normal_values));
    if (mask_status == CBC_OK) save(prefix, "mask.f32", mask, sizeof(mask));
  }
  if (!gpu) { CHECK(cbc_host_delete_node(host, aov_output)); CHECK(cbc_host_delete_node(host, normal)); CHECK(cbc_host_delete_node(host, aov)); CHECK(cbc_host_delete_node(host, depth)); }
  /* Light passes (CPU). The WebGPU device rejected diffuse_direct/background
   * passes with "surface data passes not implemented" during development, and
   * a device error is sticky, so GPU mode covers only the rejection below. */
  CBCNode direct = create("pass"), sky = create("pass");
  text(direct, "type", "diffuse_direct"); text(direct, "name", "DiffuseDirect");
  text(sky, "type", "background"); text(sky, "name", "Background");
  float direct_values[W * H * 3], sky_values[W * H * 4];
  if (gpu) { CHECK(cbc_host_delete_node(host, direct)); CHECK(cbc_host_delete_node(host, sky)); direct = sky = 0; }
  const int light_pass_render = gpu ? CBC_FAILED : cbc_host_render(host, &pass_config, combined, FLOATS);
  int direct_status = CBC_FAILED, sky_status = CBC_FAILED;
  if (!gpu) CHECK(light_pass_render);
  if (light_pass_render == CBC_OK) {
    save(prefix, "light-passes.rgba.bin", combined, sizeof(combined));
    char *passes = json_of(cbc_host_render_passes_json, host); save(prefix, "light-passes.json", passes, strlen(passes)); free(passes);
    direct_status = cbc_host_pass_floats(host, "DiffuseDirect", direct_values, W * H * 3, &required, &components);
    if (direct_status == CBC_OK) { REQUIRE(components == 3); save(prefix, "diffuse-direct.f32", direct_values, sizeof(direct_values)); }
    sky_status = cbc_host_pass_floats(host, "Background", sky_values, W * H * 4, &required, &components);
    if (sky_status == CBC_OK) save(prefix, "background.f32", sky_values, (size_t)required * sizeof(float));
  }
  if (!gpu) { CHECK(cbc_host_delete_node(host, direct)); CHECK(cbc_host_delete_node(host, sky)); }

  /* Events: progress/status, reentrancy refusal, version from a callback, cancellation by return. */
  Events events = {0};
  CHECK(cbc_host_set_event_callback(host, on_event, &events));
  render(film_b);
  REQUIRE(events.progress > 0 && events.status > 0 && events.total == 8 && events.last_done == 8 && events.error == 0);
  REQUIRE(events.refused == events.progress + events.status && events.version_ok == events.refused);
  const unsigned first_progress = events.progress;
  Events cancel = {0}; cancel.cancel_at = 1;
  CHECK(cbc_host_set_event_callback(host, on_event, &cancel));
  CBCRenderConfig long_render = {sizeof(long_render), W, H, 100000, {0}};
  for (unsigned i = 0; i < FLOATS; ++i) film_c[i] = -99;
  REQUIRE(cbc_host_render(host, &long_render, film_c, FLOATS) == CBC_CANCELLED);
  for (unsigned i = 0; i < FLOATS; ++i) REQUIRE(film_c[i] == -99);
  REQUIRE(cancel.progress > 0 && cancel.total == 100000);
  REQUIRE(cbc_host_pass_floats(host, "Combined", pass, FLOATS, &required, &components) == CBC_NOT_READY);
  CHECK(cbc_host_set_event_callback(host, NULL, &events));
  render(film_c); REQUIRE(events.progress == first_progress);

  /* Lifetime: deleting the last references, then geometry and shaders. */
  REQUIRE(cbc_host_delete_node(host, mesh) == CBC_INVALID);
  CHECK(cbc_host_delete_node(host, object)); CHECK(cbc_host_delete_node(host, mesh));
  REQUIRE(cbc_host_delete_node(host, material) == CBC_OK && cbc_host_delete_node(host, diffuse) == CBC_INVALID);
  render(film_c);
  if (gpu) {
    /* Render-time device rejection: CBC_FAILED, one error event, output and
     * pass results untouched/cleared. After the cause is removed the next
     * render recovers the device (ABI.md "Device errors") and reports it with
     * a status event. */
    Events failure = {0}; CHECK(cbc_host_set_event_callback(host, on_event, &failure));
    CBCNode surface = create("shader"), bsdf = graph_node(surface, "diffuse_bsdf");
    CHECK(cbc_host_connect(host, bsdf, "BSDF", output_of(surface), "Surface"));
    mesh = create("mesh"); nodes(mesh, "used_shaders", surface); CHECK(cbc_host_mesh(host, mesh, xyz, 3, tri, 1));
    object = create("object"); nodes(object, "geometry", mesh);
    depth = create("pass"); text(depth, "type", "depth"); text(depth, "name", "Depth");
    for (unsigned i = 0; i < FLOATS; ++i) film_b[i] = -99;
    REQUIRE(cbc_host_render(host, &pass_config, film_b, FLOATS) == CBC_FAILED);
    for (unsigned i = 0; i < FLOATS; ++i) REQUIRE(film_b[i] == -99);
    REQUIRE(failure.error == 1 && failure.refused == failure.status + failure.progress + failure.error);
    REQUIRE(cbc_host_pass_floats(host, "Combined", pass, FLOATS, &required, &components) == CBC_NOT_READY);
    REQUIRE(cbc_host_render_passes_json(host, NULL, 0, &required) == CBC_NOT_READY);
    CHECK(cbc_host_delete_node(host, depth));
    after_failure = cbc_host_render(host, &pass_config, film_b, FLOATS);
    REQUIRE(after_failure == CBC_OK && failure.error == 1);
    for (unsigned i = 0; i < FLOATS; ++i) REQUIRE(isfinite(film_b[i]));
    error_events = failure.error;
  }
  CHECK(cbc_host_destroy(host));
  REQUIRE(cbc_host_delete_node(host, world) == CBC_INVALID && cbc_host_backend(host, &backend) == CBC_INVALID);
  REQUIRE(cbc_host_set_event_callback(host, on_event, &events) == CBC_INVALID);
  printf("{\"passed\":true,\"backend\":\"%s\",\"libraryVersion\":\"%s\",\"cyclesRevision\":\"%s\",\"renders\":%.0f,"
         "\"lightTypes\":%u,\"litRenders\":%u,\"projections\":%u,\"lightDeletionRestores\":%s,\"graphRebuildRestores\":%s,"
         "\"motionRemovalRestores\":%s,\"imageUpdateRestores\":%s,\"depthStatus\":%d,\"normalStatus\":%d,\"aovStatus\":%d,"
         "\"progressEvents\":%u,\"statusEvents\":%u,\"refusedReentry\":%u,\"cancelledByCallback\":true,\"errorEvents\":%u,"
         "\"dataPassRender\":%d,\"lightPassRender\":%d,\"diffuseDirectStatus\":%d,\"backgroundStatus\":%d,\"renderAfterDeviceFailure\":%d}\n",
         gpu ? "gpu" : "cpu", version.library_version, version.cycles_revision, render_count, light_types, lit_renders, projections,
         light_deletion_restores ? "true" : "false", graph_rebuild_restores ? "true" : "false",
         motion_removal_restores ? "true" : "false", image_update_restores ? "true" : "false",
         depth_status, normal_status, mask_status, events.progress, events.status, events.refused, error_events,
         data_pass_render, light_pass_render, direct_status, sky_status, after_failure);
  return 0;
}
