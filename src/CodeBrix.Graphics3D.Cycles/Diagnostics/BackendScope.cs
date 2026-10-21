// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System.Collections.Generic;

namespace CodeBrix.Graphics3D.Cycles.Diagnostics;

/// <summary>
/// What the native capability report states about one compiled backend: its device, the camera
/// types and primitives it renders, the passes it writes and the classes of scene features it rejects
/// at render time. Scene features the report does not list may still fail at render time.
/// </summary>
public sealed class BackendScope
{
    internal BackendScope(string device, IReadOnlyList<string> cameraTypes, IReadOnlyList<string> primitives,
        IReadOnlyList<string> writtenPasses, string passesNote, IReadOnlyList<string> renderTimeRejections, string reference)
    {
        Device = device;
        CameraTypes = cameraTypes;
        Primitives = primitives;
        WrittenPasses = writtenPasses;
        PassesNote = passesNote;
        RenderTimeRejections = renderTimeRejections;
        Reference = reference;
    }

    /// <summary>Gets the device description.</summary>
    public string Device { get; }

    /// <summary>Gets the camera types the backend renders (for example only <c>perspective</c> on WebGPU).</summary>
    public IReadOnlyList<string> CameraTypes { get; }

    /// <summary>Gets the primitive types the backend renders, or an empty list when the report does not restrict them.</summary>
    public IReadOnlyList<string> Primitives { get; }

    /// <summary>Gets the pass types the backend writes when the report lists them (WebGPU: <c>combined</c>); empty when the report gives <see cref="PassesNote"/> instead.</summary>
    public IReadOnlyList<string> WrittenPasses { get; }

    /// <summary>Gets the report's statement about passes when it is text rather than a list, otherwise <see langword="null"/>.</summary>
    public string PassesNote { get; }

    /// <summary>Gets the classes of scene features the device rejects at render time (empty when none are listed).</summary>
    public IReadOnlyList<string> RenderTimeRejections { get; }

    /// <summary>Gets the reference document the report points to, or <see langword="null"/>.</summary>
    public string Reference { get; }
}
