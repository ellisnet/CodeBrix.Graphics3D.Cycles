// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles.Rendering;

/// <summary>
/// Which triangle and which point of it each output pixel of a bake shades
/// (<see cref="CyclesScene.ReadBakeMap(Generated.Scene.ObjectNode, BakeSettings)"/>). A pixel that <see cref="GetTriangle"/>
/// reports as -1 has no triangle: nothing is baked there. An application that wants a margin dilates
/// its baked image from these pixels; the package does not.
/// </summary>
public sealed class BakeMap
{
    private readonly int[] triangles;
    private readonly float[] coordinates;

    internal BakeMap(int width, int height, int[] triangles, float[] coordinates)
    {
        Width = width;
        Height = height;
        this.triangles = triangles;
        this.coordinates = coordinates;
    }

    /// <summary>Gets the width in pixels.</summary>
    public int Width { get; }

    /// <summary>Gets the height in pixels.</summary>
    public int Height { get; }

    /// <summary>Gets the covering triangle of every pixel, row 0 first; -1 where no triangle covers the pixel centre. Where UV triangles overlap, the lowest index wins.</summary>
    public ReadOnlySpan<int> Triangles => triangles;

    /// <summary>
    /// Gets six floats per pixel, row 0 first: u, v, du/dx, du/dy, dv/dx, dv/dy. The point is
    /// <c>u * V0 + v * V1 + (1 - u - v) * V2</c> of the triangle's corners and x, y are in pixels. All zero where uncovered.
    /// </summary>
    public ReadOnlySpan<float> Coordinates => coordinates;

    /// <summary>
    /// Gets the triangle of one pixel.
    /// </summary>
    /// <param name="x">The column.</param>
    /// <param name="y">The row, 0 being v = 0.</param>
    /// <returns>The triangle's index in the mesh, or -1 where no triangle covers the pixel centre.</returns>
    /// <exception cref="ArgumentOutOfRangeException">The pixel is outside the map.</exception>
    public int GetTriangle(int x, int y)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(x);
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(x, Width);
        ArgumentOutOfRangeException.ThrowIfNegative(y);
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(y, Height);
        return triangles[y * Width + x];
    }
}
