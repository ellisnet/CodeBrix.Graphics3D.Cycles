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
  // Per compiled backend: validated by native/tools/bake_gate.py, or built but not validated yet.
  out << ",\"baking\":{\"backends\":{";
  bool first = true;
  for (uint32_t backend : {uint32_t(CBC_HOST_CPU_REFERENCE), uint32_t(CBC_HOST_CPU), uint32_t(CBC_HOST_WEBGPU), uint32_t(CBC_HOST_METAL)})
    if (compiled(backend)) {
      out << (first ? "" : ",") << '"' << backend_name(backend) << "\":\"" << (backend == CBC_HOST_METAL ? "unvalidated" : "validated") << '"';
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
// subdivision: the object's mesh is a linear subdivision cage (cbc_host_subd_mesh), baked through Cycles' tessellation of it.
struct BakeRequest { Object *object; Mesh *mesh; const Attribute *uv; const BakeType *type; bool subdivision; };
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
  // Subdivision surfaces (PROVISIONAL): a linear subdivision cage is baked through the triangles Cycles dices from it
  // (subdivision_bake_map). Catmull-Clark is refused as at render (validate_geometry). Pixel-space dicing measures in a
  // camera a bake does not have: GeometryManager::device_update (scene/geometry.cpp) hands Mesh::tessellate the scene's
  // dicing camera only for subd_adaptive_space pixel, and DiagSplit::T (subd/split.cpp) divides each edge's length by
  // that camera's pixel size (Camera::world_to_raster_size). A bake sets the camera to the bake's size, and the dicing
  // camera is the camera of the scene's last render (cbc_host_render) or Cycles' default, so the bake's triangles would
  // depend on the scene's history: refused by name. In object space they depend on the cage and its dicing settings alone.
  require(request.mesh->get_subdivision_type() != Mesh::SUBDIVISION_CATMULL_CLARK,
          "Unsupported feature: Catmull-Clark subdivision needs OpenSubdiv, which this library does not include", CBC_UNSUPPORTED);
  request.subdivision = request.mesh->get_subdivision_type() == Mesh::SUBDIVISION_LINEAR && request.mesh->get_num_subd_faces() != 0;
  require(!request.subdivision || request.mesh->get_subd_adaptive_space() == Mesh::SUBDIVISION_ADAPTIVE_SPACE_OBJECT,
          "Unsupported feature: bakes of subdivision surfaces diced in pixel space (subd_adaptive_space pixel measures the dicing in "
          "pixels of a render camera, which a bake does not have; use subd_adaptive_space object)", CBC_UNSUPPORTED);
  require(request.subdivision || request.mesh->num_triangles() > 0, "The bake mesh has no triangles");
  // A subdivision surface's UV layout is one of its subdivision attributes (CBC_ATTR_SUBDIVISION), which Cycles
  // interpolates over the faces it dices.
  const AttributeSet &attributes = request.subdivision ? request.mesh->subd_attributes : request.mesh->attributes;
  const char *name = config->uv_attribute;
  request.uv = name && *name ? attributes.find(ustring(name)) : attributes.find(ATTR_STD_UV);
  require(request.uv, request.subdivision ? "The bake mesh has no such UV attribute among its subdivision attributes (CBC_ATTR_SUBDIVISION)"
                                          : "The bake mesh has no such UV attribute");
  require(request.uv->type == TypeFloat2 && (request.uv->element == ATTR_ELEMENT_CORNER || request.uv->element == ATTR_ELEMENT_VERTEX),
          "The bake UV attribute must be a float2 corner or vertex attribute");
  const bool aov = config->type == CBC_BAKE_AOV_COLOR || config->type == CBC_BAKE_AOV_VALUE;
  require(aov ? config->aov_name && *config->aov_name : !config->aov_name, aov ? "An AOV bake needs the AOV pass name" : "Only an AOV bake takes an AOV pass name");
  return request;
}
// Every other subdivision surface of a baked scene (PROVISIONAL): GeometryManager::device_update (scene/geometry.cpp) dices
// each modified linear cage whose positions or dicing settings changed (Mesh::need_tessellation, scene/mesh.cpp), and for
// subd_adaptive_space pixel it hands Mesh::tessellate the scene's dicing camera, in whose pixels DiagSplit::T
// (subd/split.cpp) measures every edge (Camera::world_to_raster_size). cbc_host_render keeps the dicing camera equal to the
// render camera; a bake sets neither, so such a surface is diced in the camera of the scene's last render (Cycles' default
// before any render), or keeps the dicing of the last render when it was not modified since. Either way what the bake sees
// of it depends on the scene's history: refused by name, as for the bake target (bake_request). Only geometry an object
// uses is diced into what rays meet.
void validate_bake_dicing(Host &host, const BakeRequest &request) {
  for (Object *object : host.session->scene->objects) {
    auto mesh = dynamic_cast<Mesh *>(object->get_geometry());
    if (!mesh || mesh == request.mesh || !mesh->get_num_subd_faces() || mesh->get_subdivision_type() == Mesh::SUBDIVISION_NONE) continue;
    require(mesh->get_subd_adaptive_space() == Mesh::SUBDIVISION_ADAPTIVE_SPACE_OBJECT,
            "Unsupported feature: bakes of subdivision surfaces diced in pixel space (another subdivision surface of the baked scene has "
            "subd_adaptive_space pixel, which a bake would dice in the camera of the scene's last render, so the bake would depend on the "
            "scene's history; use subd_adaptive_space object for every subdivision surface of a scene you bake)", CBC_UNSUPPORTED);
  }
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
// The bake map of a linear subdivision surface (PROVISIONAL). The kernel shades the triangles Cycles dices from the cage:
// GeometryManager::device_update (scene/geometry.cpp) runs Mesh::tessellate (scene/mesh_subdivision.cpp) with the mesh's
// subd_dicing_rate and subd_max_level and, in object space, no camera, whenever the cage's positions or its dicing
// settings changed since the last dicing (Mesh::need_tessellation; an edit of a subdivision attribute tags the positions,
// host_scene.inc). Those triangles and their order follow from the faces, the cage positions and the two settings alone
// (DiagSplit, subd/split.cpp; EdgeDice, subd/dice.cpp), so the host dices a copy of the cage (its faces, positions and UV
// attribute) with the same Mesh::tessellate and parameters and maps each texel through the copy's UV attribute, which
// Cycles interpolates over each face (SubdAttributeInterpolation, subd/interpolation.cpp). Its triangle indices are the
// device's; true displacement moves the diced vertices afterwards and changes neither the triangles nor their UVs.
TextureBakeMap subdivision_bake_map(const Mesh &mesh, const Attribute &uv, int width, int height) {
  const Attribute *cage = mesh.subd_attributes.find(ATTR_STD_POSITION);
  require(cage, "The subdivision surface has no cage positions", CBC_FAILED);
  Mesh diced;
  diced.resize_mesh(int(cage->size), 0);
  diced.resize_subd_faces(int(mesh.get_num_subd_faces()), int(mesh.get_subd_face_corners().size()));
  const auto copy = [](const auto &from, auto &to) { std::copy_n(from.data(), from.size(), to.data()); };
  copy(mesh.get_subd_start_corner(), diced.get_subd_start_corner()); copy(mesh.get_subd_num_corners(), diced.get_subd_num_corners());
  copy(mesh.get_subd_shader(), diced.get_subd_shader()); copy(mesh.get_subd_smooth(), diced.get_subd_smooth());
  copy(mesh.get_subd_ptex_offset(), diced.get_subd_ptex_offset()); copy(mesh.get_subd_face_corners(), diced.get_subd_face_corners());
  copy_attribute(*cage, diced.subd_attributes);  // host_edits.inc
  copy_attribute(uv, diced.subd_attributes);
  diced.set_subdivision_type(Mesh::SUBDIVISION_LINEAR);
  SubdParams params(&diced);
  params.dicing_rate = mesh.get_subd_dicing_rate();
  params.max_level = mesh.get_subd_max_level();
  diced.tessellate(params);
  const Attribute *diced_uv = diced.attributes.find(uv.name);
  require(diced_uv && diced.num_triangles() > 0, "Cycles' tessellation of the subdivision surface has no triangles", CBC_FAILED);
  return bake_map(diced, *diced_uv, width, height);
}
TextureBakeMap bake_map(const BakeRequest &request, int width, int height) {
  return request.subdivision ? subdivision_bake_map(*request.mesh, *request.uv, width, height) : bake_map(*request.mesh, *request.uv, width, height);
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
    const TextureBakeMap map = bake_map(request, int(config->width), int(config->height));
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
    // With an approximate shadow catcher and an opaque background the film gains a background pass, and the pinned
    // kernel (integrator_init_from_bake) takes any background pass as a request to bake the world: the bake is the
    // world's, as Cycles writes it on every device.
    validate_geometry(host);
    // Refused before anything renders, as at render (cycles_host.cpp).
    validate_upscale_factor(scene);
    validate_bake_dicing(host, request);
    TextureBakeMap map = bake_map(request, int(config->width), int(config->height));
    struct Prune { Host &host; ~Prune() { prune_bakes(host); } } prune{host};
    reconcile_bakes(host);
    host.prepare_graphs();
    WorldVolumeLight world_light{scene}; world_volume_light(host, world_light);
    recover_device(host);
    reconcile_displacement(host);  // host_edits.inc, as in cbc_host_render
    // The position pass is not divided by the sample count, but the kernel adds to it
    // once per sample: one sample, at the texel centre, is the exact value.
    const int samples = request.type->pass == PASS_POSITION ? 1 : int(config->samples);
    host.requested_samples = uint64_t(samples);
    scene.camera->set_full_width(int(config->width)); scene.camera->set_full_height(int(config->height));
    scene.camera->set_screen_size(int(config->width), int(config->height)); host.fit_viewplane();
    auto params = session.params; params.samples = samples;
    // The integrator's sample subset, as cbc_host_render takes it.
    params.use_sample_subset = scene.integrator->get_use_sample_subset();
    params.sample_subset_offset = scene.integrator->get_sample_subset_offset();
    params.sample_subset_length = scene.integrator->get_sample_subset_length();
    params.time_limit = host.time_limit;
    // Tiled rendering (cbc_host_tile_size, PROVISIONAL): a bake renders in tiles as a render does (no overscan when baking,
    // TileManager::update); each tile reads its region of the bake map (HostOutput::read_render_tile).
    params.use_auto_tile = host.tile_size != 0; params.tile_size = host.tile_size ? int(host.tile_size) : SessionParams().tile_size;
    scene.integrator->set_aa_samples(params.samples);
    BufferParams buffers; buffers.width = buffers.full_width = buffers.window_width = int(config->width);
    buffers.height = buffers.full_height = buffers.window_height = int(config->height);
    host.result = Result{}; host.result.width = buffers.width; host.result.height = buffers.height;
    host.result.pixels.assign(size_t(config->width) * config->height * 4, -1000.0f); host.cancellation = false; host.tiled_reads->begin();
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
    { CancelWatch watch{host}; session.reset(params, buffers); session.start(); session.wait(); }
    DropTiles drop_tiles{host};
    host.images_loaded = host.images_loaded || scene.dscene.image_textures.size() != 0;  // cbc_host_texture_limit
    host.retry_undelivered_tiles();
    try {
      host.render_tiles_delivered();
      require(!host.cancellation && !host.closing, "Bake cancelled", CBC_CANCELLED);
      require(!session.progress.get_error(), session.progress.get_error_message().c_str(), CBC_FAILED);
      require(!session.device->have_error(), session.device->error_message().c_str(), CBC_FAILED);
      require(!session.progress.get_cancel(), "Bake cancelled", CBC_CANCELLED);
      // A tiled bake's output comes from its full frame in memory (cbc_host_tile_size, as in cbc_host_render).
      if (host.read_back_tiles()) {
        require(!host.cancellation && !host.closing, "Bake cancelled", CBC_CANCELLED);
        require(!session.progress.get_error(), session.progress.get_error_message().c_str(), CBC_FAILED);
        require(!session.device->have_error(), session.device->error_message().c_str(), CBC_FAILED);
        require(!session.progress.get_cancel(), "Bake cancelled", CBC_CANCELLED);
      }
      require(host.result.bake_map_read, "Cycles did not read the bake map", CBC_FAILED);
      require(host.result.delivered_error.empty(), host.result.delivered_error.c_str(), CBC_FAILED);  // cycles_host.cpp, HostOutput
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
