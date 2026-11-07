// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Texture baking: a mesh's UV layout to raw float buffers through Cycles' bake
// kernel. Included at the end of cycles_host.cpp.
//
// Cycles' kernel reads, per pixel of the render buffer, which triangle to shade
// and where (kernel/integrator/init_from_bake.h). This file writes that map by
// rasterizing the mesh's UV triangles; nothing here depends on the device: Cycles
// asks for the map through OutputDriver::read_render_tile and copies it to the
// device's render buffers. Input of the kernel, per pixel:
//   BakePrimitive    u, v, triangle index in the mesh (the int's bits; -1 = empty)
//   BakeDifferential du/dx, du/dy, dv/dx, dv/dy, per pixel
// with the shaded point u * V0 + v * V1 + (1 - u - v) * V2.
namespace {
struct BakeType { uint32_t value; PassType pass; const char *name; };
const BakeType bake_types[] = {
  {CBC_BAKE_COMBINED, PASS_COMBINED, "combined"}, {CBC_BAKE_EMISSION, PASS_EMISSION, "emission"},
  {CBC_BAKE_BACKGROUND, PASS_BACKGROUND, "background"}, {CBC_BAKE_AO, PASS_AO, "ao"},
  {CBC_BAKE_DIFFUSE, PASS_DIFFUSE, "diffuse"}, {CBC_BAKE_DIFFUSE_DIRECT, PASS_DIFFUSE_DIRECT, "diffuse_direct"},
  {CBC_BAKE_DIFFUSE_INDIRECT, PASS_DIFFUSE_INDIRECT, "diffuse_indirect"}, {CBC_BAKE_DIFFUSE_COLOR, PASS_DIFFUSE_COLOR, "diffuse_color"},
  {CBC_BAKE_GLOSSY, PASS_GLOSSY, "glossy"}, {CBC_BAKE_GLOSSY_DIRECT, PASS_GLOSSY_DIRECT, "glossy_direct"},
  {CBC_BAKE_GLOSSY_INDIRECT, PASS_GLOSSY_INDIRECT, "glossy_indirect"}, {CBC_BAKE_GLOSSY_COLOR, PASS_GLOSSY_COLOR, "glossy_color"},
  {CBC_BAKE_TRANSMISSION, PASS_TRANSMISSION, "transmission"}, {CBC_BAKE_TRANSMISSION_DIRECT, PASS_TRANSMISSION_DIRECT, "transmission_direct"},
  {CBC_BAKE_TRANSMISSION_INDIRECT, PASS_TRANSMISSION_INDIRECT, "transmission_indirect"}, {CBC_BAKE_TRANSMISSION_COLOR, PASS_TRANSMISSION_COLOR, "transmission_color"},
  {CBC_BAKE_POSITION, PASS_POSITION, "position"}, {CBC_BAKE_NORMAL, PASS_NORMAL, "normal"}, {CBC_BAKE_UV, PASS_UV, "uv"},
  {CBC_BAKE_ROUGHNESS, PASS_ROUGHNESS, "roughness"}, {CBC_BAKE_OBJECT_ID, PASS_OBJECT_ID, "object_id"}, {CBC_BAKE_MATERIAL_ID, PASS_MATERIAL_ID, "material_id"},
  {CBC_BAKE_DEPTH, PASS_DEPTH, "depth"}, {CBC_BAKE_MIST, PASS_MIST, "mist"},
  {CBC_BAKE_AOV_COLOR, PASS_AOV_COLOR, "aov_color"}, {CBC_BAKE_AOV_VALUE, PASS_AOV_VALUE, "aov_value"}};
void bake_capabilities(std::ostringstream &out) {
  // Per compiled backend: validated by native/tools/bake_gate.py; refused by name; or built but not validated yet.
  out << ",\"baking\":{\"backends\":{";
  bool first = true;
  for (uint32_t backend : {uint32_t(CBC_HOST_CPU_REFERENCE), uint32_t(CBC_HOST_CPU), uint32_t(CBC_HOST_WEBGPU), uint32_t(CBC_HOST_METAL)})
    if (compiled(backend)) {
      out << (first ? "" : ",") << '"' << backend_name(backend) << "\":\"" << (backend == CBC_HOST_WEBGPU ? "refused" : backend == CBC_HOST_METAL ? "unvalidated" : "validated") << '"';
      first = false;
    }
  out << "},\"types\":[";
  first = true;
  for (const BakeType &type : bake_types) { out << (first ? "" : ",") << '"' << type.name << '"'; first = false; }
  out << "]}";
}
const BakeType &bake_type(uint32_t value) {
  for (const BakeType &type : bake_types) if (type.value == value) return type;
  throw Failure(CBC_INVALID, "Unknown bake type");
}
struct BakeRequest { Object *object; Mesh *mesh; const Attribute *uv; const BakeType *type; };
BakeRequest bake_request(Host &host, const CBCBakeConfig *config) {
  require(config && config->struct_size == sizeof(*config), "Invalid bake configuration");
  require(std::all_of(std::begin(config->reserved), std::end(config->reserved), [](auto value) { return value == 0; }) && config->reserved2 == 0, "Unknown bake options");
  require((config->flags & ~uint32_t(CBC_BAKE_USE_CAMERA | CBC_BAKE_INCLUDE_ALBEDO)) == 0, "Unknown bake flags");
  require(config->width && config->height && config->width <= 32768 && config->height <= 32768 && config->samples && config->samples <= INT_MAX, "Invalid bake dimensions or sample count");
  BakeRequest request{};
  request.type = &bake_type(config->type);
  auto &entry = host.entry(config->object);
  request.object = entry.graph ? nullptr : dynamic_cast<Object *>(entry.node);
  require(request.object, "Bake object handle is not an object");
  request.mesh = dynamic_cast<Mesh *>(request.object->get_geometry());
  require(request.mesh, "Only an object whose geometry is a triangle mesh can be baked");
  require(request.mesh->get_subdivision_type() == Mesh::SUBDIVISION_NONE, "A subdivision surface cannot be baked", CBC_UNSUPPORTED);
  require(request.mesh->num_triangles() > 0, "The bake mesh has no triangles");
  const char *name = config->uv_attribute;
  request.uv = name && *name ? request.mesh->attributes.find(ustring(name)) : request.mesh->attributes.find(ATTR_STD_UV);
  require(request.uv, "The bake mesh has no such UV attribute");
  require(request.uv->type == TypeFloat2 && (request.uv->element == ATTR_ELEMENT_CORNER || request.uv->element == ATTR_ELEMENT_VERTEX),
          "The bake UV attribute must be a float2 corner or vertex attribute");
  const bool aov = config->type == CBC_BAKE_AOV_COLOR || config->type == CBC_BAKE_AOV_VALUE;
  require(aov ? config->aov_name && *config->aov_name : !config->aov_name, aov ? "An AOV bake needs the AOV pass name" : "Only an AOV bake takes an AOV pass name");
  return request;
}
// Twice the signed area of (a, b, p). A pair of points is always taken in the
// same order, whichever triangle it comes from, so two triangles sharing an edge
// compute the same value for that edge: a pixel centre near a shared edge is never
// outside both (no crack from rounding).
struct Texel { double x, y; };
double edge(const Texel &a, const Texel &b, const Texel &p) {
  const bool swap = b.x < a.x || (b.x == a.x && b.y < a.y);
  const Texel &first = swap ? b : a, &second = swap ? a : b;
  const double value = (second.x - first.x) * (p.y - first.y) - (second.y - first.y) * (p.x - first.x);
  return swap ? -value : value;
}
TextureBakeMap bake_map(const Mesh &mesh, const Attribute &uv, int width, int height) {
  TextureBakeMap map;
  const size_t pixels = size_t(width) * size_t(height);
  map.primitive.assign(pixels * 3, 0.0f); map.differential.assign(pixels * 4, 0.0f); map.triangles.assign(pixels, -1);
  for (size_t i = 0; i < pixels; ++i) map.primitive[i * 3 + 2] = __int_as_float(-1);
  const float2 *values = reinterpret_cast<const float2 *>(uv.data());
  const array<int> &indices = mesh.get_triangles();
  const int triangles = int(mesh.num_triangles());
  for (int triangle = 0; triangle < triangles; ++triangle) {
    Texel t[3];
    for (int corner = 0; corner < 3; ++corner) {
      const size_t index = uv.element == ATTR_ELEMENT_CORNER ? size_t(triangle) * 3 + corner : size_t(indices[size_t(triangle) * 3 + corner]);
      const float2 value = values[index];
      require(std::isfinite(value.x) && std::isfinite(value.y), "The bake UV attribute has a nonfinite value");
      t[corner] = Texel{double(value.x) * width, double(value.y) * height};
    }
    const double area = (t[1].x - t[0].x) * (t[2].y - t[0].y) - (t[1].y - t[0].y) * (t[2].x - t[0].x);
    if (area == 0.0) continue;  // No area in UV space: covers no pixel.
    const double du_dx = (t[1].y - t[2].y) / area, du_dy = (t[2].x - t[1].x) / area;
    const double dv_dx = (t[2].y - t[0].y) / area, dv_dy = (t[0].x - t[2].x) / area;
    const double min_x = std::min({t[0].x, t[1].x, t[2].x}), max_x = std::max({t[0].x, t[1].x, t[2].x});
    const double min_y = std::min({t[0].y, t[1].y, t[2].y}), max_y = std::max({t[0].y, t[1].y, t[2].y});
    const int first_x = int(std::clamp(std::ceil(min_x - 0.5), 0.0, double(width))), last_x = int(std::clamp(std::floor(max_x - 0.5), -1.0, double(width) - 1.0));
    const int first_y = int(std::clamp(std::ceil(min_y - 0.5), 0.0, double(height))), last_y = int(std::clamp(std::floor(max_y - 0.5), -1.0, double(height) - 1.0));
    for (int y = first_y; y <= last_y; ++y) {
      for (int x = first_x; x <= last_x; ++x) {
        const size_t pixel = size_t(y) * size_t(width) + size_t(x);
        if (map.triangles[pixel] >= 0) continue;  // Overlapping UVs: the lowest triangle index wins.
        const Texel centre{x + 0.5, y + 0.5};
        const double u = edge(t[1], t[2], centre) / area, v = edge(t[2], t[0], centre) / area, w = edge(t[0], t[1], centre) / area;
        if (!(u >= 0.0 && v >= 0.0 && w >= 0.0)) continue;
        map.triangles[pixel] = triangle;
        map.primitive[pixel * 3] = float(u); map.primitive[pixel * 3 + 1] = float(v); map.primitive[pixel * 3 + 2] = __int_as_float(triangle);
        map.differential[pixel * 4] = float(du_dx); map.differential[pixel * 4 + 1] = float(du_dy);
        map.differential[pixel * 4 + 2] = float(dv_dx); map.differential[pixel * 4 + 3] = float(dv_dy);
      }
    }
  }
  return map;
}
// Scene state a bake changes, restored when the bake ends, however it ends.
struct BakeScope {
  Scene &scene;
  Pass *added = nullptr;
  std::vector<std::pair<Object *, bool>> targets;
  PassType display_pass = PASS_COMBINED;
  ~BakeScope() {
    for (auto &[object, value] : targets) object->set_is_bake_target(value);
    scene.film->set_display_pass(display_pass);
    if (added) scene.delete_node(added);
    scene.bake_manager->set_baking(&scene, false);
    scene.bake_manager->set_use_camera(false);
    scene.bake_manager->tag_update();
    scene.film->tag_modified();
  }
};
}
extern "C" int cbc_host_bake_map(CBCHost handle, const CBCBakeConfig *config, int32_t *triangles, float *coordinates, uint64_t capacity, uint64_t *required) {
  return edit(handle, [&](Host &host) {
    require(required && (!capacity || triangles), "Invalid result buffer or size pointer");
    const BakeRequest request = bake_request(host, config);
    const uint64_t pixels = uint64_t(config->width) * config->height;
    *required = pixels;
    require(capacity >= pixels, "Result buffer is too small", CBC_SMALL_BUFFER);
    validate_geometry(host);
    const TextureBakeMap map = bake_map(*request.mesh, *request.uv, int(config->width), int(config->height));
    std::copy(map.triangles.begin(), map.triangles.end(), triangles);
    if (coordinates) for (uint64_t i = 0; i < pixels; ++i) {
      coordinates[i * 6] = map.primitive[i * 3]; coordinates[i * 6 + 1] = map.primitive[i * 3 + 1];
      for (int k = 0; k < 4; ++k) coordinates[i * 6 + 2 + k] = map.differential[i * 4 + k];
    }
  });
}
extern "C" int cbc_host_bake(CBCHost handle, const CBCBakeConfig *config, float *values, uint64_t capacity, uint64_t *required, uint32_t *components) {
  return edit(handle, [&](Host &host) {
    require(required && components && (!capacity || values), "Invalid result buffer or size pointer");
    // A bake on the WGSL device is a later addition behind this same function.
    require(host.backend != CBC_HOST_WEBGPU, "The WebGPU device does not bake yet; bake with a CPU backend", CBC_UNSUPPORTED);
    const BakeRequest request = bake_request(host, config);
    auto &session = *host.session; auto &scene = *session.scene;
    // An AOV is written only to a pass its shader was compiled against: the caller's own pass.
    Pass *aov_pass = nullptr;
    if (request.type->pass == PASS_AOV_COLOR || request.type->pass == PASS_AOV_VALUE) {
      for (Pass *pass : scene.passes) if (pass->get_type() == request.type->pass && pass->get_name() == config->aov_name) aov_pass = pass;
      require(aov_pass, "An AOV bake needs an AOV pass node of that type and name");
    }
    const unsigned pass_components = unsigned(Pass::get_info(request.type->pass, PassMode::NOISY, (config->flags & CBC_BAKE_INCLUDE_ALBEDO) != 0, false).num_components);
    const uint64_t floats = uint64_t(config->width) * config->height * pass_components;
    *required = floats; *components = pass_components;
    require(capacity >= floats, "Result buffer is too small", CBC_SMALL_BUFFER);
    // With an approximate shadow catcher the film gains a background pass, and the
    // kernel treats any background pass as a request to bake the world.
    require(!(request.type->pass != PASS_BACKGROUND && scene.has_shadow_catcher() && scene.film->get_use_approximate_shadow_catcher() && !scene.background->get_transparent()),
            "Baking a scene with an approximate shadow catcher and an opaque background is not supported", CBC_UNSUPPORTED);
    validate_geometry(host);
    TextureBakeMap map = bake_map(*request.mesh, *request.uv, int(config->width), int(config->height));
    struct Prune { Host &host; ~Prune() { prune_bakes(host); } } prune{host};
    reconcile_bakes(host);
    host.prepare_graphs();
    recover_device(host);
    // The position pass is not divided by the sample count, but the kernel adds to it
    // once per sample: one sample, at the texel centre, is the exact value.
    const int samples = request.type->pass == PASS_POSITION ? 1 : int(config->samples);
    host.requested_samples = uint64_t(samples);
    scene.camera->set_full_width(int(config->width)); scene.camera->set_full_height(int(config->height));
    scene.camera->set_screen_size(int(config->width), int(config->height)); scene.camera->compute_auto_viewplane();
    auto params = session.params; params.samples = samples;
    scene.integrator->set_aa_samples(params.samples);
    BufferParams buffers; buffers.width = buffers.full_width = buffers.window_width = int(config->width);
    buffers.height = buffers.full_height = buffers.window_height = int(config->height);
    host.result = Result{}; host.result.width = buffers.width; host.result.height = buffers.height;
    host.result.pixels.assign(size_t(config->width) * config->height * 4, -1000.0f); host.cancellation = false;
    host.result.bake_map = &map;
    host.last_sample = -1; host.last_status.clear();
    struct Active { Host &host; Active(Host &h) : host(h) { host.rendering = true; } ~Active() { host.rendering = false; } } active(host);
    BakeScope scope{scene};
    scope.display_pass = scene.film->get_display_pass();
    scene.film->set_display_pass(PASS_COMBINED);
    for (Object *object : scene.objects) {
      const bool target = object == request.object;
      if (object->get_is_bake_target() != target) { scope.targets.emplace_back(object, object->get_is_bake_target()); object->set_is_bake_target(target); }
    }
    // Every pass the caller made stays out of the bake's film (the kernel chooses its
    // path by which passes exist, and writes one of two passes of a type) except AOVs.
    ParkedPasses parking{scene};
    for (auto &[id, entry] : host.nodes) if (auto pass = dynamic_cast<Pass *>(entry.node))
      if (pass->get_type() != PASS_AOV_COLOR && pass->get_type() != PASS_AOV_VALUE) { parking.parked.emplace_back(pass, pass->get_type()); pass->set_type(PASS_NONE); }
    const char *const bake_pass_name = "CodeBrix Bake";
    if (request.type->pass != PASS_COMBINED && !aov_pass) {
      scope.added = scene.create_node<Pass>();
      scope.added->set_type(request.type->pass); scope.added->set_name(ustring(bake_pass_name)); scope.added->set_mode(PassMode::NOISY);
      scope.added->set_include_albedo((config->flags & CBC_BAKE_INCLUDE_ALBEDO) != 0);
    }
    if (request.type->pass != PASS_COMBINED) {
      PassResult value; value.name = aov_pass ? aov_pass->get_name().string() : bake_pass_name; value.type = int(request.type->pass);
      value.components = pass_components; value.pixels.assign(size_t(floats), 0.0f);
      host.result.passes.push_back(std::move(value));
    }
    scene.bake_manager->set_use_seed(false);
    scene.bake_manager->set_use_camera((config->flags & CBC_BAKE_USE_CAMERA) != 0);
    scene.bake_manager->set_baking(&scene, true);
    scene.bake_manager->tag_update();
    render_pass_modes(host, parking);
    scene.film->tag_modified();
    session.progress.reset();
    session.reset(params, buffers); session.start(); session.wait();
    try {
      require(!host.cancellation && !host.closing, "Bake cancelled", CBC_CANCELLED);
      require(!session.progress.get_error(), session.progress.get_error_message().c_str(), CBC_FAILED);
      require(!session.device->have_error(), session.device->error_message().c_str(), CBC_FAILED);
      require(!session.progress.get_cancel(), "Bake cancelled", CBC_CANCELLED);
      require(host.result.bake_map_read, "Cycles did not read the bake map", CBC_FAILED);
      require(host.result.writes == 1 && host.result.valid, "Missing complete bake output", CBC_FAILED);
      if (!host.result.passes.empty()) require(host.result.passes[0].written, "The device did not write this bake pass", CBC_FAILED);
      const std::vector<float> &output = host.result.passes.empty() ? host.result.pixels : host.result.passes[0].pixels;
      require(std::all_of(output.begin(), output.end(), [](float value) { return std::isfinite(value); }), "Nonfinite bake output", CBC_FAILED);
      std::copy(output.begin(), output.end(), values);
    } catch (const Failure &failure) {
      if (failure.code == CBC_FAILED) { std::lock_guard lock(host.event_mutex); host.emit(CBC_EVENT_ERROR, 0, 0, failure.what()); }
      host.result = Result{};
      throw;
    }
    // A bake leaves no render results behind (cbc_host_pass_floats: CBC_NOT_READY).
    host.result = Result{};
  });
}
