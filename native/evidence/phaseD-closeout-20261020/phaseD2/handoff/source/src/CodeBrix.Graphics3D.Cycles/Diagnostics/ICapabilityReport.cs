// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System.Collections.Generic;

namespace CodeBrix.Graphics3D.Cycles.Diagnostics;

/// <summary>
/// A snapshot of what the device behind one scene reports about itself. This is the only place the
/// concrete GPU backend (WebGPU or Metal) is visible to backend-neutral code.
/// </summary>
public interface ICapabilityReport
{
    /// <summary>Gets the ABI version the native host reported with the report.</summary>
    uint AbiVersion { get; }

    /// <summary>Gets the native backend name: <c>webgpu</c>, <c>metal</c> or <c>cpu</c>.</summary>
    string BackendName { get; }

    /// <summary>Gets the concrete backend that renders.</summary>
    Backend Backend { get; }

    /// <summary>Gets a value indicating whether the device is a GPU.</summary>
    bool IsGpu { get; }

    /// <summary>Gets the WebGPU pipeline tier in use (<c>full</c> or <c>conservative</c>), or <see langword="null"/> for other backends.</summary>
    string PipelineTier { get; }

    /// <summary>Gets the device warnings the native host reported (empty when there are none).</summary>
    IReadOnlyList<string> Warnings { get; }

    /// <summary>Gets the complete native report as JSON, including fields this interface does not surface.</summary>
    string Json { get; }
}
