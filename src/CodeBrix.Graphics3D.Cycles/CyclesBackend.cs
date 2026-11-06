// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// A backend-neutral device choice for <see cref="CyclesScene"/>. <see cref="CyclesScene.ResolveBackend"/>
/// maps it to the concrete <see cref="Backend"/> of the running platform; which GPU device actually
/// renders is visible only through <see cref="CyclesScene.GetCapabilities"/>.
/// </summary>
public enum CyclesBackend
{
    /// <summary>
    /// The platform's GPU backend: Cycles' native Metal device in osx-arm64 (Apple Silicon) processes,
    /// and WebGPU everywhere else, osx-x64 (Intel macOS) included. A GPU render never falls back to the CPU.
    /// </summary>
    Gpu = 0,

    /// <summary>The explicit CPU reference backend, which runs the native CPU kernels.</summary>
    CpuReference = 1,

    /// <summary>
    /// The CPU backend with Embree ray tracing (<see cref="Backend.Cpu"/>): the native CPU kernels with Cycles'
    /// automatic BVH choice. Its films agree with <see cref="CpuReference"/> closely, not bit for bit (a different
    /// BVH visits primitives in another order, and a rare single pixel can differ visibly).
    /// </summary>
    Cpu = 2
}
