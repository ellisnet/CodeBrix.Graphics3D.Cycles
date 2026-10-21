// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System.Collections.Generic;

namespace CodeBrix.Graphics3D.Cycles.Diagnostics;

/// <summary>
/// A snapshot of the native capability report (<c>cbc_host_capabilities_json</c>, described in
/// <c>native/host/ABI.md</c>): what the loaded native binary compiles in and, for a scene's report,
/// what its device reports about itself. This is the only place the concrete GPU backend (WebGPU or
/// Metal) is visible to backend-neutral code. Every member is read from the native report; nothing is
/// inferred in managed code.
/// </summary>
public interface ICapabilityReport
{
    /// <summary>Gets the report schema version (0 for a bare device report without the binary-level fields).</summary>
    int SchemaVersion { get; }

    /// <summary>Gets the ABI version the native host reported.</summary>
    uint AbiVersion { get; }

    /// <summary>Gets the native library version (<c>major.minor.yyyyMMdd.build</c>), or <see langword="null"/> in a bare device report.</summary>
    string LibraryVersion { get; }

    /// <summary>Gets the pinned upstream Cycles revision, or <see langword="null"/> in a bare device report.</summary>
    string CyclesRevision { get; }

    /// <summary>Gets a value indicating whether the report describes a device (a scene's report) rather than only the binary.</summary>
    bool HasDevice { get; }

    /// <summary>Gets the device's native backend name (<c>webgpu</c>, <c>metal</c> or <c>cpu</c>), or <see langword="null"/> for the binary-level report.</summary>
    string BackendName { get; }

    /// <summary>Gets the concrete backend that renders, or <see cref="Cycles.Backend.Gpu"/> (never a concrete device) for the binary-level report.</summary>
    Backend Backend { get; }

    /// <summary>Gets a value indicating whether the device is a GPU (<see langword="false"/> for the binary-level report).</summary>
    bool IsGpu { get; }

    /// <summary>Gets the device description (adapter and driver), or <see langword="null"/> when the report has no device.</summary>
    string DeviceDescription { get; }

    /// <summary>Gets the native device identifier, or <see langword="null"/> when the report has no device.</summary>
    string DeviceId { get; }

    /// <summary>Gets the WebGPU pipeline tier in use (<c>full</c> or <c>conservative</c>), or <see langword="null"/> for other backends and the binary-level report.</summary>
    string PipelineTier { get; }

    /// <summary>Gets the device warnings the native host reported (empty when there are none).</summary>
    IReadOnlyList<string> Warnings { get; }

    /// <summary>Gets the backends compiled into the native binary (for example <c>cpu</c> and <c>webgpu</c>).</summary>
    IReadOnlyList<string> CompiledBackends { get; }

    /// <summary>Gets the backend name a <see cref="Cycles.Backend.Gpu"/> request resolves to in this binary, or <see langword="null"/> when none is compiled in.</summary>
    string GpuBackendName { get; }

    /// <summary>Gets a value indicating whether a GPU request may fall back to the CPU (the native host reports <see langword="false"/>).</summary>
    bool GpuFallsBackToCpu { get; }

    /// <summary>Gets the per-backend scope statements of the native report, keyed by backend name.</summary>
    IReadOnlyDictionary<string, BackendScope> BackendScopes { get; }

    /// <summary>Gets the scope statement of this report's device backend, or <see langword="null"/> for the binary-level report.</summary>
    BackendScope Scope { get; }

    /// <summary>Gets the optional native libraries (OSL, Embree, OpenImageDenoise, OpenVDB) and whether each is compiled in.</summary>
    IReadOnlyDictionary<string, bool> OptionalLibraries { get; }

    /// <summary>Gets the accepted raw image input formats (<c>float</c>, <c>float4</c>, <c>half</c>, ...).</summary>
    IReadOnlyList<string> ImageFormats { get; }

    /// <summary>Gets every registered Cycles pass type name and its native value.</summary>
    IReadOnlyDictionary<string, int> PassTypes { get; }

    /// <summary>Gets the native render event kinds (<c>status</c>, <c>progress</c>, <c>error</c>).</summary>
    IReadOnlyList<string> Events { get; }

    /// <summary>Gets the native limits (for example <c>maxRenderWidth</c>, <c>maxMotionSteps</c>, <c>maxThreads</c>).</summary>
    IReadOnlyDictionary<string, long> Limits { get; }

    /// <summary>Gets the reference to the native determinism statement (a section of <c>native/host/ABI.md</c>), or <see langword="null"/>.</summary>
    string DeterminismReference { get; }

    /// <summary>Gets the complete native report as JSON, including fields this interface does not surface.</summary>
    string Json { get; }
}
