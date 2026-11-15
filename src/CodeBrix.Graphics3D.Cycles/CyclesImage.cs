// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// Caller-supplied pixels copied into a <see cref="CyclesScene"/>, created with
/// <see cref="CyclesScene.CreateImage{T}"/> and bound to image or environment texture nodes, or a tiled image whose
/// reader supplies its tiles and mip levels (<see cref="CyclesScene.CreateTiledImage"/>, provisional).
/// Disposing it releases the caller's reference only: textures it is bound to keep the pixels (or the reader).
/// </summary>
public sealed class CyclesImage : IDisposable
{
    private readonly CyclesScene scene;
    private bool released;

    internal CyclesImage(CyclesScene scene, Image handle, uint width, uint height, ImageFormat format, bool tiled = false)
    {
        this.scene = scene;
        Handle = handle;
        Width = width;
        Height = height;
        Format = format;
        IsTiled = tiled;
    }

    /// <summary>
    /// Gets a value indicating whether this is a tiled image (provisional; <see cref="CyclesScene.CreateTiledImage"/>),
    /// whose reader supplies its pixels; it cannot be updated.
    /// </summary>
    public bool IsTiled { get; }

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
    /// <exception cref="InvalidOperationException">The image is a tiled image (<see cref="IsTiled"/>).</exception>
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
    /// <exception cref="InvalidOperationException">The image is a tiled image (<see cref="IsTiled"/>): its reader supplies its pixels.</exception>
    /// <exception cref="ArgumentException"><typeparamref name="T"/> does not match the component size of <paramref name="format"/>.</exception>
    /// <exception cref="NativeFailure">The size, byte count or values are invalid (status 1).</exception>
    public void Update<T>(uint width, uint height, ImageFormat format, ReadOnlySpan<T> pixels) where T : unmanaged
    {
        ObjectDisposedException.ThrowIf(released, this);
        if (IsTiled) throw new InvalidOperationException("A tiled image cannot be updated: its reader supplies its pixels. Create a new tiled image, or bind another image.");
        scene.UpdateImage(this, width, height, format, CyclesScene.PixelBytes(format, pixels));
        Width = width;
        Height = height;
        Format = format;
    }
}
