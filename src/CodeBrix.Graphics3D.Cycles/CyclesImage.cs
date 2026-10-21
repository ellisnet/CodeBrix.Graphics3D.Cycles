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
    public uint Width { get; private set; }

    /// <summary>Gets the height in pixels.</summary>
    public uint Height { get; private set; }

    /// <summary>Gets the pixel layout.</summary>
    public ImageFormat Format { get; private set; }

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

    /// <summary>
    /// Replaces the pixels with new ones of the same size and layout. Every texture still bound to this
    /// image uses them on the next render; textures bound before <see cref="Dispose"/> keep their pixels.
    /// </summary>
    /// <typeparam name="T">The pixel component type, or <see cref="byte"/> for raw bytes.</typeparam>
    /// <param name="pixels">The pixels; copied before the call returns.</param>
    /// <exception cref="ObjectDisposedException">The image was released.</exception>
    /// <exception cref="ArgumentException"><typeparamref name="T"/> does not match the component size of <see cref="Format"/>.</exception>
    /// <exception cref="NativeFailure">The byte count or values are invalid (status 1).</exception>
    public void Update<T>(ReadOnlySpan<T> pixels) where T : unmanaged => Update(Width, Height, Format, pixels);

    /// <summary>
    /// Replaces the pixels, size and layout. Every texture still bound to this image uses them on the
    /// next render; textures bound before <see cref="Dispose"/> keep their pixels. Raw pixels only:
    /// decode image files with CodeBrix.Imaging first.
    /// </summary>
    /// <typeparam name="T">The pixel component type, or <see cref="byte"/> for raw bytes.</typeparam>
    /// <param name="width">The new width in pixels.</param>
    /// <param name="height">The new height in pixels.</param>
    /// <param name="format">The new pixel layout.</param>
    /// <param name="pixels">The pixels; copied before the call returns.</param>
    /// <exception cref="ObjectDisposedException">The image was released.</exception>
    /// <exception cref="ArgumentException"><typeparamref name="T"/> does not match the component size of <paramref name="format"/>.</exception>
    /// <exception cref="NativeFailure">The size, byte count or values are invalid (status 1).</exception>
    public void Update<T>(uint width, uint height, ImageFormat format, ReadOnlySpan<T> pixels) where T : unmanaged
    {
        ObjectDisposedException.ThrowIf(released, this);
        scene.UpdateImage(this, width, height, format, CyclesScene.PixelBytes(format, pixels));
        Width = width;
        Height = height;
        Format = format;
    }
}
