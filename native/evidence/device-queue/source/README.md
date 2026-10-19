# CodeBrix.Graphics3D.Cycles

Native Cycles integration with a new WebGPU backend, targeting full Cycles
parity while using the unchanged binaries shipped by CodeBrix.WebGpu.Native.
Work is in progress; this is not yet a production renderer or NuGet package.

- [Parity ledger and remaining work](PARITY.md)
- [Successful native scene-to-image prototype](prototype/README.md)
- [Original material tests and real-model renders](parity/README.md)
- [Multi-bounce WGSL render versus the original Cycles integrator](parity/README.md#multi-bounce-integration-against-the-original-integrator)
- [GPU camera and sampling checkpoint](parity/README.md#gpu-camera-and-sampling)
- [Finite lights, shadows, MIS and the Pixel dispatch fix](parity/README.md#finite-lights-shadows-and-mis)
- [Reusable native backend, bounded batches and GPU film accumulation](native/README.md)
- [Registered Cycles device and wavefront scheduler kernels](native/DEVICE.md)
- [Kernel research and feasibility findings](research/FEASIBILITY.md)

The prototype renders an actual Cycles-prepared scene on Linux x64 and Android
ARM64/x64, including API 33, with no CLR or Slang in the rendering path. Its
current shading is deliberately restricted; full materials and integration
remain under development. Changes are left uncommitted for Jeremy's review.
