// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Phase D additions to ABI 1: version, backend-neutral selection, capability
// report, deletion, image updates, deformation motion, events and passes.
// Included at the end of cycles_host.cpp, after the original exports.
namespace {
const char *backend_name(uint32_t backend) {
  switch (backend) {
    case CBC_HOST_WEBGPU: return "webgpu";
    case CBC_HOST_CPU_REFERENCE: return "cpu";
    case CBC_HOST_METAL: return "metal";
    default: return "none";
  }
}
bool compiled(uint32_t backend) {
  switch (backend) {
    case CBC_HOST_CPU_REFERENCE: return true;
#ifdef WITH_WEBGPU
    case CBC_HOST_WEBGPU: return true;
#endif
#ifdef WITH_METAL
    case CBC_HOST_METAL: return true;
#endif
    default: return false;
  }
}
// True when holder stores target in any node-reference socket.
bool references(const Node *holder, const Node *target) {
  if (!holder || holder == target) return false;
  for (const auto &input : holder->type->inputs) {
    if (input.struct_offset < 0) continue;
    if (input.type == SocketType::NODE && holder->get_node(input) == target) return true;
    if (input.type == SocketType::NODE_ARRAY)
      for (auto node : holder->get_node_array(input)) if (node == target) return true;
  }
  return false;
}
const Node *referenced_by(Scene &scene, const Node *target) {
  std::vector<const Node *> holders = {scene.camera, scene.film, scene.integrator, scene.background};
  for (auto node : scene.objects) holders.push_back(node);
  for (auto node : scene.geometry) holders.push_back(node);
  for (auto node : scene.shaders) holders.push_back(node);
  for (auto node : scene.particle_systems) holders.push_back(node);
  for (auto node : scene.passes) holders.push_back(node);
  for (auto node : holders) if (references(node, target)) return node;
  return nullptr;
}
}

extern "C" int cbc_host_version(CBCVersionInfo *info) {
  // Not routed through boundary(): valid from event callbacks, no host state.
  if (!info || info->struct_size != sizeof(CBCVersionInfo)) { store_error("Invalid version structure"); return CBC_INVALID; }
  static_assert(sizeof(CBC_HOST_LIBRARY_VERSION) <= sizeof(info->library_version), "Library version is too long");
  static_assert(sizeof(CBC_HOST_CYCLES_REVISION) <= sizeof(info->cycles_revision), "Cycles revision is too long");
  CBCVersionInfo result{};
  result.struct_size = sizeof(result); result.abi_version = CBC_HOST_ABI;
  std::memcpy(result.library_version, CBC_HOST_LIBRARY_VERSION, sizeof(CBC_HOST_LIBRARY_VERSION));
  std::memcpy(result.cycles_revision, CBC_HOST_CYCLES_REVISION, sizeof(CBC_HOST_CYCLES_REVISION));
  *info = result; return CBC_OK;
}
extern "C" int cbc_host_resolve_backend(uint32_t requested, uint32_t *resolved) {
  if (resolved) *resolved = 0;
  return boundary([&] {
    require(resolved, "Missing output pointer");
    require(requested >= CBC_HOST_WEBGPU && requested <= CBC_HOST_GPU, "Unknown backend");
    const uint32_t backend = requested == CBC_HOST_GPU ? gpu_backend() : requested;
    require(backend && compiled(backend), "Backend is not compiled into this binary", CBC_UNSUPPORTED);
    *resolved = backend;
  });
}
extern "C" int cbc_host_backend(CBCHost handle, uint32_t *backend) {
  return boundary([&] { require(backend, "Missing output pointer"); *backend = lookup(handle)->backend; });
}
extern "C" int cbc_host_capabilities_json(CBCHost handle, char *json, uint64_t capacity, uint64_t *required) {
  auto write = [&](Host *host) {
    require(required && (!capacity || json), "Invalid result buffer or size pointer");
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "{\"schemaVersion\":1,\"abiVersion\":" << CBC_HOST_ABI << ",\"libraryVersion\":\"" << CBC_HOST_LIBRARY_VERSION
        << "\",\"cyclesRevision\":\"" << CBC_HOST_CYCLES_REVISION << "\",\"backends\":{\"compiled\":[";
    bool first = true;
    for (uint32_t backend : {uint32_t(CBC_HOST_CPU_REFERENCE), uint32_t(CBC_HOST_WEBGPU), uint32_t(CBC_HOST_METAL)})
      if (compiled(backend)) { out << (first ? "" : ",") << '"' << backend_name(backend) << '"'; first = false; }
    out << "],\"gpu\":\"" << backend_name(gpu_backend()) << "\",\"gpuFallbackToCpu\":false}";
    // Scope statements are facts about this source tree. Unsupported scene
    // features fail cbc_host_render with CBC_FAILED and the device message.
    out << ",\"backendScope\":{\"cpu\":{\"device\":\"Cycles CPU device (reference)\",\"cameraTypes\":[\"perspective\",\"orthograph\",\"panorama\""
#ifdef WITH_OSL
        << ",\"custom\""
#endif
        << "],\"passes\":\"registered pass types written by the Cycles CPU device\"}";
#ifdef WITH_WEBGPU
    out << ",\"webgpu\":{\"device\":\"CodeBrix WGSL device (native/DEVICE.md)\",\"cameraTypes\":[\"perspective\"],\"primitives\":[\"triangle\"],"
           "\"passes\":[\"combined\"],\"renderTimeRejections\":[\"non-perspective or stereo cameras\",\"light trees\",\"surface data passes\","
           "\"volume light passes\",\"background light MIS\",\"nonconstant light emission\",\"heterogeneous volumes\",\"moving transparent shadows\","
           "\"curves and points\",\"sparse or tiled images\"],\"reference\":\"native/DEVICE.md\"}";
#endif
#ifdef WITH_METAL
    out << ",\"metal\":{\"device\":\"Cycles Metal device\",\"reference\":\"native/host/MACOS_ARM64.md\"}";
#endif
    out << "},\"optionalLibraries\":{\"osl\":"
#ifdef WITH_OSL
        << "true"
#else
        << "false"
#endif
        << ",\"embree\":"
#ifdef WITH_EMBREE
        << "true"
#else
        << "false"
#endif
        << ",\"openImageDenoise\":"
#ifdef WITH_OPENIMAGEDENOISE
        << "true"
#else
        << "false"
#endif
        << ",\"openVdb\":"
#ifdef WITH_OPENVDB
        << "true"
#else
        << "false"
#endif
        << "}";
    out << ",\"imageInput\":{\"formats\":[\"float\",\"float4\",\"half\",\"half4\",\"byte\",\"byte4\",\"ushort\",\"ushort4\"],"
           "\"origin\":\"lower-left\",\"fileReading\":"
#ifdef CBC_HOST_NO_IMAGE_FILES
        << "false"
#else
        << "\"development-only\""
#endif
        << "},\"output\":{\"format\":\"float32\",\"colorSpace\":\"scene-linear\",\"origin\":\"lower-left\",\"combinedComponents\":4,\"passReadback\":\"copied\"}";
    // Pass types the native registry accepts; which ones a device writes is
    // reported per render by cbc_host_render_passes_json ("written").
    std::map<std::string, int> passes;
    for (const auto &[name, value] : *Pass::get_type_enum()) passes.emplace(name.string(), value);
    out << ",\"passTypes\":[";
    first = true;
    for (const auto &[name, value] : passes) { out << (first ? "" : ",") << "{\"name\":"; json_text(out, name.c_str()); out << ",\"value\":" << value << '}'; first = false; }
    out << "],\"events\":[\"status\",\"progress\",\"error\"],\"limits\":{\"maxRenderWidth\":32768,\"maxRenderHeight\":32768,"
           "\"maxSamples\":" << INT_MAX << ",\"maxImageDimension\":" << INT_MAX << ",\"maxMotionSteps\":" << Geometry::MAX_MOTION_STEPS
        << ",\"maxThreads\":1024,\"errorMessageBytes\":" << sizeof(last_error) << "},\"determinism\":\"native/host/ABI.md#determinism\"";
    if (host) {
      Device *device = host->session ? host->session->device.get() : nullptr;
      require(device != nullptr, "Host has no device", CBC_FAILED);
      out << ",\"host\":{\"backend\":\"" << backend_name(host->backend) << "\",\"deviceDescription\":";
      json_text(out, device->info.description.c_str());
      out << ",\"deviceId\":"; json_text(out, device->info.id.c_str());
      out << ",\"deviceReport\":" << device_report(*host) << '}';
    }
    out << '}';
    copy_text(out.str(), json, capacity, required);
  };
  if (!handle) return boundary([&] { write(nullptr); });
  return edit(handle, [&](Host &host) { write(&host); });
}
extern "C" int cbc_host_delete_node(CBCHost handle, CBCNode id) {
  return edit(handle, [&](Host &host) {
    auto &entry = host.entry(id); auto &scene = *host.session->scene;
    if (entry.graph) {
      auto node = static_cast<ShaderNode *>(entry.node); auto &graph = *entry.graph;
      require(node != graph.authoring->output(), "The shader output node cannot be deleted");
      for (auto input : node->inputs) if (input->link) graph.authoring->disconnect(input);
      for (auto output : node->outputs) if (!output->links.empty()) graph.authoring->disconnect(output);
      if (auto image = dynamic_cast<ImageSlotTextureNode *>(node)) graph.images.erase(image);
      graph.authoring->nodes.erase(node); graph.dirty = true;
      host.nodes.erase(id); return;
    }
    Node *node = entry.node;
    require(node != scene.camera && node != scene.film && node != scene.integrator && node != scene.background,
            "Scene singletons cannot be deleted");
    if (auto shader = dynamic_cast<Shader *>(node)) require(host.graphs.count(shader), "Only shaders created through this API can be deleted");
    else require(dynamic_cast<Object *>(node) || dynamic_cast<Geometry *>(node) || dynamic_cast<ParticleSystem *>(node) ||
                 dynamic_cast<Pass *>(node), "Node type cannot be deleted", CBC_UNSUPPORTED);
    if (auto holder = referenced_by(scene, node))
      throw Failure(CBC_INVALID, "Node is still referenced by a " + holder->type->name.string() + " node; clear that reference first");
    host.nodes.erase(id);
    if (auto shader = dynamic_cast<Shader *>(node)) {
      Graph *graph = host.graphs.at(shader).get();
      for (auto it = host.nodes.begin(); it != host.nodes.end();) it = it->second.graph == graph ? host.nodes.erase(it) : std::next(it);
      host.graphs.erase(shader);
      // Cycles keeps deleted shaders allocated and unused (Scene::delete_node).
      scene.delete_node(shader);
    } else if (auto object = dynamic_cast<Object *>(node)) {
      if (object->get_geometry()) scene.delete_node(object);
      else { scene.objects.erase_by_swap(object); scene.object_manager->tag_update(&scene, ObjectManager::OBJECT_REMOVED); }
    } else if (auto light = dynamic_cast<Light *>(node)) scene.delete_node(light);
    else if (auto geometry = dynamic_cast<Geometry *>(node)) { host.bakes.erase(geometry); scene.delete_node(geometry); }
    else if (auto particles = dynamic_cast<ParticleSystem *>(node)) scene.delete_node(particles);
    else scene.delete_node(static_cast<Pass *>(node));
  });
}
extern "C" int cbc_host_image_update(CBCHost handle, CBCImage image, const CBCImageConfig *config, const void *pixels, uint64_t bytes) {
  return edit(handle, [&](Host &host) {
    auto it = host.images.find(image); require(it != host.images.end(), "Invalid or released image handle");
    std::shared_ptr<const MemoryImageData> replacement = image_data(config, pixels, bytes), previous = it->second;
    it->second = replacement;
    for (auto &[shader, graph] : host.graphs) for (auto &[node, data] : graph->images)
      if (data == previous) { data = replacement; node->handle.clear(); graph->dirty = true; }
  });
}
extern "C" int cbc_host_motion_positions(CBCHost handle, CBCNode id, uint32_t steps, const float *xyz, uint64_t count) {
  return edit(handle, [&](Host &host) {
    auto &entry = host.entry(id); auto mesh = dynamic_cast<Mesh *>(entry.node); require(mesh && !entry.graph, "Handle is not a mesh");
    // A mesh baked by an earlier render gets its authored positions back first (host_edits.inc).
    before_edit(host, entry, nullptr);
    require(steps == 1 || (steps >= 3 && steps % 2 == 1 && steps <= Geometry::MAX_MOTION_STEPS), "Motion steps must be 1 or odd from 3 to 129");
    auto position = mesh->attributes.find(ATTR_STD_POSITION); require(position, "Mesh has no positions");
    auto normal = mesh->attributes.find(ATTR_STD_VERTEX_NORMAL);
    if (steps == 1) {
      require(count == 0, "Removing motion takes no positions");
      position->remove_motion(); if (normal) normal->remove_motion();
      mesh->set_use_motion_blur(false);
    } else {
      const uint64_t vertices = mesh->num_verts();
      require(vertices && count == uint64_t(steps) * vertices * 3 && xyz, "Expected steps * vertex_count * 3 positions");
      for (uint64_t i = 0; i < count; ++i) require(std::isfinite(xyz[i]), "Motion position is nonfinite");
      const uint32_t center = (steps - 1) / 2; const packed_float3 *current = position->data<packed_float3>();
      for (uint64_t v = 0; v < vertices; ++v) {
        const float *p = xyz + (uint64_t(center) * vertices + v) * 3;
        require(std::memcmp(&current[v].x, p, sizeof(float)) == 0 && std::memcmp(&current[v].y, p + 1, sizeof(float)) == 0 &&
                std::memcmp(&current[v].z, p + 2, sizeof(float)) == 0, "Center motion step must equal the mesh positions");
      }
      mesh->set_motion_steps(steps); mesh->set_use_motion_blur(true);
      position->remove_motion(); position->add_motion(mesh);
      // Stale generated motion normals would survive a position update.
      if (normal && normal->has_motion()) normal->remove_motion();
      for (uint32_t step = 0; step < steps; ++step) {
        if (step == center) continue;
        auto out = position->data_for_write<packed_float3>(position->time_step_to_attr_step(int(step), int(steps)));
        for (uint64_t v = 0; v < vertices; ++v) { const float *p = xyz + (uint64_t(step) * vertices + v) * 3; out[v] = make_float3(p[0], p[1], p[2]); }
      }
    }
    mesh->tag_position_modified(); host.changed(entry);
  });
}
extern "C" int cbc_host_set_event_callback(CBCHost handle, CBCEventCallback callback, void *user_data) {
  return edit(handle, [&](Host &host) { std::lock_guard lock(host.event_mutex); host.callback = callback; host.user_data = callback ? user_data : nullptr; });
}
extern "C" int cbc_host_pass_floats(CBCHost handle, const char *name, float *values, uint64_t capacity, uint64_t *required, uint32_t *components) {
  return edit(handle, [&](Host &host) {
    require(name && *name && components, "Missing pass name or components pointer");
    require(host.result.complete, "No successful render has produced passes", CBC_NOT_READY);
    if (std::strcmp(name, "Combined") == 0) { *components = 4; copy_result(host.result.pixels, values, capacity, required); return; }
    for (auto &pass : host.result.passes) if (pass.name == name) {
      require(pass.written, "The device did not write this pass", CBC_UNSUPPORTED);
      *components = pass.components; copy_result(pass.pixels, values, capacity, required); return;
    }
    throw Failure(CBC_INVALID, "No captured pass has this name");
  });
}
extern "C" int cbc_host_render_passes_json(CBCHost handle, char *json, uint64_t capacity, uint64_t *required) {
  return edit(handle, [&](Host &host) {
    require(host.result.complete, "No successful render has produced passes", CBC_NOT_READY);
    std::ostringstream out; out.imbue(std::locale::classic());
    out << "{\"width\":" << host.result.width << ",\"height\":" << host.result.height
        << ",\"passes\":[{\"name\":\"Combined\",\"type\":\"combined\",\"components\":4,\"written\":true}";
    auto types = Pass::get_type_enum();
    for (auto &pass : host.result.passes) {
      out << ",{\"name\":"; json_text(out, pass.name.c_str()); out << ",\"type\":";
      if (types->exists(pass.type)) json_text(out, (*types)[pass.type].c_str()); else out << "null";
      out << ",\"components\":" << pass.components << ",\"written\":" << (pass.written ? "true" : "false") << '}';
    }
    out << "]}";
    copy_text(out.str(), json, capacity, required);
  });
}
