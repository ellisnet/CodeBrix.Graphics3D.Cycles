// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles.Rendering;

/// <summary>
/// The output size, sample budget and kind of one bake. Pixel (x, y) of the output is sampled at UV
/// <c>((x + 0.5) / Width, (y + 0.5) / Height)</c>; row 0 is at v = 0, the same row order as the images
/// the scene takes.
/// </summary>
/// <param name="Width">The output width in pixels.</param>
/// <param name="Height">The output height in pixels.</param>
/// <param name="Samples">The number of samples per pixel (<see cref="BakeType.Position"/> always takes one).</param>
/// <param name="Type">What the bake writes.</param>
public readonly record struct BakeSettings(uint Width, uint Height, uint Samples, BakeType Type)
{
    /// <summary>
    /// Gets the name of the float2 corner or vertex attribute that holds the UV layout, or
    /// <see langword="null"/> for the mesh's standard <c>uv</c> attribute.
    /// </summary>
    public string UvAttribute { get; init; }

    /// <summary>Gets the AOV pass name, required by <see cref="BakeType.AovColor"/> and <see cref="BakeType.AovValue"/> and refused by the others.</summary>
    public string AovName { get; init; }

    /// <summary>Gets a value indicating whether the view direction points at the scene camera instead of along the surface normal.</summary>
    public bool UseCamera { get; init; }

    /// <summary>Gets a value indicating whether light passes keep the surface colour instead of being divided by it.</summary>
    public bool IncludeAlbedo { get; init; }

    /// <summary>Gets the floats one pixel of this bake holds.</summary>
    public int Components => Type switch
    {
        BakeType.Combined or BakeType.AovColor => 4,
        BakeType.Roughness or BakeType.ObjectId or BakeType.MaterialId or BakeType.Depth or BakeType.Mist or BakeType.AovValue => 1,
        _ => 3
    };
}
