#!/usr/bin/env python3
# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
"""Generated Cycles overlay shared by every RID: device registration names and
the render-scheduling and maintainer-tool additions. WGSL generation stays in build_cycles.py."""
import difflib
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
from cycles_source import SOURCE as UPSTREAM


def registration_overlay(out):
    out=Path(out)
    (out/'device').mkdir(parents=True,exist_ok=True)
    before={name:(UPSTREAM/'src'/name).read_text() for name in ['device/device.h','device/device.cpp','integrator/path_trace.cpp','util/colorspace.cpp','util/types_gpu_compressed.h','kernel/integrator/mnee.h']};after=dict(before)
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
    # MNEE: the import compiles spectral sampling out, which leaves this flag written and never read
    # (-Wunused-but-set-variable in every translation unit with the CPU kernel). No behaviour changes.
    replace('kernel/integrator/mnee.h','  bool has_dispersion = false;\n','  [[maybe_unused]] bool has_dispersion = false;\n')
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
    for name,source in after.items():
        path=out/name;path.parent.mkdir(parents=True,exist_ok=True)
        if not path.exists() or path.read_text()!=source:path.write_text(source)
    return out
