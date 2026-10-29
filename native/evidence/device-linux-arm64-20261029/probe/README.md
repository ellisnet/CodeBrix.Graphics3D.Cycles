# WebGPU adapter-limit probes (finding L1)

Copyright (c) 2026 Jeremy Ellis and contributors

Scratch diagnostics, compiled against the unchanged CodeBrix.WebGpu.Native.MitLicenseForever
1.0.300.210 headers and `runtimes/linux-arm64/native/libcodebrix_wgpu.so`:

    P=~/.nuget/packages/codebrix.webgpu.native.mitlicenseforever/1.0.300.210
    gcc -O1 -I$P/include probe.c -o probe -L$P/runtimes/linux-arm64/native -lcodebrix_wgpu -Wl,-rpath,$P/runtimes/linux-arm64/native

- `probe.c`: mirrors `cbc::Device` (native/src/webgpu_device.cpp) before the L1 fix: Vulkan
  instance, adapters with their limits, the device request with only the two buffer-size
  limits raised. `probe.output.txt`: the V3D device gets `maxStorageBuffersPerShaderStage` 4
  (the host requires 7) although the adapter supports 8.
- `limits.c`: every limit for the V3D adapter, the device with the fixed request (the three
  WebGPU defaults requested) and the device with no request. `limits.output.txt`.
