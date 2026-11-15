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
/* --progressive (PROVISIONAL CBC_HOST_PROGRESSIVE_RESULTS): counts the film events of each render and keeps the last
 * film, which must equal the returned film bit for bit (both read the render buffers after the last samples). */
typedef struct Films { unsigned count, total; int complete; uint64_t samples; float last[FLOAT_COUNT]; } Films;
static int on_film(void *user_data, const CBCEvent *event) {
  Films *films = user_data;
  if (event->struct_size < sizeof(CBCEvent) || event->kind != CBC_EVENT_FILM) return 0;
  if (!event->film || event->width != 32 || event->height != 24 || event->samples_done < films->samples) films->complete = 0;
  else memcpy(films->last, event->film, sizeof(films->last));
  films->samples = event->samples_done; ++films->count; ++films->total;
  return 0;
}
static void begin_films(Films *films) { films->count = 0; films->complete = 1; films->samples = 0; }
static void check_films(const Films *films, const float *pixels, uint64_t samples) {
  REQUIRE(films->count >= 2 && films->complete && films->samples == samples);
  REQUIRE(memcmp(films->last, pixels, sizeof(films->last)) == 0);
}
/* --tiled and --tiled-exit (PROVISIONAL cbc_host_image_create_tiled): a 128 x 128 byte4 image and its 64 x 64 box level,
 * served by a C reader, rows bottom first. The same pixels bound plainly render the same film bit for bit (Closest,
 * magnified: level 0); a failing read fails the render by name and the next render reads again; release runs exactly
 * once (on a failed call at once, on cbc_host_destroy, and with --tiled-exit at process exit, where it prints the row). */
enum { TILED = 128 };
static uint8_t tiled_levels[2][TILED * TILED * 4];
typedef struct Reader { unsigned reads, releases, failing, reentry_status; char reentry_error[256]; CBCHost host; } Reader;
static void tiled_pixels(void) {
  for (unsigned y = 0; y < TILED; ++y) for (unsigned x = 0; x < TILED; ++x) {
    uint8_t *p = tiled_levels[0] + (y * TILED + x) * 4;
    p[0] = (uint8_t)(x * 37 + y * 11); p[1] = (uint8_t)((x * 5) ^ (y * 29)); p[2] = (uint8_t)(x + y); p[3] = 255;
  }
  for (unsigned y = 0; y < TILED / 2; ++y) for (unsigned x = 0; x < TILED / 2; ++x) for (unsigned c = 0; c < 4; ++c) {
    unsigned sum = 0;
    for (unsigned j = 0; j < 2; ++j) for (unsigned i = 0; i < 2; ++i) sum += tiled_levels[0][((2 * y + j) * TILED + 2 * x + i) * 4 + c];
    tiled_levels[1][(y * (TILED / 2) + x) * 4 + c] = (uint8_t)((sum + 2) / 4);
  }
}
static int tile_read(void *user_data, uint32_t level, uint32_t x, uint32_t y, uint32_t width, uint32_t height, void *pixels, uint64_t row_bytes) {
  Reader *reader = user_data; ++reader->reads;
  if (reader->failing) return 9;
  if (reader->host && !reader->reentry_status) {
    /* A call into the library from the reader is refused by name. */
    reader->reentry_status = (unsigned)cbc_host_cancel(reader->host);
    uint64_t required = 0; cbc_host_error(reader->reentry_error, sizeof(reader->reentry_error), &required);
  }
  if (level > 1) return 1;
  const uint32_t side = TILED >> level;
  if (x + width > side || y + height > side) return 1;
  for (uint32_t row = 0; row < height; ++row)
    memcpy((uint8_t *)pixels + row * row_bytes, tiled_levels[level] + ((y + row) * side + x) * 4, (size_t)width * 4);
  return 0;
}
static void tile_release(void *user_data) { ++((Reader *)user_data)->releases; }
static Reader exit_reader;
static void release_at_exit(void *user_data) {
  Reader *reader = user_data; ++reader->releases;
  printf("{\"passed\":%s,\"releasedAtProcessExit\":true,\"releases\":%u}\n", reader->releases == 1 ? "true" : "false", reader->releases);
  fflush(stdout);
}
/* An orthographic view of a quad whose emission is the image texture's colour (Closest), UVs 0.35 to 0.65. */
static CBCNode tiled_scene(CBCHost host) {
  CBCNode integrator = singleton(host, "integrator");
  integer(host, integrator, "use_guiding", 0); integer(host, integrator, "use_light_tree", 0);
  integer(host, integrator, "use_adaptive_sampling", 0); integer(host, integrator, "use_denoise", 0);
  integer(host, integrator, "max_bounce", 0);
  CHECK(cbc_host_set_string(host, integrator, "sampling_pattern", "tabulated_sobol"));
  CBCNode camera = singleton(host, "camera"); CHECK(cbc_host_set_string(host, camera, "camera_type", "orthograph"));
  float transform[] = {1,0,0,0, 0,1,0,0, 0,0,1,-4};
  CHECK(cbc_host_set_floats(host, camera, "matrix", transform, 12));
  CBCNode material = create(host, "shader"), output;
  CHECK(cbc_host_shader_output(host, material, &output));
  CBCNode uv = shader_node(host, material, "uvmap"), texture = shader_node(host, material, "image_texture"), emission = shader_node(host, material, "emission");
  CHECK(cbc_host_set_string(host, texture, "interpolation", "closest"));
  CHECK(cbc_host_connect(host, uv, "UV", texture, "Vector"));
  CHECK(cbc_host_connect(host, texture, "Color", emission, "Color"));
  CHECK(cbc_host_connect(host, emission, "Emission", output, "Surface"));
  CHECK(cbc_host_set_string(host, material, "emission_sampling_method", "none"));
  CBCNode mesh = create(host, "mesh");
  CHECK(cbc_host_set_nodes(host, mesh, "used_shaders", &material, 1));
  float xyz[] = {-2,-2,0, 2,-2,0, 2,2,0, -2,2,0}; uint32_t indices[] = {0,1,2, 0,2,3};
  CHECK(cbc_host_mesh(host, mesh, xyz, 4, indices, 2));
  float uvs[] = {.35f,.35f, .65f,.35f, .65f,.65f, .35f,.35f, .65f,.65f, .35f,.65f};
  CBCAttributeConfig attribute = {sizeof(attribute), CBC_VALUE_POINT2, CBC_ATTR_CORNER, CBC_ATTR_STANDARD, {0}};
  CHECK(cbc_host_attribute_floats(host, mesh, "uv", &attribute, uvs, 12));
  CBCNode object = create(host, "object"); CHECK(cbc_host_set_nodes(host, object, "geometry", &mesh, 1));
  return texture;
}
static int tiled_main(const char *prefix, int at_exit) {
  CBCHostConfig config = {sizeof(config), CBC_HOST_ABI, CBC_HOST_CPU_REFERENCE, 1, {0}, 0, 0};
  CBCImageConfig image_config = {sizeof(image_config), TILED, TILED, CBC_IMAGE_BYTE4, 0, {0}};
  CBCRenderConfig render = {sizeof(render), 32, 24, 4, {0}};
  static float plain[FLOAT_COUNT], tiled[FLOAT_COUNT], pixels[FLOAT_COUNT];
  tiled_pixels();
  CBCHost host = 0; CHECK(cbc_host_create(&config, &host));
  CBCNode texture = tiled_scene(host);
  CBCImage image = 0;
  CHECK(cbc_host_image_create(host, &image_config, tiled_levels[0], sizeof(tiled_levels[0]), &image));
  CHECK(cbc_host_image_bind(host, texture, image)); CHECK(cbc_host_image_release(host, image));
  CHECK(cbc_host_render(host, &render, plain, FLOAT_COUNT)); CHECK(cbc_host_destroy(host));
  if (at_exit) {
    /* The host stays live at exit: the exit hook destroys it and release runs then. */
    CHECK(cbc_host_create(&config, &host)); texture = tiled_scene(host);
    CHECK(cbc_host_image_create_tiled(host, &image_config, tile_read, release_at_exit, &exit_reader, &image));
    CHECK(cbc_host_image_bind(host, texture, image)); CHECK(cbc_host_render(host, &render, tiled, FLOAT_COUNT));
    REQUIRE(memcmp(plain, tiled, sizeof(plain)) == 0 && exit_reader.reads > 0 && exit_reader.releases == 0);
    puts("{\"passed\":true,\"tiledImageLiveAtExit\":true}"); fflush(stdout);
    return 0;
  }
  static Reader invalid, absent, reader, failing;
  CHECK(cbc_host_create(&config, &host)); texture = tiled_scene(host);
  /* A call that fails releases at once: a missing reader, an unknown format. */
  REQUIRE(cbc_host_image_create_tiled(host, &image_config, NULL, tile_release, &absent, &image) == CBC_INVALID && image == 0 && absent.releases == 1);
  image_config.format = 99;
  REQUIRE(cbc_host_image_create_tiled(host, &image_config, tile_read, tile_release, &invalid, &image) == CBC_INVALID && invalid.releases == 1);
  image_config.format = CBC_IMAGE_BYTE4;
  reader.host = host;
  CHECK(cbc_host_image_create_tiled(host, &image_config, tile_read, tile_release, &reader, &image));
  REQUIRE(cbc_host_image_update(host, image, &image_config, tiled_levels[0], sizeof(tiled_levels[0])) == CBC_INVALID);
  CHECK(cbc_host_image_bind(host, texture, image)); CHECK(cbc_host_image_release(host, image));
  CHECK(cbc_host_render(host, &render, tiled, FLOAT_COUNT));
  REQUIRE(reader.reads > 0 && reader.releases == 0 && reader.reentry_status == CBC_NOT_READY);
  REQUIRE(strcmp(reader.reentry_error, "Host functions cannot be called from a tiled image's read callback") == 0);
  REQUIRE(memcmp(plain, tiled, sizeof(plain)) == 0);
  const unsigned plain_reads = reader.reads;
  CHECK(cbc_host_render(host, &render, pixels, FLOAT_COUNT)); REQUIRE(reader.reads == plain_reads && memcmp(pixels, tiled, sizeof(pixels)) == 0);
  /* A failing reader fails the render by name, leaves the output untouched, and its tiles are read again next time. */
  failing.failing = 1;
  CHECK(cbc_host_image_create_tiled(host, &image_config, tile_read, tile_release, &failing, &image));
  CHECK(cbc_host_image_bind(host, texture, image)); CHECK(cbc_host_image_release(host, image));
  REQUIRE(reader.releases == 0);
  for (unsigned i = 0; i < FLOAT_COUNT; ++i) pixels[i] = -99;
  REQUIRE(cbc_host_render(host, &render, pixels, FLOAT_COUNT) == CBC_FAILED);
  char error[512]; uint64_t required = 0; cbc_host_error(error, sizeof(error), &required);
  REQUIRE(strncmp(error, "Tiled image ", 12) == 0 && strstr(error, ": the tile reader failed (returned 9; level 0, ") != NULL);
  for (unsigned i = 0; i < FLOAT_COUNT; ++i) REQUIRE(pixels[i] == -99);
  /* The first image's binding was replaced and Cycles freed its image at that render: released once. */
  REQUIRE(reader.releases == 1);
  failing.failing = 0; const unsigned failed_reads = failing.reads;
  CHECK(cbc_host_render(host, &render, pixels, FLOAT_COUNT));
  REQUIRE(failing.reads > failed_reads && memcmp(pixels, plain, sizeof(pixels)) == 0 && failing.releases == 0);
  CHECK(cbc_host_destroy(host));
  REQUIRE(failing.releases == 1 && reader.releases == 1);
  char path[4096];
  REQUIRE(snprintf(path, sizeof(path), "%s.plain.rgba.bin", prefix) > 0);
  FILE *file = fopen(path, "wb"); REQUIRE(file); REQUIRE(fwrite(plain, sizeof(float), FLOAT_COUNT, file) == FLOAT_COUNT); REQUIRE(fclose(file) == 0);
  REQUIRE(snprintf(path, sizeof(path), "%s.tiled.rgba.bin", prefix) > 0);
  file = fopen(path, "wb"); REQUIRE(file); REQUIRE(fwrite(tiled, sizeof(float), FLOAT_COUNT, file) == FLOAT_COUNT); REQUIRE(fclose(file) == 0);
  printf("{\"passed\":true,\"tiledImages\":true,\"tiledEqualsPlainBitwise\":true,\"reads\":%u,\"failedReadFailsByName\":true,"
         "\"readAgainAfterFailure\":true,\"releasedOnFailedCalls\":true,\"releasedOncePerImage\":true,\"reentryRefused\":true,\"updateRefused\":true}\n", plain_reads);
  return 0;
}
/* --tiles (PROVISIONAL cbc_host_tile_size): the same lifecycle rendered in tiles of 8 pixels (12 tiles of the 32 x 24
 * film), the finished tiles kept in memory by the library. Sizes Cycles would change are refused; the device report
 * counts the tiles kept in memory and the full frames read back, and holds none between renders. */
static uint64_t report_number(CBCHost host, const char *key) {
  uint64_t required = 0;
  REQUIRE(cbc_host_device_report_json(host, NULL, 0, &required) == CBC_SMALL_BUFFER && required > 0);
  char *json = malloc(required); REQUIRE(json);
  CHECK(cbc_host_device_report_json(host, json, required, &required));
  char pattern[128]; REQUIRE(snprintf(pattern, sizeof(pattern), "\"%s\":", key) > 0);
  const char *at = strstr(json, pattern); REQUIRE(at != NULL);
  const uint64_t value = strtoull(at + strlen(pattern), NULL, 10);
  free(json); return value;
}
static void check_tiles(CBCHost host, uint64_t kept, uint64_t frames) {
  REQUIRE(report_number(host, "tileSize") == 8 && report_number(host, "tilesKeptInMemory") == kept);
  REQUIRE(report_number(host, "framesReadBack") == frames && report_number(host, "tilesHeld") == 0);
}
static void begin_tiles(CBCHost host) {
  REQUIRE(cbc_host_tile_size(host, 7) == CBC_INVALID && cbc_host_tile_size(host, 129) == CBC_INVALID);
  REQUIRE(cbc_host_tile_size(host, 8193) == CBC_INVALID && cbc_host_tile_size(host, 200) == CBC_INVALID);
  char error[256]; uint64_t required = 0; cbc_host_error(error, sizeof(error), &required);
  REQUIRE(strcmp(error, "Tile size must be 0 (one tile), 8 to 128, or a multiple of 128 up to 8192") == 0);
  CHECK(cbc_host_tile_size(host, 8)); check_tiles(host, 0, 0);
}
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
  if (argc == 3 && (strcmp(argv[2], "--tiled") == 0 || strcmp(argv[2], "--tiled-exit") == 0)) return tiled_main(argv[1], strcmp(argv[2], "--tiled-exit") == 0);
  REQUIRE(argc == 2 || (argc == 3 && (strcmp(argv[2], "--exit-live") == 0 || strcmp(argv[2], "--progressive") == 0 ||
                                      strcmp(argv[2], "--tiles") == 0)));
  const int progressive = argc == 3 && strcmp(argv[2], "--progressive") == 0;
  const int tiles = argc == 3 && strcmp(argv[2], "--tiles") == 0;
  static Films films;
  CBCHostConfig config = {sizeof(config), CBC_HOST_ABI, CBC_HOST_CPU_REFERENCE, 1, {0}, 0, 0};
  CBCHost host = 99, other = 0;
  config.abi_version++;
  REQUIRE(cbc_host_create(&config, &host) == CBC_INVALID && host == 0);
  config.abi_version--;
  if (progressive) {
    /* Unknown flags and a nonzero reserved2 are refused; the 32-byte configuration of earlier consumers is accepted. */
    config.flags = 2; REQUIRE(cbc_host_create(&config, &host) == CBC_INVALID && host == 0);
    config.flags = CBC_HOST_PROGRESSIVE_RESULTS; config.reserved2 = 1; REQUIRE(cbc_host_create(&config, &host) == CBC_INVALID && host == 0);
    config.reserved2 = 0; config.struct_size = 32; CHECK(cbc_host_create(&config, &host)); CHECK(cbc_host_destroy(host));
    config.struct_size = sizeof(config);
  }
  CHECK(cbc_host_create(&config, &host)); CHECK(cbc_host_create(&config, &other));
  if (progressive) CHECK(cbc_host_set_event_callback(host, on_film, &films));
  if (tiles) begin_tiles(host);
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
    begin_films(&films);
    CHECK(cbc_host_render(host, &render, pixels, FLOAT_COUNT));
    if (progressive) check_films(&films, pixels, render.samples); else REQUIRE(films.count == 0);
    if (tiles) check_tiles(host, 12 * (round + 1), round + 1);
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
  if (tiles) check_tiles(host, 60, 5);
  if (argc == 3 && !progressive && !tiles) {
    puts("{\"passed\":true,\"processExitWithLiveContext\":true}");
    return 0;
  }
  RenderThread thread = {host, render, {0}, -1}; thread.config.samples = 1000000;
  for (unsigned i = 0; i < FLOAT_COUNT; ++i) thread.pixels[i] = -99;
  pthread_t worker; REQUIRE(pthread_create(&worker, NULL, render_worker, &thread) == 0);
  wait_active(host); CHECK(cbc_host_cancel(host)); REQUIRE(pthread_join(worker, NULL) == 0);
  REQUIRE(thread.status == CBC_CANCELLED);
  for (unsigned i = 0; i < FLOAT_COUNT; ++i) REQUIRE(thread.pixels[i] == -99);
  /* A cancelled tiled render reads no frame back and leaves no tile in memory. */
  uint64_t kept_after_cancel = 0;
  if (tiles) {
    REQUIRE(report_number(host, "framesReadBack") == 5 && report_number(host, "tilesHeld") == 0);
    kept_after_cancel = report_number(host, "tilesKeptInMemory"); REQUIRE(kept_after_cancel >= 60 && kept_after_cancel <= 61);
  }
  CHECK(cbc_host_render(host, &render, pixels, FLOAT_COUNT));
  REQUIRE(memcmp(previous, pixels, sizeof(pixels)) == 0);
  if (tiles) check_tiles(host, kept_after_cancel + 12, 6);
  REQUIRE(pthread_create(&worker, NULL, render_worker, &thread) == 0);
  wait_active(host); CHECK(cbc_host_destroy(host)); REQUIRE(pthread_join(worker, NULL) == 0);
  REQUIRE(thread.status == CBC_CANCELLED);
  for (unsigned i = 0; i < FLOAT_COUNT; ++i) REQUIRE(thread.pixels[i] == -99);
  REQUIRE(cbc_host_destroy(host) == CBC_INVALID);
  if (tiles) {
    printf("{\"passed\":true,\"pureCConsumer\":true,\"explicitCpuReference\":true,\"tiledRendering\":true,\"tileSize\":8,"
           "\"tilesKeptInMemory\":%llu,\"framesReadBack\":6,\"invalidSizesRefused\":true,\"cancelPreservesOutput\":true,"
           "\"destroyDuringRender\":true}\n", (unsigned long long)(kept_after_cancel + 12));
    return 0;
  }
  if (progressive) {
    printf("{\"passed\":true,\"pureCConsumer\":true,\"explicitCpuReference\":true,\"progressiveResults\":true,\"filmEvents\":%u,"
           "\"lastFilmEqualsReturnedFilm\":true,\"unknownFlagsRejected\":true,\"earlierConfigurationSizeAccepted\":true}\n", films.total);
    return 0;
  }
  puts("{\"passed\":true,\"pureCConsumer\":true,\"explicitCpuReference\":true,\"materialEditAfterCompilation\":true,\"repeatBitwiseIdentical\":true,\"sampleBudgetDecrease\":true,\"failedRenderPreservesOutput\":true,\"cancelPreservesOutput\":true,\"renderAfterCancel\":true,\"destroyDuringRender\":true,\"foreignAndStaleHandlesRejected\":true}");
  return 0;
}
