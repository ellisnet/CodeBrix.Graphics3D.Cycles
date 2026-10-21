// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// Caller-supplied pixels copied into a <see cref="CyclesScene"/>, created with
/// <see cref="CyclesScene.CreateImage{T}"/> and bound to image or environment texture nodes.
/// Disposing it releases the caller's reference only: textures it is bound to keep the pixels.
/// </summary>
public sealed class CyclesImage : IDisposable
{
    private readonly CyclesScene scene;
    private bool released;

    internal CyclesImage(CyclesScene scene, Image handle, uint width, uint height, ImageFormat format)
    {
        this.scene = scene;
        Handle = handle;
        Width = width;
        Height = height;
        Format = format;
    }

    /// <summary>Gets the native image identifier, for use with the low-level <see cref="CyclesHost"/>.</summary>
    public Image Handle { get; }

    /// <summary>Gets the width in pixels.</summary>
    public uint Width { get; }

    /// <summary>Gets the height in pixels.</summary>
    public uint Height { get; }

    /// <summary>Gets the pixel layout.</summary>
    public ImageFormat Format { get; }

    /// <summary>Gets the scene that owns the image.</summary>
    public CyclesScene Scene => scene;

    /// <summary>Gets a value indicating whether the caller's reference has been released.</summary>
    public bool IsReleased => released;

    /// <summary>
    /// Releases the caller's reference. Bound textures keep the pixels. Calling it again, or after
    /// the scene was disposed, has no effect.
    /// </summary>
    public void Dispose()
    {
        if (released) return;
        released = true;
        scene.ReleaseImage(this);
    }
}
