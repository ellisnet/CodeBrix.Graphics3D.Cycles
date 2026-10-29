// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "cycles_host.h"
#include "device/device.h"
#include "scene/scene.h"
#include "scene/background.h"
#include "scene/camera.h"
#include "scene/film.h"
#include "scene/integrator.h"
#include "scene/mesh.h"
#include "scene/hair.h"
#include "scene/pointcloud.h"
#include "scene/volume.h"
#include "scene/particles.h"
#include "scene/light.h"
#include "scene/object.h"
#include "scene/pass.h"
#include "scene/shader_graph.h"
#include "scene/shader_nodes.h"
#include "scene/attribute.h"
#include "scene/image.h"
#include "scene/image_loader.h"
#include "scene/procedural.h"
#include "scene/scene_attributes.h"
#include "scene/shader.h"
#include "util/image_metadata.h"
#include <OpenColorIO/OpenColorIO.h>
#include "session/output_driver.h"
#include "session/session.h"
#include "util/path.h"
#ifdef __APPLE__
#  include <dlfcn.h>
#  include <sys/sysctl.h>
#endif
#ifdef _WIN32
#  include <windows.h>
#  include <corecrt_startup.h>
#endif
#include <algorithm>
#include <atomic>
#include <climits>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <locale>
#include <functional>

#if !defined(CBC_HOST_LIBRARY_VERSION) || !defined(CBC_HOST_CYCLES_REVISION)
#  error "targets.cmake stamps CBC_HOST_LIBRARY_VERSION and CBC_HOST_CYCLES_REVISION"
#endif

namespace {
using namespace ccl;
thread_local char last_error[2048] = {};
// Set while an event callback runs on this thread; host calls are refused.
thread_local bool in_callback = false;
void store_error(const char *message) noexcept {
  std::strncpy(last_error, message, sizeof(last_error) - 1); last_error[sizeof(last_error) - 1] = 0;
}
struct Failure : std::runtime_error {
  int code;
  Failure(int c, const std::string &s) : std::runtime_error(s), code(c) {}
};
void require(bool ok, const char *message, int code = CBC_INVALID) {
  if (!ok) throw Failure(code, message);
}
template<class Function> int boundary(Function &&function) noexcept {
  if (in_callback) { store_error("Host functions cannot be called from an event callback"); return CBC_NOT_READY; }
  try { function(); last_error[0] = 0; return CBC_OK; }
  catch (const Failure &error) { store_error(error.what()); return error.code; }
  catch (const std::exception &error) { store_error(error.what()); return CBC_FAILED; }
  catch (...) { store_error("Unknown native failure"); return CBC_FAILED; }
}
uint64_t new_id() {
  static std::mutex mutex;
  static uint64_t next = 1;
  std::lock_guard lock(mutex);
  require(next != UINT64_MAX, "Native handle space exhausted", CBC_FAILED);
  return next++;
}
#include "host_scene_types.inc"
struct Graph {
  Shader *shader;
  std::unique_ptr<ShaderGraph> authoring = std::make_unique<ShaderGraph>();
  bool dirty = true;
  std::map<ImageSlotTextureNode *,std::shared_ptr<const MemoryImageData>> images;
};
struct Entry { Node *node; Graph *graph = nullptr; };
struct PassResult {
  std::string name;
  int type = 0;
  unsigned components = 0;
  std::vector<float> pixels;
  bool written = false;
};
struct Result {
  std::vector<float> pixels;
  int width = 0, height = 0;
  unsigned writes = 0;
  bool valid = false;
  // Named caller passes, read from the same complete tile as Combined.
  std::vector<PassResult> passes;
  bool complete = false;
};
class HostOutput final : public OutputDriver {
  Result &result;
 public:
  explicit HostOutput(Result &value) : result(value) {}
  void write_render_tile(const Tile &tile) override {
    result.writes++;
    result.valid = tile.offset.x == 0 && tile.offset.y == 0 &&
      tile.size.x == result.width && tile.size.y == result.height &&
      tile.get_pass_pixels("Combined", 4, result.pixels.data());
    if (!result.valid) return;
    for (auto &pass : result.passes)
      pass.written = tile.get_pass_pixels(pass.name, int(pass.components), pass.pixels.data());
  }
};
std::mutex configuration_mutex;
std::atomic<uint64_t> live_hosts = 0;
struct Host {
  std::mutex operation;
  std::atomic<bool> closing = false, rendering = false, cancellation = false;
  std::unique_ptr<Session> session;
  std::map<uint64_t, Entry> nodes;
  std::map<Shader *, std::unique_ptr<Graph>> graphs;
  std::map<CBCImage,std::shared_ptr<const MemoryImageData>> images;
  // Scene edits after a render (host_edits.inc): authored attributes of
  // geometry the static BVH path may bake, and shaders edited since the last render.
  std::map<Geometry *, BakeRecord> bakes;
  std::set<Shader *> edited_shaders;
  Result result;
  // Event callback state. The callback changes only under `operation`, which
  // rendering holds, so a render sees one callback throughout.
  CBCEventCallback callback = nullptr;
  void *user_data = nullptr;
  std::mutex event_mutex;
  std::string last_status;
  int64_t last_sample = -1;
  uint64_t requested_samples = 0;
  uint32_t backend = 0;
  void emit(uint32_t kind, uint64_t done, double elapsed, const std::string &message) {
    if (!callback || closing.load()) return;
    CBCEvent event{sizeof(CBCEvent), kind, done, requested_samples, elapsed, message.c_str()};
    in_callback = true;
    int stop = 0;
    try { stop = callback(user_data, &event); } catch (...) { stop = 1; }
    in_callback = false;
    if (stop) cancellation = true;
  }
  void progress_event() {
    if (!callback || !rendering.load() || closing.load() || !session) return;
    std::lock_guard lock(event_mutex);
    auto &progress = session->progress;
    std::string status, substatus; progress.get_status(status, substatus);
    if (!substatus.empty()) status += ": " + substatus;
    double total = 0, render = 0; progress.get_time(total, render);
    const int64_t sample = progress.get_current_sample();
    if (status != last_status) { last_status = status; emit(CBC_EVENT_STATUS, uint64_t(std::max<int64_t>(sample, 0)), total, status); }
    if (sample != last_sample) { last_sample = sample; emit(CBC_EVENT_PROGRESS, uint64_t(std::max<int64_t>(sample, 0)), total, status); }
  }
  Host() { live_hosts.fetch_add(1); }
  ~Host() {
    if (session) {
      session->cancel(true);
      bakes.clear();
      nodes.clear();
      graphs.clear();
      images.clear();
      session.reset();
    }
    live_hosts.fetch_sub(1);
  }
  CBCNode remember(Node *node, Graph *graph = nullptr) {
    for (auto &[id, entry] : nodes) if (entry.node == node && entry.graph == graph) return id;
    auto id = new_id(); nodes.emplace(id, Entry{node, graph}); return id;
  }
  Entry &entry(CBCNode id) {
    auto it = nodes.find(id); require(it != nodes.end(), "Invalid or foreign node handle"); return it->second;
  }
  Graph &graph(CBCNode id) {
    auto shader = dynamic_cast<Shader *>(entry(id).node);
    require(shader != nullptr, "Handle is not a scene shader");
    auto it = graphs.find(shader); require(it != graphs.end(), "Shader has no authoring graph");
    return *it->second;
  }
  void changed(Entry &entry) {
    entry.node->tag_modified();
    if (entry.graph) { entry.graph->dirty = true; return; }
    Scene *scene = session->scene.get();
    if (auto shader = dynamic_cast<Shader *>(entry.node)) { edited_shaders.insert(shader); shader->tag_update(scene); }
    else if (auto light = dynamic_cast<Light *>(entry.node)) light->tag_update(scene);
    else if (auto geometry = dynamic_cast<Geometry *>(entry.node)) geometry->tag_update(scene, true);
    else if (auto object = dynamic_cast<Object *>(entry.node)) object->tag_update(scene);
  }
  void prepare_graphs() {
    for (auto &[shader, stored] : graphs) {
      auto &graph = *stored;
      if (!graph.dirty) continue;
      for (auto &[node, pixels] : graph.images) {
        ImageParams params;
        if (auto image=dynamic_cast<ImageTextureNode *>(node)) params=image->image_params();
        else if (auto image=dynamic_cast<EnvironmentTextureNode *>(node)) params=image->image_params();
        else throw Failure(CBC_FAILED,"Unknown bound texture type");
        node->handle=session->scene->image_manager->add_image(std::make_unique<MemoryImageLoader>(pixels),params,false);
      }
      // Authoring handles remain valid when Cycles simplifies its render graph.
      auto target = std::make_unique<ShaderGraph>();
      std::map<ShaderNode *, ShaderNode *> copies;
      copies.emplace(graph.authoring->output(), target->output());
      for (auto node : graph.authoring->nodes) {
        if (node != graph.authoring->output()) copies.emplace(node, node->clone(target.get()));
      }
      for (auto node : graph.authoring->nodes) {
        for (const auto &socket : node->type->inputs) {
          if (node == graph.authoring->output() && socket.struct_offset >= 0 && socket.type != SocketType::CLOSURE) copies.at(node)->copy_value(socket, *node, socket);
        }
        for (auto input : node->inputs) if (input->link) {
          auto from = copies.at(input->link->parent)->output(input->link->name());
          auto to = copies.at(node)->input(input->name());
          require(from && to, "Cannot recreate shader link", CBC_FAILED);
          target->connect(from, to);
        }
      }
      shader->set_graph(std::move(target));
      shader->tag_update(session->scene.get());
      graph.dirty = false;
    }
  }
};
std::mutex hosts_mutex;
std::map<CBCHost, std::shared_ptr<Host>> hosts;
std::once_flag exit_hook;
void shutdown_hosts() noexcept {
  std::map<CBCHost, std::shared_ptr<Host>> remaining;
  { std::lock_guard lock(hosts_mutex); remaining.swap(hosts);
    for (auto &[id, host] : remaining) { host->closing = true; host->cancellation = true; } }
  for (auto &[id, host] : remaining) {
    std::lock_guard lock(host->operation);
    if (host->session) { host->session->cancel(true); host->bakes.clear(); host->nodes.clear(); host->graphs.clear(); host->images.clear(); host->session.reset(); }
  }
}

// Registers shutdown_hosts to run at process exit while every thread still exists.
#ifdef _WIN32
// A DLL's own atexit table runs at DLL_PROCESS_DETACH, after ExitProcess has
// terminated every other thread (Cycles' session and scheduler threads among
// them). The process-wide table (_crt_atexit, the one exit() runs first) runs
// while they still exist, as atexit does on Linux and macOS. The DLL is pinned so
// the registered function can never outlive an unloaded library.
void __cdecl shutdown_hosts_at_exit() { shutdown_hosts(); }
bool register_exit_hook() {
  HMODULE self = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCWSTR>(&shutdown_hosts_at_exit), &self))
    return false;
  return _crt_atexit(shutdown_hosts_at_exit) == 0;
}
#else
bool register_exit_hook() { return std::atexit(shutdown_hosts) == 0; }
#endif

std::shared_ptr<Host> lookup(CBCHost handle) {
  std::lock_guard lock(hosts_mutex);
  auto it = hosts.find(handle); require(it != hosts.end(), "Invalid or destroyed host handle"); return it->second;
}
template<class Function> int edit(CBCHost handle, Function &&function) {
  return boundary([&] {
    auto host = lookup(handle); std::lock_guard lock(host->operation);
    require(!host->closing, "Host is closing", CBC_NOT_READY); function(*host);
  });
}
const SocketType &socket(Entry &entry, const char *name) {
  require(name && *name, "Socket name is required");
  auto input = entry.node->type->find_input(ustring(name));
  require(input != nullptr, "Unknown input socket"); return *input;
}
void count_ok(const void *values, uint64_t count) {
  require(count <= INT_MAX && (count == 0 || values), "Invalid value count or pointer");
}
bool reaches(ShaderNode *start, ShaderNode *wanted, std::set<ShaderNode *> &visited) {
  if (start == wanted) return true;
  if (!visited.insert(start).second) return false;
  for (auto output : start->outputs) for (auto input : output->links)
    if (reaches(input->parent, wanted, visited)) return true;
  return false;
}
#ifdef __APPLE__
// The loader does not enforce a library's minimum OS version, so check the
// build's deployment target before any Cycles or Metal code runs.
bool supported_macos() {
  char version[32] = {}; size_t size = sizeof(version) - 1;
  if (sysctlbyname("kern.osproductversion", version, &size, nullptr, 0) != 0) return false;
  return std::atoi(version) >= 15;
}
// Metal compiles kernels at run time from the source tree shipped beside this
// library; Cycles otherwise looks beside the consuming executable.
void initialize_paths() {
  static std::once_flag once;
  std::call_once(once, [] {
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void *>(&cbc_host_abi_version), &info) && info.dli_fname)
      path_init(path_join(path_dirname(info.dli_fname), "codebrix-cycles"));
  });
}
#endif
}

namespace {
uint32_t gpu_backend() {
#ifdef WITH_METAL
  return CBC_HOST_METAL;
#elif defined(WITH_WEBGPU)
  return CBC_HOST_WEBGPU;
#else
  return 0;
#endif
}
}
namespace {
#include "host_edits.inc"
}
#include "host_properties.inc"
#include "host_scene.inc"

#ifdef WITH_WEBGPU
namespace ccl {
// Defined with the WebGPU device (native/cycles/device_tier.h, device.cpp).
void host_tier_config(uint32_t tier, bool record_off, bool read_only, bool canary, const char *directory);
std::string webgpu_device_report_json(Device *device);
std::string webgpu_adapter_report_json(Device *device);
bool webgpu_recover(Device *device, std::string &reason);
std::string webgpu_unavailable_reason();
}
#endif
#ifdef WITH_METAL
namespace ccl {
// Defined in metal_recovery.mm (Objective-C++), which can see MetalDevice.
bool codebrix_metal_clear_error(Device *device);
}
#endif
extern "C" int cbc_host_tier_config(const CBCTierConfig *config, const char *directory) {
  return boundary([&] {
    require(config && config->struct_size == sizeof(*config) && config->abi_version == CBC_HOST_ABI, "Invalid tier configuration");
    require(std::all_of(std::begin(config->reserved), std::end(config->reserved), [](auto v) { return v == 0; }), "Unknown configuration flags");
    require(config->tier <= CBC_TIER_CONSERVATIVE, "Invalid tier");
    require((config->flags & ~uint32_t(CBC_TIER_RECORD_OFF | CBC_TIER_RECORD_READ_ONLY | CBC_TIER_CANARY)) == 0, "Unknown tier flags");
#ifdef WITH_WEBGPU
    ccl::host_tier_config(config->tier, config->flags & CBC_TIER_RECORD_OFF, config->flags & CBC_TIER_RECORD_READ_ONLY,
                          config->flags & CBC_TIER_CANARY, directory ? directory : "");
#else
    (void)directory;
#endif
  });
}
namespace {
std::string device_report(Host &host) {
    std::ostringstream out; out.imbue(std::locale::classic());
    Device *device = host.session ? host.session->device.get() : nullptr;
    require(device != nullptr, "Host has no device", CBC_FAILED);
    out << "{\"abiVersion\":" << CBC_HOST_ABI << ",\"backend\":";
#ifdef WITH_WEBGPU
    if (device->info.type == DEVICE_WEBGPU) {
      const std::string report = webgpu_device_report_json(device);
      require(!report.empty(), "WebGPU device report unavailable", CBC_FAILED);
      out << "\"webgpu\",\"pipelines\":" << report;
      // The adapter and WebGPU backend (Vulkan, D3D12, Metal) the device runs on.
      const std::string adapter = webgpu_adapter_report_json(device);
      if (!adapter.empty()) out << ",\"webgpu\":" << adapter;
    } else
#endif
    out << (device->info.type == DEVICE_METAL ? "\"metal\"" : "\"cpu\"") << ",\"pipelines\":null";
    out << '}';
    return out.str();
}
}
namespace {
// Device-error recovery contract (ABI.md "Device errors"). A render that fails
// with a device error leaves the error on the Cycles device. The next render
// on the host asks the device to recover: the WebGPU device clears the error
// when the device itself is intact (no lost device, no uncaptured WebGPU
// error); the scene is then tagged for a complete device update, as
// Scene::reset does without adding default shaders, so every manager uploads
// its data again. The device record and pipeline tier are not touched.
// Cycles' Metal device records the errors a scene causes (for example an image
// beyond the 16384-pixel texture limit) without losing the Metal device, so the
// host clears that error and re-uploads the scene the same way. A device that
// cannot recover (fatal WebGPU error, CPU device error) keeps the host faulted:
// every later render fails with "Host faulted".
void recover_device(Host &host) {
  Device *device = host.session->device.get();
  if (!device->have_error()) return;
  const std::string previous = device->error_message();
  std::string reason = "the device has no recovery path";
  bool recovered = false;
#ifdef WITH_WEBGPU
  recovered = webgpu_recover(device, reason);
#endif
#ifdef WITH_METAL
  if (device->info.type == DEVICE_METAL) {
    recovered = codebrix_metal_clear_error(device);
    if (!recovered) reason = "the device is not Cycles' Metal device";
  }
#endif
  if (!recovered)
    throw Failure(CBC_FAILED, "Host faulted: the device cannot recover from an earlier error (" + previous + "; " + reason +
                  "); destroy the host and create a new one");
  Scene &scene = *host.session->scene;
  scene.camera->tag_modified(); scene.dicing_camera->tag_modified(); scene.film->tag_modified(); scene.background->tag_modified();
  scene.background->tag_update(&scene);
  scene.integrator->tag_update(&scene, Integrator::UPDATE_ALL);
  scene.scene_attribute->tag_update(&scene, SceneAttributes::UPDATE_ALL);
  for (Shader *shader : scene.shaders) shader->tag_modified();
  scene.shader_manager->tag_update(&scene, ShaderManager::UPDATE_ALL);
  for (Geometry *geometry : scene.geometry) { geometry->tag_modified(); geometry->need_update_rebuild = true; }
  for (Object *object : scene.objects) object->tag_modified();
  scene.object_manager->tag_update(&scene, ObjectManager::UPDATE_ALL);
  scene.geometry_manager->tag_update(&scene, GeometryManager::UPDATE_ALL);
  scene.light_manager->tag_update(&scene, LightManager::UPDATE_ALL);
  scene.particle_system_manager->tag_update(&scene);
  scene.procedural_manager->tag_update();
  scene.image_manager->tag_update();
  std::lock_guard lock(host.event_mutex);
  host.emit(CBC_EVENT_STATUS, 0, 0, "Device recovered after an earlier error (" + previous + "); scene data re-uploaded");
}
}
extern "C" int cbc_host_device_report_json(CBCHost handle, char *json, uint64_t capacity, uint64_t *required) {
  return edit(handle, [&](Host &host) {
    require(required && (!capacity || json), "Invalid result buffer or size pointer");
    copy_text(device_report(host), json, capacity, required);
  });
}

extern "C" uint32_t cbc_host_abi_version(void) { return CBC_HOST_ABI; }
extern "C" int cbc_host_error(char *buffer, uint64_t capacity, uint64_t *required) {
  if (!required || (capacity && !buffer)) return CBC_INVALID;
  *required = std::strlen(last_error) + 1;
  if (capacity < *required) return CBC_SMALL_BUFFER;
  std::memcpy(buffer, last_error, size_t(*required)); return CBC_OK;
}
extern "C" int cbc_host_create(const CBCHostConfig *config, CBCHost *out) {
  if (out) *out = 0;
  return boundary([&] {
    require(config && out && config->struct_size == sizeof(*config) && config->abi_version == CBC_HOST_ABI, "Invalid host configuration");
    require(std::all_of(std::begin(config->reserved), std::end(config->reserved), [](auto v) { return v == 0; }), "Unknown configuration flags");
    require(config->threads <= 1024, "Invalid thread count");
    require(config->backend == CBC_HOST_WEBGPU || config->backend == CBC_HOST_CPU_REFERENCE || config->backend == CBC_HOST_METAL ||
            config->backend == CBC_HOST_GPU, "Explicit backend selection required");
    const uint32_t backend = config->backend == CBC_HOST_GPU ? gpu_backend() : config->backend;
    require(backend != 0, "This binary has no GPU backend", CBC_UNSUPPORTED);
#ifdef __APPLE__
    require(supported_macos(), "This library requires macOS 15.0 or later", CBC_UNSUPPORTED);
    initialize_paths();
#endif
#ifndef WITH_METAL
    require(backend != CBC_HOST_METAL, "The Metal backend exists only in Apple Silicon builds", CBC_UNSUPPORTED);
#endif
    std::lock_guard configuration_lock(configuration_mutex);
    auto host = std::make_shared<Host>();
    auto type = backend == CBC_HOST_WEBGPU ? DEVICE_WEBGPU : backend == CBC_HOST_METAL ? DEVICE_METAL : DEVICE_CPU;
    host->backend = backend;
    auto devices = Device::available_devices(DeviceTypeMask(1 << type));
#ifdef WITH_WEBGPU
    if (devices.empty() && type == DEVICE_WEBGPU) {
      // Keep the WebGPU device's own reason (ABI mismatch, no adapter, a limit) in the message.
      const std::string reason = webgpu_unavailable_reason();
      require(reason.empty(), ("Requested device is unavailable: " + reason).c_str(), CBC_UNSUPPORTED);
    }
#endif
    require(!devices.empty(), "Requested device is unavailable", CBC_UNSUPPORTED);
    SessionParams params; params.device = devices[0];
    if (type == DEVICE_METAL) {
      // Blender's automatic choice: Apple's ray-tracing API only on GPUs with
      // ray-tracing hardware (M3 and later). CYCLES_METALRT still overrides it.
      params.device.use_hardware_raytracing = params.device.use_metalrt_by_default;
      require(path_is_file(path_get("source/kernel/device/metal/kernel.metal")),
              "Metal kernel sources are missing beside the native library", CBC_FAILED);
    }
    params.denoise_device = params.device;
    params.headless = true; params.background = true; params.threads = int(config->threads);
    params.use_auto_tile = false; params.use_resolution_divider = false;
    SceneParams scene; scene.bvh_layout = BVH_LAYOUT_BVH2; scene.bvh_type = BVH_TYPE_STATIC;
    scene.use_bvh_unaligned_nodes = false; scene.use_texture_cache = false;
    host->session = std::make_unique<Session>(params, scene);
    require(!host->session->progress.get_error(), host->session->progress.get_error_message().c_str(), CBC_FAILED);
    require(host->session->device->info.type == type, "Device selection changed unexpectedly", CBC_FAILED);
    host->session->set_output_driver(std::make_unique<HostOutput>(host->result));
    Host *raw = host.get();
    host->session->progress.set_cancel_callback([raw] {
      if (raw->cancellation.load() || raw->closing.load()) raw->session->progress.set_cancel("Cancelled by caller");
    });
    host->session->progress.set_update_callback([raw] { raw->progress_event(); });
    for (auto pass : host->session->scene->passes) if (pass->get_type() == PASS_COMBINED) pass->set_name(ustring("Combined"));
    // Register cleanup after native registry and scheduler initialization, so
    // live contexts close before those process-level services are destroyed.
    (void)NodeType::type_names();
    std::call_once(exit_hook, [] { require(register_exit_hook(), "Cannot register native cleanup", CBC_FAILED); });
    auto id = new_id(); { std::lock_guard lock(hosts_mutex); hosts.emplace(id, std::move(host)); } *out = id;
  });
}
extern "C" int cbc_host_color_config_path(const char *path) {
  return boundary([&] {
    require(path && *path, "Color configuration path is required");
    std::lock_guard lock(configuration_mutex);
    require(live_hosts.load() == 0, "Color configuration requires all host lifetimes and calls to finish", CBC_NOT_READY);
    auto config = OCIO_NAMESPACE::Config::CreateFromFile(path);
    config->validate();
    OCIO_NAMESPACE::SetCurrentConfig(config);
  });
}
extern "C" int cbc_host_destroy(CBCHost handle) {
  return boundary([&] {
    std::shared_ptr<Host> host;
    { std::lock_guard lock(hosts_mutex); auto it = hosts.find(handle); require(it != hosts.end(), "Invalid or destroyed host handle");
      host = it->second; host->closing = true; host->cancellation = true; hosts.erase(it); }
    std::lock_guard lock(host->operation);
    host->session->cancel(true);
    host->bakes.clear(); host->nodes.clear(); host->graphs.clear(); host->images.clear(); host->session.reset();
  });
}
extern "C" int cbc_host_cancel(CBCHost handle) {
  return boundary([&] { auto host = lookup(handle); require(host->rendering, "No render is active", CBC_NOT_READY); host->cancellation = true; });
}
extern "C" int cbc_host_is_rendering(CBCHost handle, uint32_t *rendering) {
  return boundary([&] { require(rendering, "Missing output pointer"); *rendering = lookup(handle)->rendering.load(); });
}
extern "C" int cbc_host_scene_node(CBCHost handle, const char *name, CBCNode *out) {
  return edit(handle, [&](Host &host) {
    require(name && out, "Missing node name or output"); auto &scene = *host.session->scene; Node *node = nullptr;
    if (std::strcmp(name, "camera") == 0) node = scene.camera;
    else if (std::strcmp(name, "background") == 0) node = scene.background;
    else if (std::strcmp(name, "film") == 0) node = scene.film;
    else if (std::strcmp(name, "integrator") == 0) node = scene.integrator;
    require(node, "Unknown singleton node"); *out = host.remember(node);
  });
}
extern "C" int cbc_host_create_node(CBCHost handle, const char *type, CBCNode *out) {
  return edit(handle, [&](Host &host) {
    require(type && out, "Missing node type or output"); auto &scene = *host.session->scene; Node *node = nullptr;
#define CREATE(name, Class) if (std::strcmp(type, name) == 0) node = scene.create_node<Class>(); else
    CREATE("mesh", Mesh) CREATE("hair", Hair) CREATE("pointcloud", PointCloud) CREATE("volume", Volume)
    CREATE("object", Object) CREATE("particle_system", ParticleSystem) CREATE("pass", Pass)
    CREATE("pointlight", PointLight) CREATE("spotlight", SpotLight) CREATE("arealight", AreaLight)
    CREATE("sunlight", SunLight) CREATE("backgroundlight", BackgroundLight)
    if (std::strcmp(type, "shader") == 0) {
      auto shader = scene.create_node<Shader>(); node = shader;
      shader->set_graph(std::make_unique<ShaderGraph>());
      auto graph = std::make_unique<Graph>(); graph->shader = shader; host.graphs.emplace(shader, std::move(graph));
    } else throw Failure(CBC_UNSUPPORTED, "Unsupported scene node type");
#undef CREATE
    *out = host.remember(node);
  });
}
extern "C" int cbc_host_shader_output(CBCHost handle, CBCNode shader, CBCNode *out) {
  return edit(handle, [&](Host &host) { require(out, "Missing output handle"); auto &graph = host.graph(shader); *out = host.remember(graph.authoring->output(), &graph); });
}
extern "C" int cbc_host_shader_node(CBCHost handle, CBCNode shader, const char *name, CBCNode *out) {
  return edit(handle, [&](Host &host) {
    require(name && out, "Missing shader node type or output"); auto &graph = host.graph(shader);
    auto type = NodeType::find(ustring(name)); require(type && type->type == NodeType::SHADER && type->create, "Unknown shader node type");
    require(std::strcmp(name, "output") != 0, "Use the existing shader output node");
    auto node = graph.authoring->create_node(type); graph.dirty = true; *out = host.remember(node, &graph);
  });
}
extern "C" int cbc_host_connect(CBCHost handle, CBCNode from, const char *output, CBCNode to, const char *input) {
  return edit(handle, [&](Host &host) {
    require(output && input, "Missing socket name"); auto &a = host.entry(from); auto &b = host.entry(to);
    require(a.graph && a.graph == b.graph, "Shader links must remain within one graph");
    auto first = static_cast<ShaderNode *>(a.node), second = static_cast<ShaderNode *>(b.node);
    auto source = first->output(ustring(output)); auto target = second->input(ustring(input));
    require(source && target, "Unknown shader link socket");
    require(source->type() == target->type() || (SocketType::is_float3(source->type()) && SocketType::is_float3(target->type())), "Shader link type conversion is not implemented", CBC_UNSUPPORTED);
    std::set<ShaderNode *> visited; require(!reaches(second, first, visited), "Shader link would create a cycle");
    if (target->link) a.graph->authoring->disconnect(target);
    a.graph->authoring->connect(source, target); a.graph->dirty = true;
  });
}
extern "C" int cbc_host_set_floats(CBCHost handle, CBCNode id, const char *name, const float *values, uint64_t count) {
  return edit(handle, [&](Host &host) {
    count_ok(values, count); for (uint64_t i = 0; i < count; ++i) require(std::isfinite(values[i]), "Nonfinite socket value");
    auto &entry = host.entry(id); auto &input = socket(entry, name); auto &node = *entry.node;
    stored_property(input); before_edit(host, entry, name);
    if (input.type == SocketType::FLOAT) { require(count == 1, "Expected one float"); node.set(input, values[0]); }
    else if (input.type == SocketType::POINT2) { require(count == 2, "Expected two floats"); node.set(input, make_float2(values[0], values[1])); }
    else if (SocketType::is_float3(input.type)) { require(count == 3, "Expected three floats"); node.set(input, make_float3(values[0], values[1], values[2])); }
    else if (input.type == SocketType::TRANSFORM) { require(count == 12, "Expected a 3x4 row-major transform"); Transform value;
      value.x = make_float4(values[0],values[1],values[2],values[3]); value.y = make_float4(values[4],values[5],values[6],values[7]); value.z = make_float4(values[8],values[9],values[10],values[11]); node.set(input,value); }
    else if (input.type == SocketType::FLOAT_ARRAY) {
      array<float> copy; copy.resize(size_t(count)); for (size_t i=0;i<count;++i) copy[i]=values[i]; node.set(input,copy);
    } else if (input.type == SocketType::POINT2_ARRAY) {
      require(count%2==0,"Expected complete float2 elements"); array<float2> copy; copy.resize(size_t(count/2));
      for (size_t i=0;i<copy.size();++i) copy[i]=make_float2(values[2*i],values[2*i+1]);
      node.set(input,copy);
    } else if (float3_array(input.type)) {
      require(count%3==0,"Expected complete float3 elements"); array<packed_float3> copy; copy.resize(size_t(count/3));
      for (size_t i=0;i<copy.size();++i) copy[i]=make_float3(values[3*i],values[3*i+1],values[3*i+2]);
      node.set(input,copy);
    } else if (input.type == SocketType::TRANSFORM_ARRAY) {
      require(count%12==0,"Expected complete 3x4 transforms"); array<Transform> copy; copy.resize(size_t(count/12));
      for (size_t i=0;i<copy.size();++i) copy[i]=read_transform(values+12*i);
      node.set(input,copy);
    } else throw Failure(CBC_INVALID, "Socket does not contain floating-point values");
    host.changed(entry);
  });
}
extern "C" int cbc_host_set_ints(CBCHost handle, CBCNode id, const char *name, const int64_t *values, uint64_t count) {
  return edit(handle, [&](Host &host) {
    count_ok(values, count); auto &entry = host.entry(id); auto &input = socket(entry, name); auto &node = *entry.node;
    stored_property(input); before_edit(host, entry, name);
    if (input.type == SocketType::BOOLEAN_ARRAY || input.type == SocketType::INT_ARRAY) {
      if (input.type == SocketType::BOOLEAN_ARRAY) {
        for (size_t i=0;i<count;++i) require(values[i]==0 || values[i]==1,"Boolean must be zero or one");
        array<bool> copy; copy.resize(size_t(count)); for (size_t i=0;i<count;++i) copy[i]=values[i]!=0; node.set(input,copy);
      } else {
        for (size_t i=0;i<count;++i) require(values[i]>=INT_MIN && values[i]<=INT_MAX,"Integer exceeds socket range");
        array<int> copy; copy.resize(size_t(count)); for (size_t i=0;i<count;++i) copy[i]=int(values[i]); node.set(input,copy);
      }
      host.changed(entry); return;
    }
    require(count == 1, "Scalar integer socket required"); auto value = values[0];
    if (input.type == SocketType::BOOLEAN) { require(value == 0 || value == 1, "Boolean must be zero or one"); node.set(input, value != 0); }
    else if (input.type == SocketType::INT || input.type == SocketType::ENUM) { require(value >= INT_MIN && value <= INT_MAX, "Integer exceeds socket range");
      if (input.type == SocketType::ENUM) { require(input.enum_values && input.enum_values->exists(int(value)), "Unknown enumeration value"); }
      node.set(input, int(value)); }
    else if (input.type == SocketType::UINT) { require(value >= 0 && uint64_t(value) <= UINT_MAX, "Unsigned socket out of range"); node.set(input, uint(value)); }
    else if (input.type == SocketType::UINT64) { require(value >= 0, "Unsigned socket out of range"); node.set(input, uint64_t(value)); }
    else throw Failure(CBC_INVALID, "Socket does not contain integer values");
    host.changed(entry);
  });
}
extern "C" int cbc_host_set_string(CBCHost handle, CBCNode id, const char *name, const char *value) {
  return edit(handle, [&](Host &host) {
    require(value, "Missing string value"); auto &entry = host.entry(id); auto &input = socket(entry, name);
    stored_property(input); before_edit(host, entry, name);
#ifdef CBC_HOST_NO_IMAGE_FILES
    // This build contains no image file readers; callers bind decoded pixels.
    require(!*value || input.name != "filename" || !dynamic_cast<ImageSlotTextureNode *>(entry.node),
            "Image files are not read by this library; bind decoded pixels with cbc_host_image_bind", CBC_UNSUPPORTED);
#endif
    // Native automatic colorspace is the default-constructed empty ustring.
    // OIIO distinguishes its null interned pointer from ustring("").
    if (input.type == SocketType::STRING) entry.node->set(input, *value ? ustring(value) : ustring());
    else if (input.type == SocketType::ENUM) { require(input.enum_values && input.enum_values->exists(ustring(value)), "Unknown enumeration label"); entry.node->set(input, (*input.enum_values)[value]); }
    else throw Failure(CBC_INVALID, "Socket is not a string or enumeration");
    host.changed(entry);
  });
}
extern "C" int cbc_host_set_nodes(CBCHost handle, CBCNode id, const char *name, const CBCNode *values, uint64_t count) {
  return edit(handle, [&](Host &host) {
    count_ok(values, count); auto &entry = host.entry(id); auto &input = socket(entry, name);
    stored_property(input);
    require(input.type == SocketType::NODE || input.type == SocketType::NODE_ARRAY, "Socket does not accept node references");
    if (input.type == SocketType::NODE) require(count == 1, "Expected one node handle");
    array<Node *> nodes; nodes.resize(size_t(count));
    for (size_t i = 0; i < count; ++i) {
      auto node = values[i] ? host.entry(values[i]).node : nullptr;
      require(node || input.type == SocketType::NODE, "Null array node reference");
      require(!node || !input.node_type || node->is_a(input.node_type), "Node reference has the wrong type"); nodes[i] = node;
      // A baked volume cannot gain users (host_edits.inc).
      auto volume = dynamic_cast<Geometry *>(node);
      if (volume && volume->is_volume() && volume->transform_applied && dynamic_cast<Object *>(entry.node)) throw Failure(CBC_NOT_READY, volume_refusal);
    }
    before_edit(host, entry, name);
    if (input.type == SocketType::NODE) entry.node->set(input, nodes[0]); else entry.node->set(input, nodes);
    host.changed(entry);
  });
}
extern "C" int cbc_host_mesh(CBCHost handle, CBCNode id, const float *xyz, uint64_t vertices, const uint32_t *indices, uint64_t triangles) {
  return edit(handle, [&](Host &host) {
    require(vertices <= INT_MAX / 3 && triangles <= INT_MAX / 3 && (vertices == 0 || xyz) && (triangles == 0 || indices), "Invalid mesh dimensions");
    auto &entry = host.entry(id); auto mesh = dynamic_cast<Mesh *>(entry.node); require(mesh, "Handle is not a mesh");
    for (uint64_t i = 0; i < vertices * 3; ++i) require(std::isfinite(xyz[i]), "Mesh position is nonfinite");
    for (uint64_t i = 0; i < triangles * 3; ++i) require(indices[i] < vertices, "Triangle index is outside the vertex array");
    before_edit(host, entry, nullptr);
    mesh->resize_mesh(int(vertices), int(triangles));
    for (size_t i = 0; i < vertices; ++i) mesh->get_position_for_write()[i] = make_float3(xyz[3*i],xyz[3*i+1],xyz[3*i+2]);
    for (size_t i = 0; i < triangles * 3; ++i) mesh->get_triangles()[i] = int(indices[i]);
    std::ranges::fill(mesh->get_shader(), 0); std::ranges::fill(mesh->get_smooth(), false);
    mesh->tag_triangles_modified(); host.changed(entry);
  });
}
extern "C" int cbc_host_render(CBCHost handle, const CBCRenderConfig *config, float *rgba, uint64_t count) {
  return edit(handle, [&](Host &host) {
    require(config && config->struct_size == sizeof(*config), "Invalid render configuration");
    require(std::all_of(std::begin(config->reserved), std::end(config->reserved), [](auto value) { return value == 0; }), "Unknown render flags");
    require(config->width && config->height && config->width <= 32768 && config->height <= 32768 && config->samples && config->samples <= INT_MAX, "Invalid render dimensions or sample count");
    require(rgba && count == uint64_t(config->width) * config->height * 4 && count <= SIZE_MAX / sizeof(float), "Incorrect output buffer size");
    validate_geometry(host);
    // Restore baked geometry whose fresh-scene bake differs and capture the
    // authored attributes of geometry this render may bake (host_edits.inc).
    struct Prune { Host &host; ~Prune() { prune_bakes(host); } } prune{host};
    reconcile_bakes(host);
    host.prepare_graphs();
    auto &session = *host.session; auto &scene = *session.scene;
    host.requested_samples = config->samples;
    recover_device(host);
    scene.camera->set_full_width(int(config->width)); scene.camera->set_full_height(int(config->height));
    scene.camera->set_screen_size(int(config->width), int(config->height)); scene.camera->compute_auto_viewplane();
    auto params = session.params; params.samples = int(config->samples);
    // A new synchronous render owns its sample budget, including decreases.
    // Session's incremental update otherwise preserves a larger prior budget.
    scene.integrator->set_aa_samples(params.samples);
    BufferParams buffers; buffers.width = buffers.full_width = buffers.window_width = int(config->width);
    buffers.height = buffers.full_height = buffers.window_height = int(config->height);
    host.result = Result{}; host.result.width = buffers.width; host.result.height = buffers.height;
    host.result.pixels.assign(size_t(count), -1000.0f); host.cancellation = false;
    // Caller-created passes with a name are captured for cbc_host_pass_floats.
    for (auto &[id, entry] : host.nodes) if (auto pass = dynamic_cast<Pass *>(entry.node)) {
      const std::string name = pass->get_name().string();
      if (name.empty() || name == "Combined" || pass->get_type() == PASS_COMBINED) continue;
      if (std::any_of(host.result.passes.begin(), host.result.passes.end(), [&](auto &p) { return p.name == name; })) continue;
      PassResult value; value.name = name; value.type = int(pass->get_type());
      value.components = unsigned(std::max(1, pass->get_info().num_components));
      value.pixels.assign(size_t(config->width) * config->height * value.components, 0.0f);
      host.result.passes.push_back(std::move(value));
    }
    host.requested_samples = config->samples; host.last_sample = -1; host.last_status.clear();
    struct Active { Host &host; Active(Host &h) : host(h) { host.rendering = true; } ~Active() { host.rendering = false; } } active(host);
    session.progress.reset();
    session.reset(params, buffers); session.start(); session.wait();
    try {
      require(!host.cancellation && !host.closing, "Render cancelled", CBC_CANCELLED);
      require(!session.progress.get_error(), session.progress.get_error_message().c_str(), CBC_FAILED);
      require(!session.device->have_error(), session.device->error_message().c_str(), CBC_FAILED);
      require(!session.progress.get_cancel(), "Render cancelled", CBC_CANCELLED);
      require(host.result.writes == 1 && host.result.valid, "Missing complete Combined output", CBC_FAILED);
      require(std::all_of(host.result.pixels.begin(), host.result.pixels.end(), [](float value) { return std::isfinite(value); }), "Nonfinite Combined output", CBC_FAILED);
    } catch (const Failure &failure) {
      if (failure.code == CBC_FAILED) { std::lock_guard lock(host.event_mutex); host.emit(CBC_EVENT_ERROR, 0, 0, failure.what()); }
      host.result.passes.clear();
      throw;
    }
    std::copy(host.result.pixels.begin(), host.result.pixels.end(), rgba);
    host.result.complete = true;
  });
}

#include "host_api.inc"
