// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Phase E3 scene-edit and device-recovery consumer for native/tools/scene_edit_gate.py.
// Every scenario renders a base scene, edits it through the public C ABI after
// that first render, renders again and saves the film. The same scenario runs
// a second time in a new host ("replay"), and a fresh CPU host renders the
// edited scene from scratch (built with the same node creation order) as the
// oracle. argv: output prefix, "cpu" or "gpu". GPU mode also provokes the
// known WebGPU render-time rejections (a non-Combined pass, a non-perspective
// camera), removes the cause and renders again on the same host (recovery).
// Prints one JSON line; the gate compares the saved films.
#define _POSIX_C_SOURCE 200809L
#include "cycles_host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 48
#define H 36
#define SAMPLES 16
enum { FLOATS = W * H * 4, SCENARIOS = 13 };
static const char *names[SCENARIOS] = {
  "object-tfm", "object-motion-added", "camera-motion-added", "material-value", "light-edit",
  "deformation-motion-added", "mesh-replaced", "instance-added", "object-motion-removed", "tfm-round-trip",
  "attribute-replaced", "smooth-toggled", "motion-blur-enabled"};

static void check(int result, const char *call, int line) {
  if (result == CBC_OK) return;
  char error[4096]; uint64_t required = 0;
  cbc_host_error(error, sizeof(error), &required);
  fprintf(stderr, "line %d: %s returned %d: %s\n", line, call, result, error); exit(1);
}
#define CHECK(call) check((call), #call, __LINE__)
#define REQUIRE(test) do { if (!(test)) { fprintf(stderr, "Failed at line %d: %s\n", __LINE__, #test); exit(1); } } while (0)

typedef struct Params {
  float subject_tfm[12];
  int object_motion, camera_motion, deform, alternate_mesh, instance, smooth, motion_blur;
  float color[3], light_strength, light_tfm[12], tint;
} Params;
typedef struct Scene {
  CBCHost host;
  CBCNode integrator, camera, value, subject_mesh, subject, floor_mesh, light, light_holder, instance;
} Scene;

static CBCNode create(CBCHost host, const char *type) { CBCNode node = 0; CHECK(cbc_host_create_node(host, type, &node)); return node; }
static CBCNode singleton(CBCHost host, const char *name) { CBCNode node = 0; CHECK(cbc_host_scene_node(host, name, &node)); return node; }
static CBCNode graph_node(CBCHost host, CBCNode shader, const char *type) { CBCNode node = 0; CHECK(cbc_host_shader_node(host, shader, type, &node)); return node; }
static CBCNode output_of(CBCHost host, CBCNode shader) { CBCNode node = 0; CHECK(cbc_host_shader_output(host, shader, &node)); return node; }
static void integer(CBCHost host, CBCNode node, const char *name, int64_t value) { CHECK(cbc_host_set_ints(host, node, name, &value, 1)); }
static void scalar(CBCHost host, CBCNode node, const char *name, float value) { CHECK(cbc_host_set_floats(host, node, name, &value, 1)); }
static void triple(CBCHost host, CBCNode node, const char *name, const float *v) { CHECK(cbc_host_set_floats(host, node, name, v, 3)); }
static void nodes(CBCHost host, CBCNode node, const char *name, CBCNode value) { CHECK(cbc_host_set_nodes(host, node, name, &value, 1)); }
static void matrix(float *m, float sx, float sy, float sz, float x, float y, float z, float angle) {
  const float c = cosf(angle), s = sinf(angle);
  float v[] = {c * sx, 0, s * sz, x, 0, sy, 0, y, -s * sx, 0, c * sz, z}; memcpy(m, v, sizeof(v));
}

/* Subject: a closed six-triangle bipyramid; alternate positions for mesh replacement. */
static const float bipyramid[2][18] = {
  {0,.9f,0, -.7f,0,-.4f, .7f,0,-.4f, 0,0,.7f, 0,-.6f,0, .1f,.2f,.1f},
  {0,1.1f,.1f, -.8f,.1f,-.3f, .6f,-.1f,-.5f, .1f,0,.8f, 0,-.7f,.1f, .1f,.2f,.1f}};
static const uint32_t bipyramid_triangles[] = {0,1,2, 0,2,3, 0,3,1, 4,2,1, 4,3,2, 4,1,3};
static void subject_geometry(Scene *s, const Params *p) {
  CHECK(cbc_host_mesh(s->host, s->subject_mesh, bipyramid[p->alternate_mesh], 5, bipyramid_triangles, 6));
  int64_t smooth[6]; for (unsigned i = 0; i < 6; ++i) smooth[i] = p->smooth;
  CHECK(cbc_host_set_ints(s->host, s->subject_mesh, "smooth", smooth, 6));
}
static void deformation(Scene *s, const Params *p) {
  float steps[3 * 15]; const float *center = bipyramid[p->alternate_mesh];
  for (unsigned i = 0; i < 15; ++i) { steps[i] = center[i] + (i % 3 == 0 ? -.15f : i % 3 == 1 ? .05f : 0); steps[15 + i] = center[i]; steps[30 + i] = center[i] + (i % 3 == 0 ? .15f : 0); }
  CHECK(cbc_host_motion_positions(s->host, s->subject_mesh, 3, steps, 45));
}
static void object_motion(Scene *s, CBCNode object, const float *tfm, int enabled) {
  if (!enabled) { CHECK(cbc_host_set_floats(s->host, object, "motion", NULL, 0)); return; }
  float steps[36]; memcpy(steps, tfm, 48); memcpy(steps + 12, tfm, 48); memcpy(steps + 24, tfm, 48);
  steps[3] -= .3f; steps[7] += .05f; steps[27] += .3f; steps[31] += .1f;
  CHECK(cbc_host_set_floats(s->host, object, "motion", steps, 36));
}
static void camera_motion(Scene *s, int enabled) {
  if (!enabled) { CHECK(cbc_host_set_floats(s->host, s->camera, "motion", NULL, 0)); return; }
  float steps[36] = {1,0,0,-.2f, 0,1,0,1, 0,0,1,-5, 1,0,0,0, 0,1,0,1, 0,0,1,-5, 1,0,0,.25f, 0,1,0,1.1f, 0,0,1,-5};
  CHECK(cbc_host_set_floats(s->host, s->camera, "motion", steps, 36));
}
static void floor_tint(Scene *s, float tint) {
  float values[4 * 3]; for (unsigned v = 0; v < 4; ++v) { values[3 * v] = tint; values[3 * v + 1] = .5f + .1f * v; values[3 * v + 2] = 1 - tint; }
  CBCAttributeConfig config = {sizeof(config), CBC_VALUE_COLOR, CBC_ATTR_VERTEX, 0, {0}};
  CHECK(cbc_host_attribute_floats(s->host, s->floor_mesh, "tint", &config, values, 12));
}

/* Builds a scene in a fixed node creation order so a fresh host can rebuild any edited state. */
static void build(Scene *s, uint32_t backend, const Params *p) {
  CBCHostConfig config = {sizeof(config), CBC_HOST_ABI, backend, 1, {0}};
  CHECK(cbc_host_create(&config, &s->host)); CBCHost h = s->host;
  s->integrator = singleton(h, "integrator"); s->camera = singleton(h, "camera");
  integer(h, s->integrator, "use_guiding", 0); integer(h, s->integrator, "use_light_tree", 0);
  integer(h, s->integrator, "use_adaptive_sampling", 0); integer(h, s->integrator, "use_denoise", 0);
  integer(h, s->integrator, "caustics_reflective", 0); integer(h, s->integrator, "caustics_refractive", 0);
  integer(h, s->integrator, "min_bounce", 2); integer(h, s->integrator, "max_bounce", 3); integer(h, s->integrator, "seed", 3);
  CHECK(cbc_host_set_string(h, s->integrator, "sampling_pattern", "tabulated_sobol"));
  integer(h, s->integrator, "motion_blur", p->motion_blur); scalar(h, s->camera, "shuttertime", .6f);
  scalar(h, s->camera, "fov", .95f);
  float cam[12] = {1,0,0,0, 0,1,0,1, 0,0,1,-5}; CHECK(cbc_host_set_floats(h, s->camera, "matrix", cam, 12));
  camera_motion(s, p->camera_motion);
  CBCNode world = create(h, "shader"), bg = graph_node(h, world, "background_shader");
  const float sky[3] = {.2f, .3f, .45f}; triple(h, bg, "color", sky); scalar(h, bg, "strength", .5f);
  CHECK(cbc_host_connect(h, bg, "Background", output_of(h, world), "Surface"));
  CHECK(cbc_host_set_string(h, world, "emission_sampling_method", "none"));
  nodes(h, singleton(h, "background"), "shader", world);
  /* Subject material: diffuse whose color comes from an editable color node. */
  CBCNode material = create(h, "shader"), diffuse = graph_node(h, material, "diffuse_bsdf");
  s->value = graph_node(h, material, "color"); triple(h, s->value, "value", p->color); scalar(h, diffuse, "roughness", .2f);
  CHECK(cbc_host_connect(h, s->value, "Color", diffuse, "Color"));
  CHECK(cbc_host_connect(h, diffuse, "BSDF", output_of(h, material), "Surface"));
  CHECK(cbc_host_set_string(h, material, "emission_sampling_method", "none"));
  /* Floor material: diffuse tinted by the per-vertex "tint" attribute. */
  CBCNode floor_material = create(h, "shader"), floor_diffuse = graph_node(h, floor_material, "diffuse_bsdf");
  CBCNode attribute = graph_node(h, floor_material, "attribute");
  CHECK(cbc_host_set_string(h, attribute, "attribute", "tint"));
  CHECK(cbc_host_connect(h, attribute, "Color", floor_diffuse, "Color"));
  CHECK(cbc_host_connect(h, floor_diffuse, "BSDF", output_of(h, floor_material), "Surface"));
  CHECK(cbc_host_set_string(h, floor_material, "emission_sampling_method", "none"));
  s->subject_mesh = create(h, "mesh"); nodes(h, s->subject_mesh, "used_shaders", material);
  subject_geometry(s, p);
  if (p->deform) deformation(s, p);
  s->subject = create(h, "object"); nodes(h, s->subject, "geometry", s->subject_mesh);
  CHECK(cbc_host_set_floats(h, s->subject, "tfm", p->subject_tfm, 12));
  object_motion(s, s->subject, p->subject_tfm, p->object_motion);
  s->floor_mesh = create(h, "mesh"); nodes(h, s->floor_mesh, "used_shaders", floor_material);
  const float quad[] = {-4,-1,-3, 4,-1,-3, 4,-1,3, -4,-1,3}; const uint32_t quad_triangles[] = {0,1,2, 0,2,3};
  CHECK(cbc_host_mesh(h, s->floor_mesh, quad, 4, quad_triangles, 2)); floor_tint(s, p->tint);
  CBCNode floor = create(h, "object"); nodes(h, floor, "geometry", s->floor_mesh);
  CBCNode emitter = create(h, "shader"), emission = graph_node(h, emitter, "emission");
  const float white[3] = {1, 1, 1}; triple(h, emission, "color", white); scalar(h, emission, "strength", 1);
  CHECK(cbc_host_connect(h, emission, "Emission", output_of(h, emitter), "Surface"));
  s->light = create(h, "pointlight"); nodes(h, s->light, "used_shaders", emitter);
  scalar(h, s->light, "radius", .25f); integer(h, s->light, "is_sphere", 1); integer(h, s->light, "use_mis", 1);
  const float strength[3] = {p->light_strength, p->light_strength * .9f, p->light_strength * .8f}; triple(h, s->light, "strength", strength);
  s->light_holder = create(h, "object"); nodes(h, s->light_holder, "geometry", s->light);
  CHECK(cbc_host_set_floats(h, s->light_holder, "tfm", p->light_tfm, 12));
  CBCNode sun = create(h, "sunlight"); nodes(h, sun, "used_shaders", emitter); scalar(h, sun, "angle", .1f);
  const float sun_strength[3] = {.6f, .6f, .55f}; triple(h, sun, "strength", sun_strength);
  CBCNode sun_holder = create(h, "object"); nodes(h, sun_holder, "geometry", sun);
  float sun_tfm[12]; matrix(sun_tfm, 1, 1, 1, 0, 0, 0, 2.6f); sun_tfm[5] = .6f; sun_tfm[6] = -.8f; sun_tfm[9] = .8f; sun_tfm[10] = .6f;
  CHECK(cbc_host_set_floats(h, sun_holder, "tfm", sun_tfm, 12));
  s->instance = 0;
  if (p->instance) {
    s->instance = create(h, "object"); nodes(h, s->instance, "geometry", s->subject_mesh);
    float t[12]; matrix(t, .6f, .7f, .6f, 1.6f, -.4f, .5f, .7f); CHECK(cbc_host_set_floats(h, s->instance, "tfm", t, 12));
  }
}
static void base_params(Params *p, int scenario) {
  memset(p, 0, sizeof(*p));
  matrix(p->subject_tfm, 1, 1.1f, .9f, -.3f, 0, .2f, .4f);
  matrix(p->light_tfm, 1, 1, 1, 1.8f, 2.2f, -1.5f, 0);
  const float color[3] = {.7f, .25f, .1f}; memcpy(p->color, color, sizeof(color));
  p->light_strength = 60; p->tint = .8f;
  /* Motion scenarios start with motion blur enabled; scenario 12 enables it later. */
  p->motion_blur = scenario == 1 || scenario == 2 || scenario == 5 || scenario == 8;
  p->object_motion = scenario == 8;
}
/* The edited state, as a fresh host builds it. */
static void edited_params(Params *p, int scenario) {
  base_params(p, scenario);
  switch (scenario) {
    case 0: matrix(p->subject_tfm, .8f, 1.3f, 1, .4f, .1f, -.3f, -.6f); break;
    case 1: p->object_motion = 1; break;
    case 2: p->camera_motion = 1; break;
    case 3: p->color[0] = .1f; p->color[1] = .6f; p->color[2] = .3f; break;
    case 4: p->light_strength = 35; matrix(p->light_tfm, 1, 1, 1, -1.5f, 2.6f, -1, 0); break;
    case 5: p->deform = 1; break;
    case 6: p->alternate_mesh = 1; break;
    case 7: p->instance = 1; break;
    case 8: p->object_motion = 0; break;
    case 9: break;
    case 10: p->tint = .2f; break;
    case 11: p->smooth = 1; break;
    case 12: p->motion_blur = 1; p->object_motion = 1; break;
  }
}
/* Applies scenario edits to a host that already rendered its base state. */
static void edit(Scene *s, int scenario, float *scratch) {
  Params p; edited_params(&p, scenario); CBCHost h = s->host;
  switch (scenario) {
    case 0: CHECK(cbc_host_set_floats(h, s->subject, "tfm", p.subject_tfm, 12)); break;
    case 1: object_motion(s, s->subject, p.subject_tfm, 1); break;
    case 2: camera_motion(s, 1); break;
    case 3: triple(h, s->value, "value", p.color); break;
    case 4: { const float strength[3] = {p.light_strength, p.light_strength * .9f, p.light_strength * .8f};
              triple(h, s->light, "strength", strength); CHECK(cbc_host_set_floats(h, s->light_holder, "tfm", p.light_tfm, 12)); break; }
    case 5: deformation(s, &p); break;
    case 6: subject_geometry(s, &p); break;
    case 7: { s->instance = create(h, "object"); nodes(h, s->instance, "geometry", s->subject_mesh);
              float t[12]; matrix(t, .6f, .7f, .6f, 1.6f, -.4f, .5f, .7f); CHECK(cbc_host_set_floats(h, s->instance, "tfm", t, 12)); break; }
    case 8: object_motion(s, s->subject, p.subject_tfm, 0); break;
    case 9: { /* Render a moved state in between, then return to the base transform. */
              float moved[12]; matrix(moved, 1.4f, .8f, 1, .5f, .3f, 0, 1.1f);
              CHECK(cbc_host_set_floats(h, s->subject, "tfm", moved, 12));
              CBCRenderConfig config = {sizeof(config), W, H, SAMPLES, {0}}; CHECK(cbc_host_render(h, &config, scratch, FLOATS));
              CHECK(cbc_host_set_floats(h, s->subject, "tfm", p.subject_tfm, 12)); break; }
    case 10: floor_tint(s, p.tint); break;
    case 11: { int64_t smooth[6] = {1, 1, 1, 1, 1, 1}; CHECK(cbc_host_set_ints(h, s->subject_mesh, "smooth", smooth, 6)); break; }
    case 12: integer(h, s->integrator, "motion_blur", 1); object_motion(s, s->subject, p.subject_tfm, 1); break;
  }
}
static void save(const char *prefix, const char *name, const float *data) {
  char path[4096]; REQUIRE(snprintf(path, sizeof(path), "%s.%s.rgba.bin", prefix, name) > 0);
  FILE *file = fopen(path, "wb"); REQUIRE(file); REQUIRE(fwrite(data, sizeof(float), FLOATS, file) == FLOATS); REQUIRE(fclose(file) == 0);
}
static void render(CBCHost host, float *film) {
  CBCRenderConfig config = {sizeof(config), W, H, SAMPLES, {0}};
  CHECK(cbc_host_render(host, &config, film, FLOATS));
  for (unsigned i = 0; i < FLOATS; ++i) REQUIRE(isfinite(film[i]));
}
/* One edit sequence: base render, edit, edited render. */
static void sequence(uint32_t backend, int scenario, float *before, float *after) {
  Params p; base_params(&p, scenario); Scene s; build(&s, backend, &p);
  render(s.host, before); float scratch[FLOATS]; edit(&s, scenario, scratch); render(s.host, after);
  CHECK(cbc_host_destroy(s.host));
}

/* Device-error recovery events. */
typedef struct Events { unsigned status, errors, recovered; } Events;
static int on_event(void *data, const CBCEvent *event) {
  Events *events = data;
  if (event->kind == CBC_EVENT_STATUS) { events->status++; if (event->message && strstr(event->message, "recovered")) events->recovered++; }
  if (event->kind == CBC_EVENT_ERROR) events->errors++;
  return 0;
}

int main(int argc, char **argv) {
  REQUIRE(argc == 3 && (strcmp(argv[2], "cpu") == 0 || strcmp(argv[2], "gpu") == 0));
  const int gpu = strcmp(argv[2], "gpu") == 0; const char *prefix = argv[1];
  const uint32_t backend = gpu ? CBC_HOST_GPU : CBC_HOST_CPU_REFERENCE;
  static float before[FLOATS], after[FLOATS], replay_before[FLOATS], replay_after[FLOATS], fresh[FLOATS], base[FLOATS];
  unsigned changed = 0;
  for (int scenario = 0; scenario < SCENARIOS; ++scenario) {
    fprintf(stderr, "scenario %d %s\n", scenario, names[scenario]);
    char name[256];
    sequence(backend, scenario, before, after);
    sequence(backend, scenario, replay_before, replay_after);
    Params p; edited_params(&p, scenario); Scene s; build(&s, CBC_HOST_CPU_REFERENCE, &p); render(s.host, fresh); CHECK(cbc_host_destroy(s.host));
    base_params(&p, scenario); build(&s, CBC_HOST_CPU_REFERENCE, &p); render(s.host, base); CHECK(cbc_host_destroy(s.host));
    snprintf(name, sizeof(name), "%s.before", names[scenario]); save(prefix, name, before);
    snprintf(name, sizeof(name), "%s.edited", names[scenario]); save(prefix, name, after);
    snprintf(name, sizeof(name), "%s.replay", names[scenario]); save(prefix, name, replay_after);
    snprintf(name, sizeof(name), "%s.fresh-cpu", names[scenario]); save(prefix, name, fresh);
    snprintf(name, sizeof(name), "%s.base-cpu", names[scenario]); save(prefix, name, base);
    /* Every edit except the round trip must change the image. */
    if (memcmp(base, fresh, sizeof(base)) != 0) changed++;
    else REQUIRE(scenario == 9);
  }
  int failed_pass = -1, after_pass = -1, failed_camera = -1, after_camera = -1; Events events = {0};
  if (gpu) {
    /* Render-time WebGPU rejections on one host, each followed by recovery. */
    Params p; base_params(&p, 0); Scene s; build(&s, backend, &p);
    CHECK(cbc_host_set_event_callback(s.host, on_event, &events));
    render(s.host, before);
    CBCNode depth = create(s.host, "pass");
    CHECK(cbc_host_set_string(s.host, depth, "type", "depth")); CHECK(cbc_host_set_string(s.host, depth, "name", "Depth"));
    CBCRenderConfig config = {sizeof(config), W, H, SAMPLES, {0}};
    for (unsigned i = 0; i < FLOATS; ++i) after[i] = -99;
    failed_pass = cbc_host_render(s.host, &config, after, FLOATS);
    for (unsigned i = 0; i < FLOATS; ++i) REQUIRE(after[i] == -99);
    CHECK(cbc_host_delete_node(s.host, depth));
    after_pass = cbc_host_render(s.host, &config, after, FLOATS);
    if (after_pass == CBC_OK) save(prefix, "recovery-pass", after);
    CHECK(cbc_host_set_string(s.host, s.camera, "camera_type", "orthograph"));
    failed_camera = cbc_host_render(s.host, &config, replay_after, FLOATS);
    CHECK(cbc_host_set_string(s.host, s.camera, "camera_type", "perspective"));
    after_camera = cbc_host_render(s.host, &config, replay_after, FLOATS);
    if (after_camera == CBC_OK) save(prefix, "recovery-camera", replay_after);
    CHECK(cbc_host_destroy(s.host));
    build(&s, CBC_HOST_CPU_REFERENCE, &p); render(s.host, fresh); CHECK(cbc_host_destroy(s.host));
    save(prefix, "recovery.fresh-cpu", fresh);
  }
  printf("{\"passed\":true,\"backend\":\"%s\",\"scenarios\":%d,\"changedScenarios\":%u,\"width\":%d,\"height\":%d,\"samples\":%d,"
         "\"recoveryPassRejected\":%d,\"recoveryPassRender\":%d,\"recoveryCameraRejected\":%d,\"recoveryCameraRender\":%d,"
         "\"recoveryErrorEvents\":%u,\"recoveredStatusEvents\":%u,\"names\":[",
         gpu ? "gpu" : "cpu", SCENARIOS, changed, W, H, SAMPLES, failed_pass, after_pass, failed_camera, after_camera, events.errors, events.recovered);
  for (int i = 0; i < SCENARIOS; ++i) printf("%s\"%s\"", i ? "," : "", names[i]);
  printf("]}\n");
  return 0;
}
