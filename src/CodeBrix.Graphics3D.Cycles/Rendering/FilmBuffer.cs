// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles.Rendering;

/// <summary>
/// A rendered film or pass: raw scene-linear float32, <see cref="Components"/> floats per pixel (four,
/// RGBA, for the combined pass), rows bottom to top (row 0 is the bottom of the image, the lower-left
/// origin of the native host). Values are unclamped and not display-encoded; convert and encode them
/// with CodeBrix.Imaging.
/// </summary>
public sealed class FilmBuffer
{
    /// <summary>The number of floats per pixel of the combined pass (red, green, blue, alpha).</summary>
    public const int Channels = 4;

    private readonly float[] pixels;

    internal FilmBuffer(uint width, uint height, float[] pixels, RenderPass pass)
        : this(width, height, pixels, pass, "Combined", Channels)
    {
    }

    internal FilmBuffer(uint width, uint height, float[] pixels, RenderPass pass, string passName, int components)
    {
        Width = checked((int)width);
        Height = checked((int)height);
        this.pixels = pixels;
        Pass = pass;
        PassName = passName;
        Components = components;
    }

    /// <summary>Gets the width in pixels.</summary>
    public int Width { get; }

    /// <summary>Gets the height in pixels.</summary>
    public int Height { get; }

    /// <summary>Gets the floats per pixel: 4 for <see cref="RenderPass.Combined"/>, otherwise as the native pass reports (1, 3 or 4).</summary>
    public int Components { get; }

    /// <summary>Gets the row stride in floats (<c>Width * Components</c>; rows are tightly packed).</summary>
    public int Stride => Width * Components;

    /// <summary>Gets the kind of pass this film holds.</summary>
    public RenderPass Pass { get; }

    /// <summary>Gets the native pass name (<c>Combined</c> for the combined pass).</summary>
    public string PassName { get; }

    /// <summary>Gets all pixels, row 0 (the bottom row) first.</summary>
    public ReadOnlyMemory<float> Pixels => pixels;

    /// <summary>Gets all pixels as a span, row 0 (the bottom row) first.</summary>
    public ReadOnlySpan<float> Span => pixels;

    /// <summary>
    /// Gets one row of pixels.
    /// </summary>
    /// <param name="y">The row index, 0 being the bottom row.</param>
    /// <returns>The row's <see cref="Stride"/> floats.</returns>
    /// <exception cref="ArgumentOutOfRangeException"><paramref name="y"/> is outside the image.</exception>
    public ReadOnlySpan<float> GetRow(int y)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(y);
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(y, Height);
        return pixels.AsSpan(y * Stride, Stride);
    }

    /// <summary>
    /// Copies the pixels into a caller-owned buffer.
    /// </summary>
    /// <param name="destination">At least <c>Width * Height * Components</c> floats.</param>
    /// <exception cref="ArgumentException"><paramref name="destination"/> is too small.</exception>
    public void CopyTo(Span<float> destination) => pixels.AsSpan().CopyTo(destination);

    /// <summary>
    /// Returns a copy of the pixels.
    /// </summary>
    /// <returns>A new array of <c>Width * Height * Components</c> floats.</returns>
    public float[] ToArray() => (float[])pixels.Clone();
}
