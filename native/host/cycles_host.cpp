// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
#include "cycles_host.h"
#include "device/device.h"
#include "scene/scene.h"
#include "scene/background.h"
#include "scene/bake.h"
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
#include "subd/dice.h"
#include "util/image_metadata.h"
#include <OpenColorIO/OpenColorIO.h>
#include "session/output_driver.h"
#include "session/session.h"
#include "integrator/path_trace.h"
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
#include <chrono>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <sstream>
#include <iomanip>
#include <locale>
#include <functional>

#if !defined(CBC_HOST_LIBRARY_VERSION) || !defined(CBC_HOST_CYCLES_REVISION)
#  error "targets.cmake stamps CBC_HOST_LIBRARY_VERSION and CBC_HOST_CYCLES_REVISION"
#endif

// Tiled rendering (cbc_host_tile_size, PROVISIONAL): the in-memory tile store of the overlay's integrator/path_trace.cpp
// (native/tools/cycles_overlay.py), where Cycles keeps a tiled render's finished tiles in place of its tile file. The
// tiles held by a path tracer's frames, finished or not; with discard the frames are dropped.
CCL_NAMESPACE_BEGIN
int codebrix_memory_tiles(const PathTrace *path_trace, bool discard);
// The delivered film (PROVISIONAL): during an output driver write, the parameters of the render buffers its passes are read
// with (the same overlay); null outside a write.
const BufferParams *codebrix_delivered_buffer_params();
CCL_NAMESPACE_END

namespace {
using namespace ccl;
thread_local char last_error[2048] = {};
// Names the callback into the caller that runs on this thread (an event callback, or a tiled image's read or release
// callback); host calls are refused while it runs.
thread_local const char *in_callback = nullptr;
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
  if (in_callback) { std::snprintf(last_error, sizeof(last_error), "Host functions cannot be called from %s", in_callback); return CBC_NOT_READY; }
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
  std::map<ImageSlotTextureNode *,ImageBinding> images;
};
// A binding made by cbc_host_image_bind_tiles (PROVISIONAL): every entry has a UDIM tile number.
bool udim_binding(const ImageBinding &binding) { return !binding.empty() && binding.front().first != 0; }
struct Entry {
  Node *node; Graph *graph = nullptr;
  // A pass node's mode as the caller set it (-1: never set). Cycles'
  // Film::finalize_passes overwrites the node's mode when a render does not
  // denoise, so the request is kept here (render_pass_modes).
  int requested_pass_mode = -1; bool pass_mode_edit = false;
};
struct PassResult {
  std::string name;
  std::string source; // the Cycles pass read (a noisy request without denoising reads its pass's twin)
  int type = 0;
  unsigned components = 0;
  std::vector<float> pixels;
  bool written = false;
};
// The per-pixel input of a texture bake (host_bake.inc): the kernel reads it from
// the render buffer's BakePrimitive and BakeDifferential passes.
struct TextureBakeMap {
  std::vector<float> primitive, differential;
  std::vector<int32_t> triangles;
};
struct Result {
  std::vector<float> pixels;
  int width = 0, height = 0;
  // The rendered region (the camera border) in film pixels, lower-left origin; zero size means the whole film.
  int region_x = 0, region_y = 0, region_width = 0, region_height = 0;
  unsigned writes = 0;
  bool valid = false;
  // Named caller passes, read from the same complete tile as Combined.
  std::vector<PassResult> passes;
  bool complete = false;
  // A texture bake's map, handed to Cycles when it asks (read_render_tile): once, or once per tile of a tiled bake
  // (cbc_host_tile_size, PROVISIONAL), each tile reading its region.
  const TextureBakeMap *bake_map = nullptr;
  bool bake_map_read = false, bake_map_failed = false;
  // The film the output driver delivered is not the one requested (HostOutput::write_render_tile, PROVISIONAL): what
  // differed, for the render's or bake's CBC_FAILED.
  std::string delivered_error;
};
// The delivered film (PROVISIONAL): a pass read fills its destination with this quiet NaN first, a value no render writes
// (a kernel's NaN is the default NaN or carries its operands' payload), and every component must be overwritten.
constexpr uint32_t unwritten_marker = 0x7fc0c0deu;
float unwritten_value() { float value; std::memcpy(&value, &unwritten_marker, sizeof(value)); return value; }
size_t unwritten_count(const std::vector<float> &values) {
  size_t count = 0;
  for (float value : values) { uint32_t bits; std::memcpy(&bits, &value, sizeof(bits)); count += bits == unwritten_marker; }
  return count;
}
class HostOutput final : public OutputDriver {
  Result &result;
 public:
  // CBC_HOST_PROGRESSIVE_RESULTS (PROVISIONAL): the host reads the in-progress tile of a display update (Host::film_update).
  std::function<bool(const Tile &)> film_update;
  explicit HostOutput(Result &value) : result(value) {}
  bool update_render_tile(const Tile &tile) override { return film_update && film_update(tile); }
  // The delivered film (PROVISIONAL): the host verifies that what the output driver delivers is the film it asked for
  // before it uses it, and otherwise fails the render or bake by name (CBC_FAILED). The tile is the whole render, at offset
  // 0 and of the render's size (the film, or the camera border's region); the render buffers the passes are read with
  // (codebrix_delivered_buffer_params: the full frame of a tiled render, else the path tracer's effective big tile, which
  // its resolution divider scales, as a denoiser upscale factor does) have that size, a full window, and that place in a
  // film of the requested size; every pass the host reads has the components it reads; and every component of every
  // pixel of those passes is written (a marker no render writes is overwritten).
  bool delivered_layout(const Tile &tile) {
    const int width = result.region_width ? result.region_width : result.width;
    const int height = result.region_width ? result.region_height : result.height;
    std::ostringstream detail; detail.imbue(std::locale::classic());
    if (tile.offset.x != 0 || tile.offset.y != 0 || tile.size.x != width || tile.size.y != height || tile.full_size.x != width ||
        tile.full_size.y != height)
      detail << "a tile of " << tile.size.x << " x " << tile.size.y << " at (" << tile.offset.x << ", " << tile.offset.y << ") of "
             << tile.full_size.x << " x " << tile.full_size.y << " for a render of " << width << " x " << height;
    else if (const BufferParams *params = codebrix_delivered_buffer_params(); !params)
      detail << "no render buffer parameters for the write";
    else {
      if (params->width != width || params->height != height || params->window_x != 0 || params->window_y != 0 ||
          params->window_width != width || params->window_height != height || params->full_x != result.region_x ||
          params->full_y != result.region_y || params->full_width != result.width || params->full_height != result.height)
        detail << "render buffers of " << params->width << " x " << params->height << " (window " << params->window_width << " x "
               << params->window_height << " at (" << params->window_x << ", " << params->window_y << "), at (" << params->full_x
               << ", " << params->full_y << ") of " << params->full_width << " x " << params->full_height << ") for a render of "
               << width << " x " << height << " at (" << result.region_x << ", " << result.region_y << ") of " << result.width
               << " x " << result.height;
      else {
        const BufferPass *combined = params->find_pass("Combined");
        if (!combined || Pass::get_info(combined->type, combined->mode, combined->include_albedo, !combined->lightgroup.empty()).num_components != 4)
          detail << "no four-component Combined pass in the render buffers";
        for (const PassResult &pass : result.passes) {
          if (!detail.str().empty()) break;
          const BufferPass *found = params->find_pass(pass.source.empty() ? pass.name : pass.source);
          if (!found) continue;  // a pass the device did not write: cbc_host_pass_floats answers CBC_UNSUPPORTED
          const int components = std::max(1, Pass::get_info(found->type, found->mode, found->include_albedo, !found->lightgroup.empty()).num_components);
          if (unsigned(components) != pass.components)
            detail << "pass \"" << pass.name << "\" with " << components << " components in the render buffers, " << pass.components << " expected";
        }
      }
    }
    if (detail.str().empty()) return true;
    result.delivered_error = "The film the output driver delivered is not the one requested: " + detail.str();
    return false;
  }
  // Reads one pass of the render into its destination (a camera border's region at its place in the film), with the
  // unwritten-component check; false when the device did not write the pass.
  bool read_pass(const Tile &tile, const std::string &name, unsigned components, std::vector<float> &film) {
    const bool border = result.region_width != 0;
    std::vector<float> region;
    std::vector<float> &target = border ? region : film;
    if (border) region.assign(size_t(result.region_width) * result.region_height * components, unwritten_value());
    else std::fill(film.begin(), film.end(), unwritten_value());
    if (!tile.get_pass_pixels(name, int(components), target.data())) {
      if (!border) std::fill(film.begin(), film.end(), 0.0f);
      return false;
    }
    if (const size_t unwritten = unwritten_count(target)) {
      std::ostringstream detail; detail.imbue(std::locale::classic());
      detail << "The film the output driver delivered is not the one requested: " << (target.size() - unwritten) << " of "
             << target.size() << " values of pass \"" << name << "\" written (a pass of another resolution)";
      if (result.delivered_error.empty()) result.delivered_error = detail.str();
      return false;
    }
    if (border) {
      const size_t row = size_t(result.region_width) * components;
      for (int y = 0; y < result.region_height; ++y)
        std::copy_n(region.data() + y * row, row, film.data() + (size_t(result.region_y + y) * result.width + result.region_x) * components);
    }
    return true;
  }
  void write_render_tile(const Tile &tile) override {
    result.writes++;
    // A border render: Cycles' buffer holds only the region; it lands at its place in the film, the rest stays zero.
    result.valid = delivered_layout(tile) && read_pass(tile, "Combined", 4, result.pixels);
    if (!result.valid) return;
    for (auto &pass : result.passes) {
      pass.written = read_pass(tile, pass.source.empty() ? pass.name : pass.source, pass.components, pass.pixels);
      if (!result.delivered_error.empty()) { result.valid = false; return; }
    }
  }
  bool read_render_tile(const Tile &tile) override {
    if (!result.bake_map) return false;
    bool read = false;
    if (tile.offset.x == 0 && tile.offset.y == 0 && tile.size.x == result.width && tile.size.y == result.height) {
      read = tile.set_pass_pixels("BakePrimitive", 3, result.bake_map->primitive.data()) &&
             tile.set_pass_pixels("BakeDifferential", 4, result.bake_map->differential.data());
    } else if (tile.offset.x >= 0 && tile.offset.y >= 0 && tile.size.x > 0 && tile.size.y > 0 &&
               tile.offset.x + tile.size.x <= result.width && tile.offset.y + tile.size.y <= result.height) {
      // A tile of a tiled bake (cbc_host_tile_size): its region of the map (a bake renders no overscan).
      std::vector<float> primitive(size_t(tile.size.x) * tile.size.y * 3), differential(size_t(tile.size.x) * tile.size.y * 4);
      for (int y = 0; y < tile.size.y; ++y) {
        const size_t from = size_t(tile.offset.y + y) * result.width + tile.offset.x, to = size_t(y) * tile.size.x;
        std::copy_n(result.bake_map->primitive.data() + from * 3, size_t(tile.size.x) * 3, primitive.data() + to * 3);
        std::copy_n(result.bake_map->differential.data() + from * 4, size_t(tile.size.x) * 4, differential.data() + to * 4);
      }
      read = tile.set_pass_pixels("BakePrimitive", 3, primitive.data()) && tile.set_pass_pixels("BakeDifferential", 4, differential.data());
    }
    if (!read) result.bake_map_failed = true;
    result.bake_map_read = !result.bake_map_failed;
    return read;
  }
};
// Cycles' session with its path tracer's quick cancel reachable. PathTrace::cancel stops the batch of samples being path
// traced (the CPU work checks it per pixel and sample, the GPU work per kernel step) and waits for it; it is the part of
// Session::cancel(true) a render needs. Session::cancel also cancels the device, which on Metal drops the device's
// pending kernel compilations for the rest of its life, and the host's session renders again after a cancel.
struct HostSession : Session {
  using Session::Session;
  void cancel_path_trace() { path_trace_->cancel(); }
  // Tiled rendering (cbc_host_tile_size, PROVISIONAL): the path tracer whose frames the in-memory tile store keeps, and
  // whether the last render or bake had more than one tile.
  const PathTrace *path_tracer() const { return path_trace_.get(); }
  bool multiple_tiles() const { return tile_manager_.has_multiple_tiles(); }
};
std::mutex configuration_mutex;
std::atomic<uint64_t> live_hosts = 0;
struct Host {
  std::mutex operation;
  std::atomic<bool> closing = false, rendering = false, cancellation = false;
  // The callbacks of the host's tiled images (cbc_host_image_create_tiled, PROVISIONAL; host_scene_types.inc): serialized
  // here, stopped by every cancel request, and the first failed read of a render or bake.
  std::shared_ptr<TiledReads> tiled_reads = std::make_shared<TiledReads>();
  // A cancel request (cbc_host_cancel, a destroy, a callback's nonzero return, a failed tile read) wakes the render's
  // CancelWatch and stops the tiled images' reads, so a render waiting on reads stops after the read in progress.
  std::mutex cancel_mutex;
  std::condition_variable cancel_signal;
  void request_cancel(bool close = false) {
    { std::lock_guard lock(cancel_mutex); if (close) closing = true; cancellation = true; }
    cancel_signal.notify_all();
    tiled_reads->stop();
  }
  std::unique_ptr<HostSession> session;
  std::map<uint64_t, Entry> nodes;
  std::map<Shader *, std::unique_ptr<Graph>> graphs;
  std::map<CBCImage,std::shared_ptr<const MemoryImageData>> images;
  // Scene edits after a render (host_edits.inc): authored attributes of
  // geometry the static BVH path may bake, and shaders edited since the last render.
  std::map<Geometry *, BakeRecord> bakes;
  std::set<Shader *> edited_shaders;
  // The authored radius (every motion step) of each point cloud rendered as Gaussian splats since its last edit: Cycles
  // replaces it with the splats' bounding radius at the device update (host_edits.inc, keep_splat_radii).
  std::map<Geometry *, std::unique_ptr<AttributeSet>> splat_radii;
  // The authored attributes of each triangle mesh with true displacement from the render that displaced it to the next
  // edit of the mesh: GeometryManager::displace adds the displacement to the current positions (host_edits.inc,
  // reconcile_displacement). And the shaders prepare_graphs compiled for the render being prepared.
  std::map<Geometry *, DisplacedRecord> displaced;
  std::set<Shader *> recompiled;
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
  // The caller set a camera viewplane socket (lens shift, sensor fit): renders and bakes keep that viewplane
  // instead of Cycles' automatic one, until all four sides are zero again.
  bool explicit_viewplane = false;
  // cbc_host_render_time_limit: seconds, 0 for none (SessionParams::time_limit of every render and bake).
  double time_limit = 0;
  // A render or bake has loaded images into the scene's image manager (cbc_host_texture_limit, PROVISIONAL): Cycles does
  // not load an image again once it has loaded it, so the texture limit is fixed from then on.
  bool images_loaded = false;
  // CBCHostConfig.flags (PROVISIONAL), fixed at cbc_host_create.
  uint32_t flags = 0;
  // CBC_HOST_PROGRESSIVE_RESULTS: film_events is set for the duration of a cbc_host_render (never a bake). A display
  // update of Cycles' session (HostOutput::update_render_tile) leaves the Combined film so far in film, pending, and the
  // next progress event on the same thread sends it as CBC_EVENT_FILM (progress_event); cbc_host_render sends a film
  // still pending when the session has finished. Guarded by event_mutex.
  bool film_events = false, film_pending = false;
  std::vector<float> film;
  std::thread::id film_thread;
  // A progressive host's tiled render (cbc_host_tile_size): the film so far, with the tiles rendered before the one an
  // update holds. Guarded by event_mutex.
  std::vector<float> tiled_film;
  // cbc_host_tile_size (PROVISIONAL): Cycles' tile size of later renders and bakes, 0 for one tile. A tiled render's
  // finished tiles stay in the in-memory tile store of the overlay's path tracer; the session names the full frame
  // through its full buffer callback (tile_frame: set on the session thread, guarded by tile_mutex) and the render reads
  // it back (read_back_tiles). For the device report: the tiles the store kept for this host's renders and bakes, and the
  // frames read back.
  uint32_t tile_size = 0;
  std::mutex tile_mutex;
  std::string tile_frame;
  uint64_t tiles_kept = 0, frames_read = 0;
  void fit_viewplane() {
    Camera &camera = *session->scene->camera;
    const BoundBox2D plane = camera.viewplane;
    if (explicit_viewplane && (plane.left != 0.0f || plane.right != 0.0f || plane.bottom != 0.0f || plane.top != 0.0f)) {
      require(plane.left < plane.right && plane.bottom < plane.top,
              "Camera viewplane needs left < right and bottom < top (all four zero restores the automatic viewplane)");
      return;
    }
    explicit_viewplane = false; camera.compute_auto_viewplane();
  }
  void emit(uint32_t kind, uint64_t done, double elapsed, const std::string &message, const std::vector<float> *pixels = nullptr) {
    if (!callback || closing.load()) return;
    CBCEvent event{sizeof(CBCEvent), kind, done, requested_samples, elapsed, message.c_str(), pixels ? pixels->data() : nullptr,
                   pixels ? uint32_t(result.width) : 0u, pixels ? uint32_t(result.height) : 0u};
    const char *previous = in_callback; in_callback = "an event callback";
    int stop = 0;
    try { stop = callback(user_data, &event); } catch (...) { stop = 1; }
    in_callback = previous;
    if (stop) request_cancel();
  }
  // Reads the in-progress Combined film of a display update into a full film (a camera border's region at its place, as
  // HostOutput::write_render_tile places it) and leaves it pending for the next progress event (CBC_HOST_PROGRESSIVE_RESULTS).
  bool film_update(const OutputDriver::Tile &tile) {
    {
      std::lock_guard lock(event_mutex);
      if (!film_events || !callback || cancellation.load() || closing.load()) return false;
    }
    // A tiled render (cbc_host_tile_size, PROVISIONAL): an update holds the tile being rendered, which goes to its place in
    // the film (in the camera border's region); the film so far keeps the tiles rendered before it.
    const int area_width = result.region_width ? result.region_width : result.width;
    const int area_height = result.region_width ? result.region_height : result.height;
    if (tile.offset.x != 0 || tile.offset.y != 0 || tile.size.x != area_width || tile.size.y != area_height) {
      if (tile.offset.x < 0 || tile.offset.y < 0 || tile.size.x <= 0 || tile.size.y <= 0 || tile.offset.x + tile.size.x > area_width ||
          tile.offset.y + tile.size.y > area_height) return false;
      std::vector<float> region(size_t(tile.size.x) * tile.size.y * 4);
      if (!tile.get_pass_pixels("Combined", 4, region.data())) return false;
      std::lock_guard lock(event_mutex);
      if (tiled_film.empty()) tiled_film.assign(size_t(result.width) * result.height * 4, 0.0f);
      const size_t row = size_t(tile.size.x) * 4;
      for (int y = 0; y < tile.size.y; ++y)
        std::copy_n(region.data() + y * row, row,
                    tiled_film.data() + (size_t(result.region_y + tile.offset.y + y) * result.width + result.region_x + tile.offset.x) * 4);
      film = tiled_film; film_pending = true; film_thread = std::this_thread::get_id();
      return true;
    }
    std::vector<float> pixels(size_t(result.width) * result.height * 4, 0.0f);
    if (result.region_width) {
      if (tile.offset.x != 0 || tile.offset.y != 0 || tile.size.x != result.region_width || tile.size.y != result.region_height) return false;
      std::vector<float> region(size_t(result.region_width) * result.region_height * 4);
      if (!tile.get_pass_pixels("Combined", 4, region.data())) return false;
      const size_t row = size_t(result.region_width) * 4;
      for (int y = 0; y < result.region_height; ++y)
        std::copy_n(region.data() + y * row, row, pixels.data() + (size_t(result.region_y + y) * result.width + result.region_x) * 4);
    } else {
      if (tile.offset.x != 0 || tile.offset.y != 0 || tile.size.x != result.width || tile.size.y != result.height) return false;
      if (!tile.get_pass_pixels("Combined", 4, pixels.data())) return false;
    }
    std::lock_guard lock(event_mutex);
    film.swap(pixels); film_pending = true; film_thread = std::this_thread::get_id();
    return true;
  }
  // Sends a pending film (event_mutex held). After a cancel request no film is sent.
  void emit_film(double elapsed, const std::string &status) {
    film_pending = false;
    if (!film_events || cancellation.load() || !session) return;
    emit(CBC_EVENT_FILM, uint64_t(std::max<int64_t>(session->progress.get_current_sample(), 0)), elapsed, status, &film);
  }
  void progress_event() {
    if (!callback || !rendering.load() || closing.load() || !session) return;
    std::lock_guard lock(event_mutex);
    auto &progress = session->progress;
    std::string status, substatus; progress.get_status(status, substatus);
    // The status and its substatus; a tiled render's full-frame steps have only a substatus (PathTrace::
    // process_full_buffer_from_disk: "Denoising", "Finishing" under an empty layer name).
    if (!substatus.empty()) status = status.empty() ? substatus : status + ": " + substatus;
    double total = 0, render = 0; progress.get_time(total, render);
    // Cycles counts a batch's samples when it schedules the batch (RenderScheduler), and a batch that a cancel request
    // stops is counted in full although most of it never ran. So once a cancel has been requested the count stays at
    // the last one sent before the request (0 if none): no progress event follows, and status events carry it.
    const bool cancelled = cancellation.load();
    const int64_t sample = cancelled ? std::max<int64_t>(last_sample, 0) : progress.get_current_sample();
    if (status != last_status) { last_status = status; emit(CBC_EVENT_STATUS, uint64_t(std::max<int64_t>(sample, 0)), total, status); }
    if (!cancelled && sample != last_sample) { last_sample = sample; emit(CBC_EVENT_PROGRESS, uint64_t(std::max<int64_t>(sample, 0)), total, status); }
    // The thread of the display update: its progress event comes from progress_update_if_needed, which runs right after
    // update_display once Progress::add_samples has counted the samples now in the film.
    if (film_pending && film_thread == std::this_thread::get_id()) emit_film(total, status);
  }
  Host() { live_hosts.fetch_add(1); }
  ~Host() {
    if (session) {
      session->cancel(true);
      bakes.clear();
      splat_radii.clear();
      displaced.clear();
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
    if (entry.pass_mode_edit) {
      entry.pass_mode_edit = false;
      if (auto pass = dynamic_cast<Pass *>(entry.node)) entry.requested_pass_mode = int(pass->get_mode());
    }
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
      for (auto &[node, binding] : graph.images) {
        ImageParams params;
        if (auto image=dynamic_cast<ImageTextureNode *>(node)) params=image->image_params();
        else if (auto image=dynamic_cast<EnvironmentTextureNode *>(node)) params=image->image_params();
        else throw Failure(CBC_FAILED,"Unknown bound texture type");
        // Caller pixels, or a tiled image whose reader supplies them (cbc_host_image_create_tiled, PROVISIONAL).
        const auto loader = [](const std::shared_ptr<const MemoryImageData> &pixels, int tile) -> unique_ptr<ImageLoader> {
          if (pixels->tiled) return std::make_unique<TiledImageLoader>(pixels, tile);
          return std::make_unique<MemoryImageLoader>(pixels, tile);
        };
        if (!udim_binding(binding)) {
          node->handle=session->scene->image_manager->add_image(loader(binding.front().second,0),params,false);
          continue;
        }
        // UDIM tiles: Cycles' multi-loader add_image keys each tile's image by its loader's tile number.
        vector<unique_ptr<ImageLoader>> loaders;
        for (auto &[tile, pixels] : binding) loaders.push_back(loader(pixels, tile));
        node->handle=session->scene->image_manager->add_image(std::move(loaders),params);
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
      recompiled.insert(shader);
      graph.dirty = false;
    }
  }
  // After a render or bake (tiled images, PROVISIONAL). A tiled image for which a read did not deliver (a failed read, or
  // one a cancel stopped) has a tile Cycles marked failed and never loads again: it gets a new generation, and every
  // texture bound to it adds it again at the next render, so Cycles loads it anew (TiledImageSource::undelivered).
  void retry_undelivered_tiles() {
    std::vector<const TiledImageSource *> stale;
    {
      std::lock_guard lock(tiled_reads->mutex);
      for (TiledImageSource *source : tiled_reads->sources)
        if (source->undelivered.exchange(false)) { source->generation++; stale.push_back(source); }
    }
    if (stale.empty()) return;
    for (auto &[shader, graph] : graphs) for (auto &[node, binding] : graph->images)
      for (auto &[tile, data] : binding)
        if (data->tiled && std::find(stale.begin(), stale.end(), data->tiled.get()) != stale.end()) { node->handle.clear(); graph->dirty = true; }
  }
  // A render or bake whose tiled image read failed fails with the reader's failure, never with the texture's Missing
  // colour in the tile (kit rule 5).
  void render_tiles_delivered() {
    std::string failure;
    { std::lock_guard lock(tiled_reads->mutex); if (tiled_reads->failed) failure = tiled_reads->failure; }
    require(failure.empty(), failure.c_str(), CBC_FAILED);
  }
  // Tiled rendering (cbc_host_tile_size, PROVISIONAL). A render or bake that completed in several tiles reads its full
  // frame back from memory here, on the calling thread (the session thread has finished), as Cycles intends for a tiled
  // render (PathTrace::finalize_full_buffer_on_disk): Session::process_full_buffer_from_disk denoises the whole film when
  // denoising and writes it, every pass included, to the output driver once. True when a frame was read back.
  bool read_back_tiles() {
    std::string frame;
    { std::lock_guard lock(tile_mutex); frame.swap(tile_frame); }
    if (frame.empty()) {
      require(!session->multiple_tiles(), "Tiled render produced no full frame", CBC_FAILED);
      return false;
    }
    tiles_kept += uint64_t(codebrix_memory_tiles(session->path_tracer(), false));
    session->process_full_buffer_from_disk(frame);
    if (!session->progress.get_error()) frames_read++;
    return true;
  }
  // After every render or bake: nothing stays in the tile store (a cancelled or failed render's frame, finished or not,
  // is dropped unread).
  void drop_tiles() {
    { std::lock_guard lock(tile_mutex); tile_frame.clear(); }
    if (session) tiles_kept += uint64_t(codebrix_memory_tiles(session->path_tracer(), true));
  }
};
// Spans the end of a render or bake: drops what a tiled render left in the tile store (Host::drop_tiles).
struct DropTiles {
  Host &host;
  ~DropTiles() { host.drop_tiles(); }
};
// Spans the session's work of a render or bake, from before session.start() (a thread that cannot start throws while
// nothing renders) to after session.wait(). Cycles' session reads the host's cancel flag (the Progress cancel callback)
// only between batches of samples, and a headless session sizes the batch after its first one-sample batch to about
// 30 seconds of samples (RenderScheduler), so a cancel request would otherwise wait for that batch. On a request this
// thread stops the running batch (HostSession::cancel_path_trace); the session thread then sees the flag and ends the
// render. PathTrace::cancel clears its request once the batch has stopped, so a request that lands just before the
// session thread starts a batch would be lost: the watch repeats it every 5 ms until the session has finished. Until a
// request it only waits, so a render that is not cancelled runs exactly as without it.
struct CancelWatch {
  Host &host;
  bool done = false;  // Guarded by host.cancel_mutex.
  std::thread watcher;
  explicit CancelWatch(Host &h) : host(h), watcher([this] { run(); }) {}
  ~CancelWatch() {
    { std::lock_guard lock(host.cancel_mutex); done = true; }
    host.cancel_signal.notify_all();
    watcher.join();
  }
  void run() {
    std::unique_lock lock(host.cancel_mutex);
    host.cancel_signal.wait(lock, [this] { return done || host.cancellation.load() || host.closing.load(); });
    while (!done) {
      lock.unlock();
      host.session->cancel_path_trace();
      lock.lock();
      host.cancel_signal.wait_for(lock, std::chrono::milliseconds(5), [this] { return done; });
    }
  }
};
std::mutex hosts_mutex;
std::map<CBCHost, std::shared_ptr<Host>> hosts;
std::once_flag exit_hook;
// Registers every Cycles node type, once, before a host or a capability report can reach a node type or
// Pass::get_type_enum directly: that enum is a function-local static the pinned code fills on first use
// without a lock, and two threads filling it corrupt the heap. NodeType's own initialization is locked.
void register_node_types_once() {
  static std::once_flag once;
  std::call_once(once, [] { (void)NodeType::type_names(); });
}
void shutdown_hosts() noexcept {
  std::map<CBCHost, std::shared_ptr<Host>> remaining;
  { std::lock_guard lock(hosts_mutex); remaining.swap(hosts);
    for (auto &[id, host] : remaining) host->request_cancel(true); }
  for (auto &[id, host] : remaining) {
    std::lock_guard lock(host->operation);
    if (host->session) { host->session->cancel(true); host->bakes.clear(); host->splat_radii.clear(); host->displaced.clear(); host->nodes.clear(); host->graphs.clear(); host->images.clear(); host->session.reset(); }
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
// Integer, boolean and enumeration sockets the host itself sets, or whose value never reaches the returned film or
// passes: a set that would change one is refused with this message instead of being lost (a set to the current
// value is accepted).
const char *fixed_socket(const Node &node, const SocketType &input) {
  if ((dynamic_cast<const Camera *>(&node) && (input.name == "full_width" || input.name == "full_height")) ||
      (dynamic_cast<const Integrator *>(&node) && input.name == "aa_samples"))
    return "This socket is set by every render from the render configuration (width, height, samples) and cannot be set";
  if (dynamic_cast<const Film *>(&node) && (input.name == "display_pass" || input.name == "show_active_pixels"))
    return "This film socket is a viewport display setting with no effect on the returned film or passes; read a pass through a pass node instead";
  // Session::update_scene sets it on every render: true for a render in several tiles (cbc_host_tile_size), whose film
  // the sample count pass normalises per pixel.
  if (dynamic_cast<const Film *>(&node) && input.name == "use_sample_count")
    return "Film use_sample_count is Cycles' session state, set by every render; add a pass of type sample_count to read the sample count";
  return nullptr;
}
// The Image Texture node's tiles socket (PROVISIONAL refusal): Cycles' list of the UDIM tiles whose image files it would
// read, by the <UDIM> token of the node's file name (ImageTextureNode, scene/shader_nodes.cpp). This library reads no
// image files, so the list has no effect on bound pixels, a plain binding or a tiles binding: a set that would change it
// is refused with this message instead of being ignored (a set to the current value, the default empty list included,
// is accepted).
const char *file_tiles_socket(const Node &node, const SocketType &input) {
  if (dynamic_cast<const ImageTextureNode *>(&node) && input.name == "tiles")
    return "The Image Texture node's tiles socket is Cycles' list of UDIM tiles read from image files, which this library "
           "does not read; bind images as UDIM tiles with cbc_host_image_bind_tiles";
  return nullptr;
}
// The node name of objects and shaders (Node::name, not a socket: their
// cryptomatte object and material ids hash it) is the string property "name".
bool node_name_property(Entry &entry, const char *name) {
  return name && std::strcmp(name, "name") == 0 && !entry.node->type->find_input(ustring("name")) &&
         (dynamic_cast<Object *>(entry.node) || dynamic_cast<Shader *>(entry.node));
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
void host_pipeline_cache_config(bool off, const char *directory);
void host_pipeline_cache_allow_default();
std::string webgpu_pipeline_cache_report_json(Device *device);
std::string webgpu_device_report_json(Device *device);
std::string webgpu_adapter_report_json(Device *device);
// The oracle arithmetic the generated WGSL adapters reproduce (native/cycles/camera.cpp).
std::string wgsl_arithmetic();
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
extern "C" int cbc_host_pipeline_cache_config(const CBCPipelineCacheConfig *config, const char *directory) {
  return boundary([&] {
    require(config && config->struct_size == sizeof(*config) && config->abi_version == CBC_HOST_ABI, "Invalid pipeline cache configuration");
    require(config->flags == 0 && std::all_of(std::begin(config->reserved), std::end(config->reserved), [](auto v) { return v == 0; }), "Unknown configuration flags");
    require(config->mode <= CBC_PIPELINE_CACHE_OFF, "Invalid pipeline cache mode");
#ifdef WITH_WEBGPU
    ccl::host_pipeline_cache_config(config->mode == CBC_PIPELINE_CACHE_OFF, directory ? directory : "");
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
      // Which CPU oracle's rounding the WGSL reproduces: oracle films record it and compare.py gates
      // the film against the CPU gold standard of that ISA family.
      out << ",\"wgslArithmetic\":\"" << wgsl_arithmetic() << '"';
      out << ",\"pipelineCache\":" << webgpu_pipeline_cache_report_json(device);
    } else
#endif
    out << (device->info.type == DEVICE_METAL ? "\"metal\"" : host.backend == CBC_HOST_CPU ? "\"cpu-embree\"" : "\"cpu\"") << ",\"pipelines\":null,\"pipelineCache\":{\"active\":false,\"reason\":\"backend\",\"directory\":null}";
    // Tiled rendering (cbc_host_tile_size, PROVISIONAL): the tile size, the tiles the library kept in memory for this host's
    // renders and bakes, the full frames it read back, and the tiles it holds now (0 outside a render).
    out << ",\"tiledRendering\":{\"tileSize\":" << host.tile_size << ",\"tilesKeptInMemory\":" << host.tiles_kept
        << ",\"framesReadBack\":" << host.frames_read << ",\"tilesHeld\":" << codebrix_memory_tiles(host.session->path_tracer(), false) << '}';
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
    // CBCHostConfig is 40 bytes with the trailing flags (PROVISIONAL); the 32 bytes before them are accepted as no flags,
    // and nothing past struct_size is read.
    static_assert(sizeof(CBCHostConfig) == 40 && offsetof(CBCHostConfig, flags) == 32, "CBCHostConfig layout");
    require(config && out && (config->struct_size == offsetof(CBCHostConfig, flags) || config->struct_size == sizeof(*config)) &&
            config->abi_version == CBC_HOST_ABI, "Invalid host configuration");
    require(std::all_of(std::begin(config->reserved), std::end(config->reserved), [](auto v) { return v == 0; }), "Unknown configuration flags");
    const uint32_t flags = config->struct_size == sizeof(*config) ? config->flags : 0;
    require((flags & ~uint32_t(CBC_HOST_PROGRESSIVE_RESULTS)) == 0 && (config->struct_size != sizeof(*config) || config->reserved2 == 0),
            "Unknown configuration flags");
    require(config->threads <= 1024, "Invalid thread count");
    require(config->backend == CBC_HOST_WEBGPU || config->backend == CBC_HOST_CPU_REFERENCE || config->backend == CBC_HOST_METAL ||
            config->backend == CBC_HOST_GPU || config->backend == CBC_HOST_CPU, "Explicit backend selection required");
    const uint32_t backend = config->backend == CBC_HOST_GPU ? gpu_backend() : config->backend;
    require(backend != 0, "This binary has no GPU backend", CBC_UNSUPPORTED);
#ifdef __APPLE__
    require(supported_macos(), "This library requires macOS 15.0 or later", CBC_UNSUPPORTED);
    initialize_paths();
#endif
#ifndef WITH_METAL
    require(backend != CBC_HOST_METAL, "The Metal backend exists only in Apple Silicon builds", CBC_UNSUPPORTED);
#endif
#ifndef WITH_EMBREE
    require(backend != CBC_HOST_CPU, "The Embree CPU backend exists only in builds with Embree", CBC_UNSUPPORTED);
#endif
#ifdef WITH_WEBGPU
    ccl::host_pipeline_cache_allow_default();
#endif
    register_node_types_once();
    std::lock_guard configuration_lock(configuration_mutex);
    auto host = std::make_shared<Host>();
    auto type = backend == CBC_HOST_WEBGPU ? DEVICE_WEBGPU : backend == CBC_HOST_METAL ? DEVICE_METAL : DEVICE_CPU;
    host->backend = backend; host->flags = flags;
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
    // Progressive results: Cycles schedules display updates for a session that is not headless (RenderScheduler); it
    // stays a background session, whose session.wait() returns when the render is done.
    params.headless = !(flags & CBC_HOST_PROGRESSIVE_RESULTS); params.background = true; params.threads = int(config->threads);
    params.use_auto_tile = false; params.use_resolution_divider = false;
    // Every backend builds Cycles' BVH2 (the CPU reference and the exact WGSL traversal agree on it), except the
    // Embree CPU backend, which leaves the layout to the CPU device (Embree).
    SceneParams scene; scene.bvh_layout = backend == CBC_HOST_CPU ? BVH_LAYOUT_AUTO : BVH_LAYOUT_BVH2; scene.bvh_type = BVH_TYPE_STATIC;
    // Unaligned BVH2 nodes for curves, as the pinned default: the BVH2 traversal of the CPU reference, of the Metal
    // kernels and of the WGSL device all read them; only scenes with curves can have them (scene/geometry_bvh.cpp).
    scene.use_bvh_unaligned_nodes = true;
    // Cycles' texture cache for tiled images (cbc_host_image_create_tiled, PROVISIONAL): the image manager copies the
    // setting once, at construction (scene/image.cpp), so it is on for every host. Only an image whose loader reports tiles
    // and mip levels takes the cached path (ImageManager::device_load_image); the host's other images do not, and texture
    // file names are refused in this product (or, in a development build that reads files, look for an existing .tx file
    // beside the image, as stock Cycles does; no .tx file is made: auto_texture_cache stays off).
    scene.use_texture_cache = true; scene.auto_texture_cache = false;
    host->session = std::make_unique<HostSession>(params, scene);
    require(!host->session->progress.get_error(), host->session->progress.get_error_message().c_str(), CBC_FAILED);
    require(host->session->device->info.type == type, "Device selection changed unexpectedly", CBC_FAILED);
    Host *raw = host.get();
    host->tiled_reads->cancel_render = [raw] { raw->request_cancel(); };
    auto output = std::make_unique<HostOutput>(host->result);
    if (flags & CBC_HOST_PROGRESSIVE_RESULTS) output->film_update = [raw](const OutputDriver::Tile &tile) { return raw->film_update(tile); };
    host->session->set_output_driver(std::move(output));
    host->session->progress.set_cancel_callback([raw] {
      if (raw->cancellation.load() || raw->closing.load()) raw->session->progress.set_cancel("Cancelled by caller");
    });
    host->session->progress.set_update_callback([raw] { raw->progress_event(); });
    // Tiled rendering (cbc_host_tile_size): the name of a tiled render's full frame in the in-memory tile store.
    host->session->full_buffer_written_cb = [raw](string_view name) {
      std::lock_guard lock(raw->tile_mutex); raw->tile_frame.assign(name.data(), name.size());
    };
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
      host = it->second; host->request_cancel(true); hosts.erase(it); }
    std::lock_guard lock(host->operation);
    host->session->cancel(true);
    host->bakes.clear(); host->splat_radii.clear(); host->displaced.clear(); host->nodes.clear(); host->graphs.clear(); host->images.clear(); host->session.reset();
  });
}
extern "C" int cbc_host_cancel(CBCHost handle) {
  return boundary([&] { auto host = lookup(handle); require(host->rendering, "No render is active", CBC_NOT_READY); host->request_cancel(); });
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
    else if (std::strcmp(name, "scene_attributes") == 0) node = scene.scene_attribute;
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
    const bool same = source->type() == target->type() || (SocketType::is_float3(source->type()) && SocketType::is_float3(target->type()));
    require(same || source->type() != SocketType::CLOSURE, "A shader output can link to a shader input only", CBC_UNSUPPORTED);
    std::set<ShaderNode *> visited; require(!reaches(second, first, visited), "Shader link would create a cycle");
    if (target->link) a.graph->authoring->disconnect(target);
    if (same) a.graph->authoring->connect(source, target);
    else {
      // Different value types (a float into a colour, a colour into a shader input, ...): the authoring graph keeps the
      // link as made, and building the render graph (prepare_graphs) lets ShaderGraph::connect insert Cycles' own
      // conversion node, as Cycles converts every such link.
      target->link = source; source->links.push_back(target);
    }
    a.graph->dirty = true;
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
        array<int> copy; copy.resize(size_t(count)); for (size_t i=0;i<count;++i) copy[i]=int(values[i]);
        if (const char *why = file_tiles_socket(node, input)) require(copy == node.get_int_array(input), why);
        node.set(input,copy);
      }
      host.changed(entry); return;
    }
    require(count == 1, "Scalar integer socket required"); auto value = values[0];
    if (const char *why = fixed_socket(node, input))
      require(value == (input.type == SocketType::BOOLEAN ? int64_t(node.get_bool(input)) : int64_t(node.get_int(input))), why);
    if (input.type == SocketType::BOOLEAN) { require(value == 0 || value == 1, "Boolean must be zero or one"); node.set(input, value != 0); }
    else if (input.type == SocketType::INT || input.type == SocketType::ENUM) { require(value >= INT_MIN && value <= INT_MAX, "Integer exceeds socket range");
      if (input.type == SocketType::ENUM) {
        // Film cryptomatte_passes is a CryptomatteType bit set (the kernel reads the bits):
        // any union of its enumerated flags is accepted, as Blender sets it.
        auto flag_set = [&] {
          if (!dynamic_cast<Film *>(&node) || input.name != "cryptomatte_passes" || !input.enum_values || value < 0) return false;
          int64_t known = 0; for (const auto &[label, flag] : *input.enum_values) known |= flag;
          return (value & ~known) == 0;
        };
        require(input.enum_values && (input.enum_values->exists(int(value)) || flag_set()), "Unknown enumeration value");
      }
      node.set(input, int(value)); }
    else if (input.type == SocketType::UINT) { require(value >= 0 && uint64_t(value) <= UINT_MAX, "Unsigned socket out of range"); node.set(input, uint(value)); }
    else if (input.type == SocketType::UINT64) { require(value >= 0, "Unsigned socket out of range"); node.set(input, uint64_t(value)); }
    else throw Failure(CBC_INVALID, "Socket does not contain integer values");
    host.changed(entry);
  });
}
extern "C" int cbc_host_set_string(CBCHost handle, CBCNode id, const char *name, const char *value) {
  return edit(handle, [&](Host &host) {
    require(value, "Missing string value"); auto &entry = host.entry(id);
    if (node_name_property(entry, name)) {
      before_edit(host, entry, name); entry.node->name = ustring(value);
      // ObjectManager rehashes the cryptomatte ids when the asset name is modified.
      if (auto object = dynamic_cast<Object *>(entry.node)) object->tag_asset_name_modified();
      host.changed(entry); return;
    }
    auto &input = socket(entry, name);
    stored_property(input); before_edit(host, entry, name);
#ifdef CBC_HOST_NO_IMAGE_FILES
    // This build contains no image file readers; callers bind decoded pixels.
    require(!*value || input.name != "filename" || !dynamic_cast<ImageSlotTextureNode *>(entry.node),
            "Image files are not read by this library; bind decoded pixels with cbc_host_image_bind", CBC_UNSUPPORTED);
#endif
    // Native automatic colorspace is the default-constructed empty ustring.
    // OIIO distinguishes its null interned pointer from ustring("").
    if (input.type == SocketType::STRING) entry.node->set(input, *value ? ustring(value) : ustring());
    else if (input.type == SocketType::ENUM) { require(input.enum_values && input.enum_values->exists(ustring(value)), "Unknown enumeration label");
      if (const char *why = fixed_socket(*entry.node, input)) require((*input.enum_values)[value] == entry.node->get_int(input), why);
      entry.node->set(input, (*input.enum_values)[value]); }
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
    // Cycles skips the shaders no node references when it compiles them; Shader::tag_used has a shader that becomes
    // referenced after a render compiled for the next one, as Cycles' own integrations call it when they assign one.
    for (Node *node : nodes) if (auto shader = dynamic_cast<Shader *>(node)) shader->tag_used(host.session->scene.get());
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
    // Triangles replace subdivision faces a cbc_host_subd_mesh call left (their attributes go with them).
    if (mesh->get_num_subd_faces()) { mesh->resize_subd_faces(0, 0); mesh->subd_attributes.clear(); }
    mesh->resize_mesh(int(vertices), int(triangles));
    for (size_t i = 0; i < vertices; ++i) mesh->get_position_for_write()[i] = make_float3(xyz[3*i],xyz[3*i+1],xyz[3*i+2]);
    for (size_t i = 0; i < triangles * 3; ++i) mesh->get_triangles()[i] = int(indices[i]);
    std::ranges::fill(mesh->get_shader(), 0); std::ranges::fill(mesh->get_smooth(), false);
    mesh->tag_triangles_modified(); host.changed(entry);
  });
}
// Subdivision geometry (ABI.md "Geometry"): the control cage's positions and polygon faces, each face's corner count and
// its corners in order. Mesh::resize_subd_faces sets the face count Cycles' tessellation reads (Mesh::num_subd_faces,
// which the subd_* sockets alone leave at zero) and the cage positions are the subdivision position attribute
// (subd_attributes ATTR_STD_POSITION, which Mesh::tessellate reads); the render tessellates them by subdivision_type.
extern "C" int cbc_host_subd_mesh(CBCHost handle, CBCNode id, const float *xyz, uint64_t vertices, const uint32_t *face_corners,
                                  uint64_t faces, const uint32_t *corners, uint64_t corner_count) {
  return edit(handle, [&](Host &host) {
    require(vertices <= INT_MAX / 3 && faces <= INT_MAX && corner_count <= INT_MAX && (vertices == 0 || xyz) && (faces == 0 || face_corners) &&
            (corner_count == 0 || corners), "Invalid subdivision mesh dimensions");
    auto &entry = host.entry(id); auto mesh = dynamic_cast<Mesh *>(entry.node); require(mesh && !mesh->is_volume(), "Handle is not a mesh");
    for (uint64_t i = 0; i < vertices * 3; ++i) require(std::isfinite(xyz[i]), "Mesh position is nonfinite");
    uint64_t total = 0;
    for (uint64_t f = 0; f < faces; ++f) { require(face_corners[f] >= 3, "A subdivision face needs at least three corners"); total += face_corners[f]; }
    require(total == corner_count, "Face corner counts do not add up to the corner array");
    for (uint64_t i = 0; i < corner_count; ++i) require(corners[i] < vertices, "Face corner is outside the vertex array");
    before_edit(host, entry, nullptr);
    // A cage set after Cycles diced the mesh starts from no diced vertices: Mesh::clear_non_sockets resets the count of
    // vertices the dicing added, which Mesh::get_num_subd_base_verts subtracts and the cage attributes are sized by
    // (Attribute::element_size), as Mesh::clear does for Cycles' own hosts; the old count would size them wrongly.
    mesh->clear_non_sockets();
    mesh->subd_attributes.clear();
    mesh->resize_mesh(int(vertices), 0);
    for (size_t i = 0; i < vertices; ++i) mesh->get_position_for_write()[i] = make_float3(xyz[3*i],xyz[3*i+1],xyz[3*i+2]);
    mesh->resize_subd_faces(int(faces), int(corner_count));
    int start = 0, ptex = 0;
    for (size_t f = 0; f < faces; ++f) {
      const int n = int(face_corners[f]);
      mesh->get_subd_start_corner()[f] = start; mesh->get_subd_num_corners()[f] = n;
      mesh->get_subd_shader()[f] = 0; mesh->get_subd_smooth()[f] = false; mesh->get_subd_ptex_offset()[f] = ptex;
      start += n; ptex += n == 4 ? 1 : n;
    }
    for (size_t i = 0; i < corner_count; ++i) mesh->get_subd_face_corners()[i] = int(corners[i]);
    Attribute *cage = mesh->subd_attributes.add(ATTR_STD_POSITION);
    for (size_t i = 0; i < vertices; ++i) cage->data_for_write<packed_float3>()[i] = make_float3(xyz[3*i],xyz[3*i+1],xyz[3*i+2]);
    mesh->tag_triangles_modified(); host.changed(entry);
  });
}
// Curve geometry, the shape of cbc_host_mesh: validated, copied, every curve starts on material 0.
extern "C" int cbc_host_curves(CBCHost handle, CBCNode id, const float *xyz, const float *radius, uint64_t key_count, const uint32_t *first_key, uint64_t curve_count) {
  return edit(handle, [&](Host &host) {
    require(key_count <= INT_MAX / 3 && curve_count <= INT_MAX && (key_count == 0 || (xyz && radius)) && (curve_count == 0 || first_key), "Invalid curve dimensions");
    require((curve_count == 0) == (key_count == 0), "Curve keys and curves must both be empty or both be present");
    auto &entry = host.entry(id); auto hair = dynamic_cast<Hair *>(entry.node); require(hair, "Handle is not a hair geometry");
    for (uint64_t i = 0; i < key_count * 3; ++i) require(std::isfinite(xyz[i]), "Curve key position is nonfinite");
    for (uint64_t i = 0; i < key_count; ++i) require(std::isfinite(radius[i]) && radius[i] >= 0.0f, "Curve key radius is nonfinite or negative");
    if (curve_count) require(first_key[0] == 0, "First curve must start at key zero");
    for (uint64_t i = 0; i < curve_count; ++i) {
      const uint64_t end = i + 1 < curve_count ? first_key[i + 1] : key_count;
      require(first_key[i] < end && end <= key_count, "Invalid curve key range");
    }
    before_edit(host, entry, nullptr);
    hair->resize_curves(int(curve_count), int(key_count));
    auto positions = hair->attributes.find(ATTR_STD_POSITION)->data_for_write<packed_float3>();
    auto radii = hair->attributes.find(ATTR_STD_RADIUS)->data_for_write<float>();
    for (size_t i = 0; i < key_count; ++i) { positions[i] = make_float3(xyz[3*i],xyz[3*i+1],xyz[3*i+2]); radii[i] = radius[i]; }
    for (size_t i = 0; i < curve_count; ++i) { hair->get_curve_first_key()[i] = int(first_key[i]); hair->get_curve_shader()[i] = 0; }
    hair->tag_curve_first_key_modified(); hair->tag_curve_shader_modified(); host.changed(entry);
  });
}
// Curve deformation motion, the shape of cbc_host_motion_positions: time-ordered keys, the center step the current keys
// bit for bit; radii per step, or the center radii at every step (radius null).
extern "C" int cbc_host_curve_motion(CBCHost handle, CBCNode id, uint32_t steps, const float *xyz, const float *radius, uint64_t count) {
  return edit(handle, [&](Host &host) {
    auto &entry = host.entry(id); auto hair = dynamic_cast<Hair *>(entry.node); require(hair, "Handle is not a hair geometry");
    // A hair geometry baked by an earlier render gets its authored keys back first (host_edits.inc).
    before_edit(host, entry, nullptr);
    require(steps == 1 || (steps >= 3 && steps % 2 == 1 && steps <= Geometry::MAX_MOTION_STEPS), "Motion steps must be 1 or odd from 3 to 129");
    auto position = hair->attributes.find(ATTR_STD_POSITION); auto radii = hair->attributes.find(ATTR_STD_RADIUS);
    require(position && radii, "Hair geometry has no curve keys");
    if (steps == 1) {
      require(count == 0, "Removing motion takes no keys");
      position->remove_motion(); radii->remove_motion();
      hair->set_use_motion_blur(false);
    } else {
      const uint64_t keys = hair->num_keys();
      require(keys && count == uint64_t(steps) * keys && xyz, "Expected steps * key_count keys");
      for (uint64_t i = 0; i < count * 3; ++i) require(std::isfinite(xyz[i]), "Motion key position is nonfinite");
      if (radius) for (uint64_t i = 0; i < count; ++i) require(std::isfinite(radius[i]) && radius[i] >= 0.0f, "Motion key radius is nonfinite or negative");
      const uint32_t center = (steps - 1) / 2; const packed_float3 *current = position->data<packed_float3>(); const float *current_radius = radii->data<float>();
      for (uint64_t k = 0; k < keys; ++k) {
        const float *p = xyz + (uint64_t(center) * keys + k) * 3;
        require(std::memcmp(&current[k].x, p, sizeof(float)) == 0 && std::memcmp(&current[k].y, p + 1, sizeof(float)) == 0 &&
                std::memcmp(&current[k].z, p + 2, sizeof(float)) == 0, "Center motion step must equal the curve keys");
        if (radius) require(std::memcmp(&current_radius[k], radius + uint64_t(center) * keys + k, sizeof(float)) == 0, "Center motion step must equal the curve radii");
      }
      hair->set_motion_steps(steps); hair->set_use_motion_blur(true);
      position->remove_motion(); position->add_motion(hair);
      radii->remove_motion(); if (radius) radii->add_motion(hair);
      for (uint32_t step = 0; step < steps; ++step) {
        if (step == center) continue;
        auto out = position->data_for_write<packed_float3>(position->time_step_to_attr_step(int(step), int(steps)));
        for (uint64_t k = 0; k < keys; ++k) { const float *p = xyz + (uint64_t(step) * keys + k) * 3; out[k] = make_float3(p[0], p[1], p[2]); }
        if (radius) std::memcpy(radii->data_for_write<float>(radii->time_step_to_attr_step(int(step), int(steps))), radius + uint64_t(step) * keys, size_t(keys) * sizeof(float));
      }
    }
    hair->tag_curve_first_key_modified(); host.changed(entry);
  });
}
extern "C" int cbc_host_curve_shape(CBCHost handle, CBCNode id, uint32_t shape) {
  return edit(handle, [&](Host &host) {
    static_assert(int(CBC_CURVE_RIBBON) == int(CURVE_RIBBON) && int(CBC_CURVE_THICK) == int(CURVE_THICK) && int(CBC_CURVE_THICK_LINEAR) == int(CURVE_THICK_LINEAR));
    require(shape == CBC_CURVE_RIBBON || shape == CBC_CURVE_THICK || shape == CBC_CURVE_THICK_LINEAR, "Unknown curve shape");
    auto &entry = host.entry(id); auto hair = dynamic_cast<Hair *>(entry.node); require(hair, "Handle is not a hair geometry");
    if (hair->curve_shape == CurveShapeType(shape)) return;
    before_edit(host, entry, nullptr); hair->curve_shape = CurveShapeType(shape); hair->tag_curve_first_key_modified(); host.changed(entry);
  });
}
extern "C" int cbc_host_render_time_limit(CBCHost handle, double seconds) {
  return edit(handle, [&](Host &host) {
    require(std::isfinite(seconds) && seconds >= 0, "Render time limit must be finite and not negative");
    host.time_limit = seconds;
  });
}
extern "C" int cbc_host_curve_subdivisions(CBCHost handle, uint32_t subdivisions) {
  return edit(handle, [&](Host &host) {
    require(subdivisions <= 4, "Curve subdivisions must be 0 to 4 (1 to 16 ribbon sections per segment)");
    auto &scene = *host.session->scene;
    if (scene.params.hair_subdivisions == int(subdivisions)) return;
    scene.params.hair_subdivisions = int(subdivisions);
    // KernelBVH holds the section count: the next render updates it through the geometry update of the curves.
    for (Geometry *geometry : scene.geometry) if (geometry->is_hair()) { geometry->tag_modified(); geometry->tag_update(&scene, false); }
  });
}
// Cycles' texture limit (PROVISIONAL, ABI.md "Images"): SceneParams::texture_limit, which ImageManager::device_load_image
// reads when it loads an image (scene/image.cpp): a full image whose larger side exceeds it is scaled by limit / side,
// rounded down to a power of two (ImageCache::load_full), and box-filtered on the host (util_image_resize_pixels), before
// any device sees it. An image already loaded is not loaded again (need_load), so a different value after a render or
// bake has loaded images would leave those, Cycles' own Sky Texture images among them, at the old size: refused.
extern "C" int cbc_host_texture_limit(CBCHost handle, uint32_t max_size) {
  return edit(handle, [&](Host &host) {
    require(max_size <= uint32_t(INT_MAX), "Texture limit must be 0 (no limit) to INT_MAX pixels");
    auto &scene = *host.session->scene;
    if (scene.params.texture_limit == int(max_size)) return;
    require(!host.images_loaded, "The texture limit is fixed once a render or bake has loaded images (Cycles does not load an image "
            "again); set it before the first render or create a new host", CBC_NOT_READY);
    scene.params.texture_limit = int(max_size);
  });
}
// Cycles' BVH spatial splits (PROVISIONAL, ABI.md "Scene authoring"): SceneParams::use_bvh_spatial_split, which the BVH2
// builder reads for every object BVH (scene/geometry_bvh.cpp; it switches the splits off for the top level, curves and
// motion-blurred objects, bvh/build.cpp) and Embree for its build quality (bvh/embree.cpp); MetalRT does not read it. A
// BVH keeps the parameters it was built with, so a change has every geometry BVH and the scene BVH built again at the
// next render: need_update_bvh_for_offset (with need_update_rebuild) makes GeometryManager::device_update rebuild a
// geometry's BVH without tagging the geometry modified, which would run its true displacement again on displaced
// positions, and GEOMETRY_ADDED, the flag of a geometry added after a render, has GeometryManager::device_update_preprocess
// drop the scene BVH, so it is created again with the new parameters.
extern "C" int cbc_host_bvh_spatial_split(CBCHost handle, uint32_t enabled) {
  return edit(handle, [&](Host &host) {
    require(enabled <= 1, "BVH spatial split must be 0 (off) or 1 (on)");
    auto &scene = *host.session->scene;
    if (scene.params.use_bvh_spatial_split == (enabled != 0)) return;
    scene.params.use_bvh_spatial_split = enabled != 0;
    for (Geometry *geometry : scene.geometry)
      if (!geometry->is_light()) { geometry->need_update_rebuild = true; geometry->need_update_bvh_for_offset = true; }
    scene.geometry_manager->tag_update(&scene, GeometryManager::GEOMETRY_ADDED);
  });
}
// Tiled rendering (PROVISIONAL, ABI.md "Tiled rendering"): SessionParams::use_auto_tile and tile_size of later renders and
// bakes. Session::get_effective_tile_size renders one tile unless the tile's area is smaller than the film's or a film side
// exceeds TileManager::MAX_TILE_SIZE (8192), and TileManager::compute_render_tile_size takes a size up to
// TileManager::IMAGE_TILE_SIZE (128) as given and rounds a larger one up to a multiple of it, so the sizes accepted are
// those Cycles uses unchanged. The finished tiles stay in memory (the overlay's tile store; Host::read_back_tiles).
extern "C" int cbc_host_tile_size(CBCHost handle, uint32_t tile_size) {
  return edit(handle, [&](Host &host) {
    require(tile_size == 0 || (tile_size >= 8 && tile_size <= uint32_t(TileManager::IMAGE_TILE_SIZE)) ||
            (tile_size % uint32_t(TileManager::IMAGE_TILE_SIZE) == 0 && tile_size <= uint32_t(TileManager::MAX_TILE_SIZE)),
            "Tile size must be 0 (one tile), 8 to 128, or a multiple of 128 up to 8192");
    host.tile_size = tile_size;
  });
}
// Point cloud geometry, the shape of cbc_host_curves: validated, copied, every point starts on material 0.
extern "C" int cbc_host_points(CBCHost handle, CBCNode id, const float *xyz, const float *radius, uint64_t count) {
  return edit(handle, [&](Host &host) {
    require(count <= INT_MAX / 3 && (count == 0 || (xyz && radius)), "Invalid point dimensions");
    auto &entry = host.entry(id); auto points = dynamic_cast<PointCloud *>(entry.node); require(points, "Handle is not a point cloud");
    for (uint64_t i = 0; i < count * 3; ++i) require(std::isfinite(xyz[i]), "Point position is nonfinite");
    for (uint64_t i = 0; i < count; ++i) require(std::isfinite(radius[i]) && radius[i] >= 0.0f, "Point radius is nonfinite or negative");
    before_edit(host, entry, nullptr);
    points->attributes.find(ATTR_STD_POSITION)->remove_motion(); points->attributes.find(ATTR_STD_RADIUS)->remove_motion();
    points->set_use_motion_blur(false);
    points->resize(int(count));
    auto positions = points->attributes.find(ATTR_STD_POSITION)->data_for_write<packed_float3>();
    auto radii = points->attributes.find(ATTR_STD_RADIUS)->data_for_write<float>();
    for (size_t i = 0; i < count; ++i) { positions[i] = make_float3(xyz[3*i],xyz[3*i+1],xyz[3*i+2]); radii[i] = radius[i]; points->get_shader()[i] = 0; }
    host.changed(entry);
  });
}
// Deformation motion of a point cloud, the shape of cbc_host_motion_positions with one radius per position.
extern "C" int cbc_host_point_motion(CBCHost handle, CBCNode id, uint32_t steps, const float *xyz, const float *radius, uint64_t count) {
  return edit(handle, [&](Host &host) {
    auto &entry = host.entry(id); auto points = dynamic_cast<PointCloud *>(entry.node); require(points, "Handle is not a point cloud");
    before_edit(host, entry, nullptr);
    require(steps == 1 || (steps >= 3 && steps % 2 == 1 && steps <= Geometry::MAX_MOTION_STEPS), "Motion steps must be 1 or odd from 3 to 129");
    auto position = points->attributes.find(ATTR_STD_POSITION); auto radii = points->attributes.find(ATTR_STD_RADIUS);
    if (steps == 1) {
      require(count == 0, "Removing motion takes no positions");
      position->remove_motion(); radii->remove_motion(); points->set_use_motion_blur(false);
    } else {
      const uint64_t total = points->num_points();
      require(total && count == uint64_t(steps) * total && xyz && radius, "Expected steps * point_count positions and radii");
      for (uint64_t i = 0; i < count * 3; ++i) require(std::isfinite(xyz[i]), "Motion position is nonfinite");
      for (uint64_t i = 0; i < count; ++i) require(std::isfinite(radius[i]) && radius[i] >= 0.0f, "Motion radius is nonfinite or negative");
      const uint32_t center = (steps - 1) / 2; const packed_float3 *current = position->data<packed_float3>(); const float *current_radius = radii->data<float>();
      for (uint64_t v = 0; v < total; ++v) {
        const uint64_t at = uint64_t(center) * total + v; const float *p = xyz + at * 3;
        require(std::memcmp(&current[v].x, p, sizeof(float)) == 0 && std::memcmp(&current[v].y, p + 1, sizeof(float)) == 0 && std::memcmp(&current[v].z, p + 2, sizeof(float)) == 0 &&
                std::memcmp(&current_radius[v], radius + at, sizeof(float)) == 0, "Center motion step must equal the points");
      }
      points->set_motion_steps(steps); points->set_use_motion_blur(true);
      position->remove_motion(); radii->remove_motion(); position->add_motion(points); radii->add_motion(points);
      for (uint32_t step = 0; step < steps; ++step) {
        if (step == center) continue;
        const int attr_step = position->time_step_to_attr_step(int(step), int(steps));
        auto out = position->data_for_write<packed_float3>(attr_step); auto out_radius = radii->data_for_write<float>(attr_step);
        for (uint64_t v = 0; v < total; ++v) { const uint64_t at = uint64_t(step) * total + v; const float *p = xyz + at * 3; out[v] = make_float3(p[0], p[1], p[2]); out_radius[v] = radius[at]; }
      }
    }
    points->tag_position_modified(); points->tag_radius_modified(); host.changed(entry);
  });
}
// The particles of a particle system: what the Particle Info node reads through the object's particle_system and
// particle_index sockets. values holds CBC_PARTICLE_FLOATS floats per particle (cycles_host.h).
extern "C" int cbc_host_particles(CBCHost handle, CBCNode id, const uint32_t *index, const float *values, uint64_t count) {
  return edit(handle, [&](Host &host) {
    require(count <= INT_MAX / CBC_PARTICLE_FLOATS && (count == 0 || (index && values)), "Invalid particle dimensions");
    auto &entry = host.entry(id); auto system = dynamic_cast<ParticleSystem *>(entry.node); require(system, "Handle is not a particle system");
    for (uint64_t i = 0; i < count * CBC_PARTICLE_FLOATS; ++i) require(std::isfinite(values[i]), "Particle value is nonfinite");
    system->particles.resize(size_t(count));
    for (size_t i = 0; i < count; ++i) {
      const float *v = values + i * CBC_PARTICLE_FLOATS; Particle &particle = system->particles[i];
      particle.index = int(index[i]); particle.age = v[0]; particle.lifetime = v[1];
      particle.location = make_float3(v[2], v[3], v[4]); particle.rotation = make_float4(v[5], v[6], v[7], v[8]);
      particle.size = v[9]; particle.velocity = make_float3(v[10], v[11], v[12]); particle.angular_velocity = make_float3(v[13], v[14], v[15]);
    }
    Scene &scene = *host.session->scene;
    system->tag_update(&scene); scene.object_manager->tag_update(&scene, ObjectManager::PARTICLE_MODIFIED); host.changed(entry);
  });
}
// Denoised passes are the default, the noisy version an explicit request.
// Cycles' Film::finalize_passes switches every pass to noisy mode whenever a
// render does not denoise, and nothing switches it back (Blender re-syncs its
// passes before every render). When the integrator asks for denoising, give
// the passes Cycles' Open Image Denoise denoiser writes (integrator/
// denoiser_oidn.cpp: combined, shadow catcher, shadow catcher matte) the mode
// requested: denoised, unless the caller set the pass's mode to noisy. Renders
// that do not denoise are left to Cycles unchanged.
// Without denoising every pass is noisy, so a noisy request duplicates another
// pass of its type; two named passes of one type and mode both get buffers but
// the kernel writes only one. For that render the request's pass is parked
// (type none) and read from its twin: Combined, or the other pass of that type.
struct ParkedPasses {
  Scene &scene; std::vector<std::pair<Pass *, PassType>> parked;
  Pass *added = nullptr;  // a pass the host adds for one render only (render_pass_modes), removed after it
  ~ParkedPasses() {
    for (auto &[pass, type] : parked) pass->set_type(type);
    if (added) scene.delete_node(added);
    if (!parked.empty() || added) scene.film->tag_modified();
  }
};
bool denoisable_pass(PassType type) { return type == PASS_COMBINED || type == PASS_SHADOW_CATCHER || type == PASS_SHADOW_CATCHER_MATTE; }
void render_pass_modes(Host &host, ParkedPasses &parking) {
  Scene &scene = *host.session->scene;
  auto noisy_request = [](const Entry &entry, const Pass *pass) {
    return denoisable_pass(pass->get_type()) && pass->get_lightgroup().empty() &&
           entry.requested_pass_mode == int(PassMode::NOISY);
  };
  if (!scene.integrator->get_use_denoise()) {
    for (auto &result : host.result.passes) {
      for (auto &[id, entry] : host.nodes) {
        auto pass = dynamic_cast<Pass *>(entry.node);
        if (!pass || pass->get_name().string() != result.name || !noisy_request(entry, pass)) continue;
        std::string twin = pass->get_type() == PASS_COMBINED ? "Combined" : "";
        for (auto &[other_id, other] : host.nodes) {
          auto candidate = dynamic_cast<Pass *>(other.node);
          if (twin.empty() && candidate && candidate != pass && candidate->get_type() == pass->get_type() &&
              !candidate->get_name().empty() && !noisy_request(other, candidate))
            twin = candidate->get_name().string();
        }
        if (twin.empty()) break;  // the only pass of its type: no duplicate
        result.source = twin;
        parking.parked.emplace_back(pass, pass->get_type());
        pass->set_type(PASS_NONE);
        break;
      }
    }
    if (!parking.parked.empty()) scene.film->tag_modified();
    return;
  }
  bool changed = false;
  auto apply = [&](Pass *pass, PassMode mode) {
    if (pass->get_mode() != mode) { pass->set_mode(mode); changed = true; }
  };
  for (Pass *pass : scene.passes)
    if (pass->get_type() == PASS_COMBINED && pass->get_name() == "Combined") apply(pass, PassMode::DENOISED);
  for (auto &[id, entry] : host.nodes) {
    auto pass = dynamic_cast<Pass *>(entry.node);
    if (!pass || !denoisable_pass(pass->get_type())) continue;
    // Light group passes are combined passes no denoiser writes (it denoises the main combined pass): they stay noisy.
    const bool lightgroup = pass->get_type() == PASS_COMBINED && !pass->get_lightgroup().empty();
    apply(pass, lightgroup || noisy_request(entry, pass) ? PassMode::NOISY : PassMode::DENOISED);
  }
  // With light group passes the main noisy combined pass must stay apart from them. Film::update_passes adds that
  // pass unnamed after every pass of the scene, and Film::finalize_passes merges an unnamed pass into the first earlier
  // pass of its type and mode, light group or not; a light group pass would then hold it and the kernel's main
  // combined pass would not exist. So a noisy combined pass without a light group, with a name, goes before the first
  // light group pass: the caller's own noisy request when there is one (moved there), otherwise one the host adds
  // for this render only. Pass order matters only within one component count and type (compare_pass_order).
  const auto is_lightgroup = [](const Pass *pass) { return pass->get_type() == PASS_COMBINED && !pass->get_lightgroup().empty(); };
  size_t first_group = scene.passes.size();
  for (size_t i = 0; i < scene.passes.size(); i++) if (is_lightgroup(scene.passes[i])) { first_group = i; break; }
  if (first_group < scene.passes.size()) {
    size_t noisy = scene.passes.size();
    for (size_t i = 0; i < scene.passes.size(); i++) {
      const Pass *pass = scene.passes[i];
      if (pass->get_type() == PASS_COMBINED && pass->get_lightgroup().empty() && pass->get_mode() == PassMode::NOISY && !pass->get_name().empty() &&
          pass->get_name() != "Combined") { noisy = i; break; }
    }
    if (noisy == scene.passes.size()) {
      parking.added = scene.create_node<Pass>();
      parking.added->set_type(PASS_COMBINED); parking.added->set_mode(PassMode::NOISY); parking.added->set_name(ustring("CodeBrix Noisy Combined"));
      noisy = scene.passes.size() - 1;
    }
    if (noisy > first_group) {
      // Rotate [first_group, noisy] by one so the noisy pass comes first; every other pass keeps its order.
      std::vector<std::unique_ptr<Pass>> moved;
      for (size_t i = first_group; i <= noisy; i++) moved.push_back(scene.passes.steal(i));
      scene.passes.replace(first_group, std::move(moved.back()));
      for (size_t i = 0; i + 1 < moved.size(); i++) scene.passes.replace(first_group + 1 + i, std::move(moved[i]));
    }
    changed = true;
  }
  if (changed) scene.film->tag_modified();
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
    WorldVolumeLight world_light{*host.session->scene}; world_volume_light(host, world_light);
    auto &session = *host.session; auto &scene = *session.scene;
    host.requested_samples = config->samples;
    recover_device(host);
    // Restore displaced meshes Cycles is about to displace again, after the recovery's complete update (host_edits.inc).
    reconcile_displacement(host);
    scene.camera->set_full_width(int(config->width)); scene.camera->set_full_height(int(config->height));
    scene.camera->set_screen_size(int(config->width), int(config->height)); host.fit_viewplane();
    // The camera border (a render region, fractions of the film with a lower-left origin): Cycles renders only that
    // region (BufferParams full_x/full_y inside full_width/full_height, the camera keeping the whole film's raster), and
    // the film returned is the whole film with zero outside the region. None (all zero) or 0..1 renders the whole film.
    const BoundBox2D border = scene.camera->border;
    const bool whole = (border.left == 0 && border.right == 0 && border.bottom == 0 && border.top == 0) ||
                       (border.left == 0 && border.right == 1 && border.bottom == 0 && border.top == 1);
    int region_x = 0, region_y = 0, region_width = int(config->width), region_height = int(config->height);
    if (!whole) {
      require(border.left >= 0 && border.left < border.right && border.right <= 1 && border.bottom >= 0 && border.bottom < border.top && border.top <= 1,
              "Camera border needs 0 <= left < right <= 1 and 0 <= bottom < top <= 1");
      // The region's pixel edges: the border's fractions of the film size, rounded to the nearest pixel edge.
      region_x = int(std::lround(double(border.left) * config->width)); region_y = int(std::lround(double(border.bottom) * config->height));
      region_width = int(std::lround(double(border.right) * config->width)) - region_x;
      region_height = int(std::lround(double(border.top) * config->height)) - region_y;
      require(region_width > 0 && region_height > 0, "Camera border covers no whole pixel of this film");
    }
    // Cycles upscales only in its GPU denoisers; the Open Image Denoise CPU denoiser that the CPU and WebGPU backends
    // use resets the factor to 1 (integrator/denoiser.cpp), so the setting would be lost, and with GPU denoising on the
    // Metal backend the output driver receives the reduced-resolution render, which the film does not upscale. Refused
    // on every backend.
    require(!scene.integrator->get_use_denoise() || scene.integrator->get_denoiser_upscale_factor() == 1.0f,
            "Unsupported feature: a denoiser upscale factor other than 1 (no backend of this library returns an upscaled film)",
            CBC_UNSUPPORTED);
    // Adaptive dicing of subdivision surfaces measures in the dicing camera's pixels: with subdivision faces in the scene
    // the host keeps it equal to the render camera (Cycles' own scene leaves it at its defaults).
    if (std::any_of(scene.geometry.begin(), scene.geometry.end(), [](Geometry *g) {
          auto mesh = dynamic_cast<Mesh *>(g); return mesh && mesh->get_num_subd_faces() && mesh->get_subdivision_type() != Mesh::SUBDIVISION_NONE; }))
      for (const SocketType &input : scene.camera->type->inputs)
        if (!scene.dicing_camera->equals_value(*scene.camera, input)) scene.dicing_camera->set_value(input, *scene.camera, input);
    auto params = session.params; params.samples = int(config->samples);
    // The integrator's sample subset sockets: Session::update_scene copies the session's values over them, so the
    // render takes them from the integrator (they act only with use_sample_subset).
    params.use_sample_subset = scene.integrator->get_use_sample_subset();
    params.sample_subset_offset = scene.integrator->get_sample_subset_offset();
    params.sample_subset_length = scene.integrator->get_sample_subset_length();
    params.time_limit = host.time_limit;
    // Tiled rendering (cbc_host_tile_size, PROVISIONAL): Cycles' tiles for this render; 0 renders one tile.
    params.use_auto_tile = host.tile_size != 0; params.tile_size = host.tile_size ? int(host.tile_size) : SessionParams().tile_size;
    // A new synchronous render owns its sample budget, including decreases.
    // Session's incremental update otherwise preserves a larger prior budget.
    scene.integrator->set_aa_samples(params.samples);
    BufferParams buffers; buffers.width = buffers.full_width = buffers.window_width = int(config->width);
    buffers.height = buffers.full_height = buffers.window_height = int(config->height);
    host.result = Result{}; host.result.width = buffers.width; host.result.height = buffers.height;
    host.result.pixels.assign(size_t(count), -1000.0f); host.cancellation = false; host.tiled_reads->begin();
    if (!whole) {
      buffers.full_x = region_x; buffers.full_y = region_y;
      buffers.width = buffers.window_width = region_width; buffers.height = buffers.window_height = region_height;
      host.result.region_x = region_x; host.result.region_y = region_y;
      host.result.region_width = region_width; host.result.region_height = region_height;
      std::fill(host.result.pixels.begin(), host.result.pixels.end(), 0.0f);
    }
    // Caller-created passes with a name are captured for cbc_host_pass_floats,
    // light group passes (PASS_COMBINED with a light group) included.
    for (auto &[id, entry] : host.nodes) if (auto pass = dynamic_cast<Pass *>(entry.node)) {
      const std::string name = pass->get_name().string();
      // A plain combined pass is captured only when the caller asked for its
      // noisy version (mode noisy); otherwise it would duplicate Combined.
      if (name.empty() || name == "Combined" || (pass->get_type() == PASS_COMBINED && pass->get_lightgroup().empty() &&
          entry.requested_pass_mode != int(PassMode::NOISY))) continue;
      if (std::any_of(host.result.passes.begin(), host.result.passes.end(), [&](auto &p) { return p.name == name; })) continue;
      PassResult value; value.name = name; value.type = int(pass->get_type());
      value.components = unsigned(std::max(1, pass->get_info().num_components));
      value.pixels.assign(size_t(config->width) * config->height * value.components, 0.0f);
      host.result.passes.push_back(std::move(value));
    }
    host.requested_samples = config->samples; host.last_sample = -1; host.last_status.clear();
    struct Active { Host &host; Active(Host &h) : host(h) { host.rendering = true; } ~Active() { host.rendering = false; } } active(host);
    // Film events of a progressive host (CBC_HOST_PROGRESSIVE_RESULTS) for this render only.
    struct Films {
      Host &host;
      Films(Host &h) : host(h) { std::lock_guard lock(host.event_mutex); host.film_events = host.flags & CBC_HOST_PROGRESSIVE_RESULTS; host.film_pending = false; host.tiled_film.clear(); }
      ~Films() { std::lock_guard lock(host.event_mutex); host.film_events = false; host.film_pending = false; host.film.clear(); host.tiled_film.clear(); }
    } films(host);
    ParkedPasses parking{scene};
    render_pass_modes(host, parking);
    session.progress.reset();
    { CancelWatch watch{host}; session.reset(params, buffers); session.start(); session.wait(); }
    DropTiles drop_tiles{host};
    // The image manager sizes its device image table when it loads images (cbc_host_texture_limit).
    host.images_loaded = host.images_loaded || scene.dscene.image_textures.size() != 0;
    host.retry_undelivered_tiles();
    // A film the session left pending after its last progress event goes out here, on the calling thread, before the render
    // returns (the session thread has finished).
    if (host.film_pending && !session.progress.get_cancel()) {
      std::lock_guard lock(host.event_mutex);
      double total = 0, render = 0; session.progress.get_time(total, render);
      std::string status, substatus; session.progress.get_status(status, substatus);
      if (!substatus.empty()) status = status.empty() ? substatus : status + ": " + substatus;
      if (host.film_pending) host.emit_film(total, status);
    }
    try {
      host.render_tiles_delivered();
      require(!host.cancellation && !host.closing, "Render cancelled", CBC_CANCELLED);
      require(!session.progress.get_error(), session.progress.get_error_message().c_str(), CBC_FAILED);
      require(!session.device->have_error(), session.device->error_message().c_str(), CBC_FAILED);
      require(!session.progress.get_cancel(), "Render cancelled", CBC_CANCELLED);
      // A tiled render's film comes from its full frame in memory (cbc_host_tile_size); a cancel or failure during the
      // read-back fails the render as during rendering.
      const bool tiled = host.read_back_tiles();
      if (tiled) {
        require(!host.cancellation && !host.closing, "Render cancelled", CBC_CANCELLED);
        require(!session.progress.get_error(), session.progress.get_error_message().c_str(), CBC_FAILED);
        require(!session.device->have_error(), session.device->error_message().c_str(), CBC_FAILED);
        require(!session.progress.get_cancel(), "Render cancelled", CBC_CANCELLED);
      }
      require(host.result.delivered_error.empty(), host.result.delivered_error.c_str(), CBC_FAILED);
      require(host.result.writes == 1 && host.result.valid, "Missing complete Combined output", CBC_FAILED);
      require(std::all_of(host.result.pixels.begin(), host.result.pixels.end(), [](float value) { return std::isfinite(value); }), "Nonfinite Combined output", CBC_FAILED);
      // A progressive host's tiled render ends with the returned film (the full frame, denoised when denoising), as an
      // untiled render's last film is the returned film.
      if (tiled && (host.flags & CBC_HOST_PROGRESSIVE_RESULTS)) {
        std::lock_guard lock(host.event_mutex);
        double total = 0, render = 0; session.progress.get_time(total, render);
        std::string status, substatus; session.progress.get_status(status, substatus);
        if (!substatus.empty()) status = status.empty() ? substatus : status + ": " + substatus;
        host.film = host.result.pixels; host.emit_film(total, status);
      }
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
#include "host_bake.inc"
