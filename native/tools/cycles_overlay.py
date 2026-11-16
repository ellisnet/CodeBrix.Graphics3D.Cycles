#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Generated Cycles overlay shared by every RID: device registration names, the
render-scheduling and maintainer-tool additions and the in-memory tile store of
tiled rendering. WGSL generation stays in build_cycles.py."""
import difflib
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from cycles_source import SOURCE as UPSTREAM

# The in-memory tile store of integrator/path_trace.cpp (tiled rendering, registration_overlay below).
MEMORY_TILES = r'''
/* CodeBrix host (native/host/ABI.md "Tiled rendering"): the finished tiles of a tiled render stay in system memory.
 * The pinned TileManager (session/tile.cpp) writes them to a tile file through OpenImageIO, which needs an EXR writer,
 * and reads the file back for the full-frame work; this store takes the place of the three calls PathTrace makes into
 * that file, so a tiled render behaves the same in every build, whatever OpenImageIO plugins it has. A frame holds
 * what TileManager::read_full_buffer_from_disk would read back: the full buffer's parameters and passes and the
 * integrator's denoise parameters (the sockets the file's attributes carry), and every pixel's channels (the passes in
 * use, in buffer order), zero where no tile was written. A finished frame is found by its name, which the session's
 * full buffer callback receives in place of the file name. A frame lives until it is read back, the next render of its
 * path tracer starts, codebrix_memory_tiles drops it, or the path tracer is destroyed. No file is written or read. */
int codebrix_memory_tiles(const PathTrace *path_trace, bool discard);

namespace {

struct CodeBrixMemoryFrame {
  const PathTrace *owner = nullptr;
  bool finished = false;
  string name;
  BufferParams params;
  DenoiseParams denoise;
  int channels = 0;
  vector<float> pixels;
  int tiles = 0;
};

thread_mutex codebrix_memory_mutex;
std::list<CodeBrixMemoryFrame> codebrix_memory_frames;
std::map<const PathTrace *, DenoiseParams> codebrix_memory_denoise;
uint64_t codebrix_memory_frame_count = 0;

/* The sockets the tile file's attributes carry (node_to_image_spec_atttributes), set as its read-back sets them. */
void codebrix_memory_copy_sockets(Node *to, const Node &from)
{
  for (const SocketType &socket : from.type->inputs) {
    to->copy_value(socket, from, socket);
  }
}

/* The integrator's denoise parameters, which TileManager::update keeps with the tile file. */
void codebrix_memory_denoise_params(const PathTrace *owner, const DenoiseParams &params)
{
  const thread_scoped_lock lock(codebrix_memory_mutex);
  codebrix_memory_denoise[owner] = params;
}

/* TileManager::write_tile: the window of the tile's buffer goes to its place in the frame (the first tile of a render
 * starts the frame, from the full buffer's parameters, as TileManager::update describes the file). */
bool codebrix_memory_write_tile(const PathTrace *owner,
                                const BufferParams &full_params,
                                const RenderBuffers &tile_buffers)
{
  const thread_scoped_lock lock(codebrix_memory_mutex);
  CodeBrixMemoryFrame *frame = nullptr;
  for (CodeBrixMemoryFrame &candidate : codebrix_memory_frames) {
    if (candidate.owner == owner && !candidate.finished) {
      frame = &candidate;
    }
  }
  if (frame == nullptr) {
    frame = &codebrix_memory_frames.emplace_back();
    frame->owner = owner;
    frame->params = full_params;
    const auto denoise = codebrix_memory_denoise.find(owner);
    if (denoise != codebrix_memory_denoise.end()) {
      frame->denoise = denoise->second;
    }
    for (const BufferPass &pass : full_params.passes) {
      if (pass.offset != PASS_UNUSED) {
        frame->channels += pass.get_info().num_components;
      }
    }
    frame->pixels.resize(size_t(full_params.width) * size_t(full_params.height) * size_t(frame->channels), 0.0f);
  }

  const BufferParams &tile_params = tile_buffers.params;
  const int tile_x = tile_params.full_x - frame->params.full_x + tile_params.window_x;
  const int tile_y = tile_params.full_y - frame->params.full_y + tile_params.window_y;
  if (tile_params.pass_stride != frame->params.pass_stride || frame->channels > tile_params.pass_stride ||
      tile_x < 0 || tile_y < 0 || tile_x + tile_params.window_width > frame->params.width ||
      tile_y + tile_params.window_height > frame->params.height)
  {
    return false;
  }

  const int64_t pass_stride = tile_params.pass_stride;
  for (int y = 0; y < tile_params.window_height; ++y) {
    const float *in = tile_buffers.buffer.data() +
                      ((int64_t(tile_params.window_y) + y) * tile_params.width + tile_params.window_x) *
                          pass_stride;
    float *out = frame->pixels.data() +
                 ((int64_t(tile_y) + y) * frame->params.width + tile_x) * frame->channels;
    for (int x = 0; x < tile_params.window_width; ++x, in += pass_stride, out += frame->channels) {
      memcpy(out, in, sizeof(float) * frame->channels);
    }
  }
  ++frame->tiles;
  return true;
}

/* TileManager::finish_write_tiles: the frame is complete (a tile never written reads zero, as the zero tiles Cycles
 * writes for it) and gets its name. Empty when no tile was written. */
string codebrix_memory_finish_frame(const PathTrace *owner)
{
  const thread_scoped_lock lock(codebrix_memory_mutex);
  for (CodeBrixMemoryFrame &frame : codebrix_memory_frames) {
    if (frame.owner == owner && !frame.finished) {
      frame.finished = true;
      frame.name = "codebrix-memory-tile-frame-" + to_string(++codebrix_memory_frame_count);
      return frame.name;
    }
  }
  return string();
}

/* TileManager::read_full_buffer_from_disk: the buffer and denoise parameters rebuilt from their sockets, as from the
 * file's attributes, and the frame's pixels; the frame is dropped. */
bool codebrix_memory_read_frame(const string_view name, RenderBuffers *buffers, DenoiseParams *denoise_params)
{
  std::list<CodeBrixMemoryFrame> taken;
  {
    const thread_scoped_lock lock(codebrix_memory_mutex);
    for (auto it = codebrix_memory_frames.begin(); it != codebrix_memory_frames.end(); ++it) {
      if (it->finished && string_view(it->name) == name) {
        taken.splice(taken.begin(), codebrix_memory_frames, it);
        break;
      }
    }
  }
  if (taken.empty()) {
    LOG_ERROR << "No tile frame in memory named " << name;
    return false;
  }

  const CodeBrixMemoryFrame &frame = taken.front();
  BufferParams buffer_params;
  codebrix_memory_copy_sockets(&buffer_params, frame.params);
  for (const BufferPass &stored : frame.params.passes) {
    BufferPass pass;
    codebrix_memory_copy_sockets(&pass, stored);
    buffer_params.passes.emplace_back(std::move(pass));
  }
  if (buffer_params.passes.empty()) {
    LOG_ERROR << "Missing passes in the tile frame.";
    return false;
  }
  buffer_params.update_passes();
  buffers->reset(buffer_params);

  codebrix_memory_copy_sockets(denoise_params, frame.denoise);

  if (frame.pixels.size() > buffers->buffer.size()) {
    LOG_ERROR << "Tile frame larger than its render buffer.";
    return false;
  }
  memcpy(buffers->buffer.data(), frame.pixels.data(), sizeof(float) * frame.pixels.size());
  return true;
}

void codebrix_memory_release(const PathTrace *owner)
{
  codebrix_memory_tiles(owner, true);
  const thread_scoped_lock lock(codebrix_memory_mutex);
  codebrix_memory_denoise.erase(owner);
}

}  // namespace

/* The tiles held by path_trace's frames, finished or not; with discard the frames are dropped (the CodeBrix host's
 * bookkeeping, and a cancelled or failed render's frame). */
int codebrix_memory_tiles(const PathTrace *path_trace, const bool discard)
{
  const thread_scoped_lock lock(codebrix_memory_mutex);
  int tiles = 0;
  for (auto it = codebrix_memory_frames.begin(); it != codebrix_memory_frames.end();) {
    if (it->owner != path_trace) {
      ++it;
      continue;
    }
    tiles += it->tiles;
    it = discard ? codebrix_memory_frames.erase(it) : std::next(it);
  }
  return tiles;
}
'''

DELIVERED_PARAMS = r'''
/* CodeBrix host (native/host/ABI.md "Render output", the delivered film): the parameters of the render buffers whose
 * passes a write to the output driver delivers, while the driver's write_render_tile runs on this thread. The tile the
 * driver receives states the size of the render requested; these state the size the passes were rendered at, which
 * differs when the path tracer renders at a reduced resolution (the resolution divider, which a denoiser upscale factor
 * raises), so the host can refuse such a film by name instead of reading it as the requested one. */
const BufferParams *codebrix_delivered_buffer_params();

namespace {
thread_local const BufferParams *codebrix_delivered_params = nullptr;
}  // namespace

const BufferParams *codebrix_delivered_buffer_params()
{
  return codebrix_delivered_params;
}
'''


def registration_overlay(out):
    out=Path(out)
    (out/'device').mkdir(parents=True,exist_ok=True)
    before={name:(UPSTREAM/'src'/name).read_text() for name in ['device/device.h','device/device.cpp','integrator/path_trace.cpp','util/colorspace.cpp','util/types_gpu_compressed.h']};after=dict(before)
    def replace(name,old,new):
        if after[name].count(old)!=1:raise RuntimeError('Upstream overlay anchor changed: '+old)
        after[name]=after[name].replace(old,new)
    replace('device/device.h','  DEVICE_DUMMY,','  DEVICE_DUMMY,\n  DEVICE_WEBGPU,')
    replace('device/device.h','  DEVICE_MASK_ALL = ~0','  DEVICE_MASK_WEBGPU = (1 << DEVICE_WEBGPU),\n  DEVICE_MASK_ALL = ~0')
    # The host clears an error the scene caused once the caller has removed the
    # cause and the whole scene is uploaded again (ABI.md, "Device errors").
    replace('device/device.h','  virtual void set_error(const string &error);\n',
            '  virtual void set_error(const string &error);\n'
            '  /* CodeBrix host: forget a recorded error before a full scene re-upload. */\n'
            '  void clear_error()\n  {\n    error_msg.clear();\n  }\n')
    replace('device/device.cpp','#include "device/device.h"','#include "device/device.h"\n#ifdef WITH_WEBGPU\n#  include "codebrix_webgpu_device.h"\n#endif')
    replace('device/device.cpp','    case DEVICE_CPU:\n      device =',
            '#ifdef WITH_WEBGPU\n    case DEVICE_WEBGPU:\n      device = device_webgpu_create(info, stats, profiler, headless);\n      break;\n#endif\n    case DEVICE_CPU:\n      device =')
    replace('device/device.cpp','  if (strcmp(name, "ONEAPI") == 0) {',
            '  if (strcmp(name, "WEBGPU") == 0) {\n    return DEVICE_WEBGPU;\n  }\n  if (strcmp(name, "ONEAPI") == 0) {')
    replace('device/device.cpp','  if (type == DEVICE_ONEAPI) {',
            '  if (type == DEVICE_WEBGPU) {\n    return "WEBGPU";\n  }\n  if (type == DEVICE_ONEAPI) {')
    replace('device/device.cpp','  types.push_back(DEVICE_CPU);','  types.push_back(DEVICE_CPU);\n#ifdef WITH_WEBGPU\n  types.push_back(DEVICE_WEBGPU);\n#endif')
    replace('device/device.cpp','  if (mask & DEVICE_MASK_CPU) {\n    if (!(devices_initialized_mask',
            '#ifdef WITH_WEBGPU\n  if (mask & DEVICE_MASK_WEBGPU) {\n    device_webgpu_info(devices);\n  }\n#endif\n\n  if (mask & DEVICE_MASK_CPU) {\n    if (!(devices_initialized_mask')
    replace('integrator/path_trace.cpp','    case DEVICE_METAL:\n      return "Metal";',
            '    case DEVICE_METAL:\n      return "Metal";\n    case DEVICE_WEBGPU:\n      return "WebGPU";')
    # Volumes: the scatter probability guidance of a primary ray reads the volume majorant passes that the
    # pixel's earlier samples wrote when their paths ended. The CPU device renders a pixel's samples one after
    # another; a wavefront GPU device runs the samples of one update side by side, so their reads see a
    # different set of earlier samples. One sample per update gives the WebGPU device the CPU device's order.
    replace('integrator/path_trace.cpp','    render_scheduler_.set_limit_samples_per_update(limit);\n  }\n',
            '    render_scheduler_.set_limit_samples_per_update(limit);\n'
            '    /* CodeBrix WebGPU: one sample per update with volumes (the CPU device\'s sample order). */\n'
            '    if (device_->info.type == DEVICE_WEBGPU) {\n'
            '      render_scheduler_.set_limit_samples_per_update(1);\n'
            '    }\n  }\n'
            '  /* CodeBrix maintainer modes (native/cycles/fingerprint.h, scene_capture.h): one sample per update keeps the\n'
            '   * hashed command sequence independent of the clock (RenderScheduler::calculate_num_samples_per_update), and a\n'
            '   * capture\'s path records at their first size. The film does not depend on this grouping. */\n'
            '  if (device_->info.type == DEVICE_WEBGPU && (getenv("CODEBRIX_CYCLES_FINGERPRINT") || getenv("CODEBRIX_CYCLES_RENDER_CAPTURE"))) {\n'
            '    render_scheduler_.set_limit_samples_per_update(1);\n'
            '  }\n')
    replace('integrator/path_trace.cpp','#include "integrator/path_trace.h"\n','#include "integrator/path_trace.h"\n\n#include <cstdio>\n#include <cstdlib>\n')
    # Replay of an oracle scene (native/tools/oracle_replay.py): the CPU reference's raw render buffer after each update.
    replace('integrator/path_trace.cpp','  denoise_volume_guiding_buffers(render_work, has_volume);\n  if (render_cancel_.is_requested) {\n    return;\n  }\n',
            '  denoise_volume_guiding_buffers(render_work, has_volume);\n  if (render_cancel_.is_requested) {\n    return;\n  }\n'
            '  /* CodeBrix maintainer tool: raw render buffer of the CPU device, DIR/s<samples>.bin. */\n'
            '  if (const char *dump = getenv("CODEBRIX_CYCLES_RENDER_BUFFER_DUMP")) {\n'
            '    path_trace_works_[0]->copy_render_buffers_from_device();\n'
            '    RenderBuffers *buffers = path_trace_works_[0]->get_render_buffers();\n'
            '    const string name = string(dump) + "/s" + to_string(render_work.path_trace.start_sample + render_work.path_trace.num_samples) + ".bin";\n'
            '    if (FILE *file = fopen(name.c_str(), "wb")) {\n'
            '      fwrite(buffers->buffer.data(), sizeof(float), buffers->buffer.size(), file);\n'
            '      fclose(file);\n'
            '    }\n'
            '  }\n')
    # Tiled rendering (native/host/ABI.md "Tiled rendering"): the finished tiles stay in system memory. The pinned
    # TileManager (session/tile.cpp) writes them to a tile file through OpenImageIO, which needs an EXR writer some
    # builds do not have, and reads it back for the full-frame work; PathTrace's three calls into that file go to the
    # in-memory store of MEMORY_TILES instead, the same on every platform. session/tile.cpp itself is not overlaid (no
    # build compiles an overlay copy of it) and none of these files is in the Metal kernel closure.
    replace('integrator/path_trace.cpp','#include "util/time.h"\n','#include "util/time.h"\n\n#include <cstring>\n#include <list>\n#include <map>\n')
    replace('integrator/path_trace.cpp','CCL_NAMESPACE_BEGIN\n\nPathTrace::PathTrace(','CCL_NAMESPACE_BEGIN\n'+MEMORY_TILES+DELIVERED_PARAMS+'\nPathTrace::PathTrace(')
    replace('integrator/path_trace.cpp','PathTrace::~PathTrace()\n{\n  destroy_gpu_resources();\n}\n',
            'PathTrace::~PathTrace()\n{\n  destroy_gpu_resources();\n'
            '  /* CodeBrix: the path tracer\'s frames in memory and its denoise parameters go with it. */\n'
            '  codebrix_memory_release(this);\n}\n')
    replace('integrator/path_trace.cpp','  full_params_ = full_params;\n',
            '  full_params_ = full_params;\n\n'
            '  /* CodeBrix: a new render drops the frames an earlier render left in memory (TileManager::reset_scheduling closes\n'
            '   * the tile file a stopped render left open). */\n'
            '  if (reset_rendering) {\n'
            '    codebrix_memory_tiles(this, true);\n'
            '  }\n')
    replace('integrator/path_trace.cpp','void PathTrace::set_denoiser_params(const DenoiseParams &params)\n{\n',
            'void PathTrace::set_denoiser_params(const DenoiseParams &params)\n{\n'
            '  /* CodeBrix: the integrator\'s denoise parameters, kept with a frame in memory as TileManager::update keeps them. */\n'
            '  codebrix_memory_denoise_params(this, params);\n\n')
    replace('integrator/path_trace.cpp',
            '  if (!tile_manager_.write_tile(*buffers)) {\n'
            '    device_->set_error("Error writing tile to file");\n'
            '  }\n',
            '  /* CodeBrix: the tile goes to the frame in memory, never to a tile file. */\n'
            '  if (!codebrix_memory_write_tile(this, full_params_, *buffers)) {\n'
            '    device_->set_error("Error keeping tile in memory");\n'
            '  }\n')
    replace('integrator/path_trace.cpp',
            '  if (!tile_manager_.has_written_tiles()) {\n'
            '    LOG_DEBUG << "No tiles on disk.";\n'
            '    return;\n'
            '  }\n'
            '\n'
            '  /* Make sure writing to the file is fully finished.\n'
            '   * This will include writing all possible missing tiles, ensuring validness of the file. */\n'
            '  tile_manager_.finish_write_tiles();\n',
            '  /* CodeBrix: the frame in memory is complete, and its name goes to the session\'s full buffer callback in place of\n'
            '   * the tile file\'s name (TileManager::finish_write_tiles). */\n'
            '  const string codebrix_frame = codebrix_memory_finish_frame(this);\n'
            '  if (codebrix_frame.empty()) {\n'
            '    LOG_DEBUG << "No tiles in memory.";\n'
            '    return;\n'
            '  }\n'
            '  if (tile_manager_.full_buffer_written_cb) {\n'
            '    tile_manager_.full_buffer_written_cb(codebrix_frame);\n'
            '  }\n')
    replace('integrator/path_trace.cpp',
            '  progress_set_status("Reading full buffer from disk");\n'
            '\n'
            '  RenderBuffers full_frame_buffers(cpu_device_.get());\n'
            '\n'
            '  DenoiseParams denoise_params;\n'
            '  if (!tile_manager_.read_full_buffer_from_disk(filename, &full_frame_buffers, &denoise_params)) {\n'
            '    const string error_message = "Error reading tiles from file";\n',
            '  /* CodeBrix: the frame comes from memory (TileManager::read_full_buffer_from_disk reads the tile file). */\n'
            '  progress_set_status("Reading full buffer from memory");\n'
            '\n'
            '  RenderBuffers full_frame_buffers(cpu_device_.get());\n'
            '\n'
            '  DenoiseParams denoise_params;\n'
            '  if (!codebrix_memory_read_frame(filename, &full_frame_buffers, &denoise_params)) {\n'
            '    const string error_message = "Error reading tiles from memory";\n')
    # The delivered film (native/host/ABI.md "Render output"): the host learns the parameters of the render buffers a write
    # to the output driver reads from (the full frame of a tiled render, else the effective parameters of the big tile,
    # which the resolution divider scales), to verify that the passes it receives have the size it asked for.
    replace('integrator/path_trace.cpp',
            '  const PathTraceTile tile(*this);\n'
            '  output_driver_->write_render_tile(tile);\n',
            '  const PathTraceTile tile(*this);\n'
            '  /* CodeBrix: the parameters the passes of this write are read with, for the host\'s check of the delivered film. */\n'
            '  codebrix_delivered_params = full_frame_state_.render_buffers ? &full_frame_state_.render_buffers->params :\n'
            '                                                                 &render_state_.effective_big_tile_params;\n'
            '  output_driver_->write_render_tile(tile);\n'
            '  codebrix_delivered_params = nullptr;\n')
    # Integrator state precision: the pinned source stores some state fields in half precision on GPU devices
    # (FloatCompressedOnGPU, SpectrumCompressedOnGPU). The WebGPU device keeps them in full precision, in the types
    # those fields had before the compressed wrappers existed (float and PackedSpectrum). Only the host-side choice of
    # the stored type changes, and only with WITH_WEBGPU: a build without it compiles the upstream text.
    replace('util/types_gpu_compressed.h',
            'template<typename T> struct gpu_state_storage<T, gpu_void_t<typename T::compressed_type>> {\n'
            '  using gpu_type = typename T::compressed_type;\n};\n',
            'template<typename T> struct gpu_state_storage<T, gpu_void_t<typename T::compressed_type>> {\n'
            '  using gpu_type = typename T::compressed_type;\n};\n'
            '#ifdef WITH_WEBGPU\n'
            '/* CodeBrix WebGPU: compressed-on-GPU state fields are stored in full precision. */\n'
            'template<> struct gpu_state_storage<FloatCompressedOnGPU, void> {\n  using gpu_type = float;\n};\n'
            'template<> struct gpu_state_storage<SpectrumCompressedOnGPU, void> {\n  using gpu_type = PackedSpectrum;\n};\n'
            '#endif\n')
    # util/colorspace.cpp is handed over UNCHANGED. The pinned source invalidates its caches when the OpenColorIO
    # configuration object changes (it keeps the configuration it cached for), which covers a configuration whose
    # transforms change while its scene-linear name stays the same, so the earlier correction is gone. The file stays
    # in the overlay only because every platform's build recipe and host gate still take it from here.
    for name in after:
        if name != 'util/colorspace.cpp':
            after[name]='// Copyright (c) 2026 Jeremy Ellis and contributors\n// CodeBrix WebGPU registration additions; upstream notices follow.\n'+after[name]
    patch=''.join(''.join(difflib.unified_diff(before[name].splitlines(True),after[name].splitlines(True),
                     fromfile='a/src/'+name,tofile='b/src/'+name)) for name in before)
    if not (out.parent/'cycles-registration.patch').exists() or (out.parent/'cycles-registration.patch').read_text()!=patch:
        (out.parent/'cycles-registration.patch').write_text(patch)
    # A file an earlier overlay held and this one does not: a copy left in OUT would still shadow the pinned file.
    for retired in ['kernel/integrator/mnee.h']:
        (out/retired).unlink(missing_ok=True)
    for name,source in after.items():
        path=out/name;path.parent.mkdir(parents=True,exist_ok=True)
        if not path.exists() or path.read_text()!=source:path.write_text(source)
    return out
