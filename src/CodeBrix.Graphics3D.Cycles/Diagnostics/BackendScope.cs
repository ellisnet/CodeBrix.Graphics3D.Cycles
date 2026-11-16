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
        IReadOnlyList<string> writtenPasses, string passesNote, IReadOnlyList<string> renderTimeRejections, string reference,
        long? maxImageDimension = null, long? maxImageTileSize = null)
    {
        Device = device;
        CameraTypes = cameraTypes;
        Primitives = primitives;
        WrittenPasses = writtenPasses;
        PassesNote = passesNote;
        RenderTimeRejections = renderTimeRejections;
        Reference = reference;
        MaxImageDimension = maxImageDimension;
        MaxImageTileSize = maxImageTileSize;
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

    /// <summary>
    /// (Provisional) Gets the largest image width or height the backend's device takes for an image loaded whole
    /// (<see cref="CyclesScene.CreateImage{T}"/>): 16384 on Cycles' Metal device, 1073741823 on the WebGPU device and on
    /// the CPU backends (whose kernels compute twice the side in <c>int</c> for the Mirror extension); <see langword="null"/>
    /// when the report does not state it. A larger image fails the render, on the CPU backends by name (a
    /// <see cref="CyclesScene.TextureLimit"/> that reduces it below the limit brings it within).
    /// </summary>
    public long? MaxImageDimension { get; }

    /// <summary>
    /// (Provisional) Gets the largest tile side of a tiled image (<see cref="CyclesScene.CreateTiledImage"/>) on the
    /// backend: 128 on Cycles' Metal device, 1024 on the CPU backends and on WebGPU (which refuses tiled images at
    /// render); <see langword="null"/> when the report does not state it. See <see cref="TiledImage"/>.
    /// </summary>
    public long? MaxImageTileSize { get; }
}
