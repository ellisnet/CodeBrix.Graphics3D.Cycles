// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// Selects the native device a <see cref="CyclesHost"/> renders with. The values equal the
/// <c>CBC_HOST_*</c> backend constants of the native host ABI.
/// </summary>
public enum Backend : uint
{
    /// <summary>The WebGPU backend. A requested WebGPU render never falls back to CPU shading.</summary>
    WebGpu = 1,

    /// <summary>The explicit CPU reference backend, which runs the native CPU kernels.</summary>
    CpuReference = 2,

    /// <summary>
    /// The native Metal device. It is available only in the osx-arm64 build; other builds reject it
    /// as unsupported.
    /// </summary>
    Metal = 3,

    /// <summary>
    /// The backend-neutral GPU request (<c>CBC_HOST_GPU</c>): the native host resolves it to Cycles'
    /// Metal device in macOS builds and to WebGPU in every other build. A host never reports it as its
    /// concrete backend; <see cref="CyclesHost.GetBackend"/> returns what was chosen.
    /// </summary>
    Gpu = 4
}
