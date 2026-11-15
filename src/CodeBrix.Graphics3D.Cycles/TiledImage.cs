// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// The mip levels of a tiled image (provisional; <see cref="CyclesScene.CreateTiledImage"/>), as Cycles' texture cache
/// asks an <see cref="ITiledImageReader"/> for them: a reader must produce exactly these sizes. They depend on the
/// image's tile size: pass the one given to <see cref="CyclesScene.CreateTiledImage"/>.
/// </summary>
public static class TiledImage
{
    /// <summary>
    /// The default side of the square tiles Cycles' texture cache loads (Cycles' block size, 64). A tiled image may choose
    /// another (provisional): a power of two from 64 (<see cref="MinTileSize"/>) to 128 on the Metal backend (Cycles'
    /// Metal tile pool of 64 tiles of side + 4 pixels is at most 16384 wide) and to <see cref="MaxTileSize"/> on the CPU
    /// backends.
    /// </summary>
    public const int TileSize = 64;

    /// <summary>The smallest tile side (provisional): Cycles loads smaller tiles in blocks of 64.</summary>
    public const int MinTileSize = 64;

    /// <summary>
    /// The largest tile side on any backend (provisional): 1024 on the CPU backends (Cycles allocates a pool of 64 tiles
    /// whole: 271 MB for byte4 tiles of 1024, 1.1 GB for float4); 128 on the Metal backend. The capability report states
    /// each backend's (<see cref="Diagnostics.BackendScope.MaxImageTileSize"/>).
    /// </summary>
    public const int MaxTileSize = 1024;

    /// <summary>
    /// Returns the number of mip levels of a tiled image: from level 0 to the first level whose sides are both at most
    /// <paramref name="tileSize"/> pixels; 1 for an image smaller than <paramref name="tileSize"/> in both sides, which
    /// loads whole.
    /// </summary>
    /// <param name="width">The width of level 0 in pixels.</param>
    /// <param name="height">The height of level 0 in pixels.</param>
    /// <param name="tileSize">The image's tile side (provisional; <see cref="TileSize"/> by default).</param>
    /// <returns>The level count.</returns>
    /// <exception cref="ArgumentOutOfRangeException">A side is zero, or <paramref name="tileSize"/> is not a power of two
    /// from <see cref="MinTileSize"/> to <see cref="MaxTileSize"/>.</exception>
    public static int LevelCount(uint width, uint height, int tileSize = TileSize)
    {
        ArgumentOutOfRangeException.ThrowIfZero(width);
        ArgumentOutOfRangeException.ThrowIfZero(height);
        if (tileSize < MinTileSize || tileSize > MaxTileSize || !int.IsPow2(tileSize))
            throw new ArgumentOutOfRangeException(nameof(tileSize), tileSize, $"A tile side is a power of two from {MinTileSize} to {MaxTileSize}.");
        if (width < tileSize && height < tileSize) return 1;
        for (int level = 0; ; level++)
        {
            (uint w, uint h) = Size(width, height, level);
            if (w <= tileSize && h <= tileSize) return level + 1;
        }
    }

    /// <summary>
    /// Returns the size of mip level <paramref name="level"/>: each side shifted right by the level, at least 1 pixel.
    /// </summary>
    /// <param name="width">The width of level 0 in pixels.</param>
    /// <param name="height">The height of level 0 in pixels.</param>
    /// <param name="level">The level, 0 to <see cref="LevelCount"/> - 1.</param>
    /// <param name="tileSize">The image's tile side (provisional; <see cref="TileSize"/> by default).</param>
    /// <returns>The level's width and height.</returns>
    /// <exception cref="ArgumentOutOfRangeException">A side is zero, the tile size is invalid, or the level is outside the
    /// image's levels.</exception>
    public static (uint Width, uint Height) LevelSize(uint width, uint height, int level, int tileSize = TileSize)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(level);
        ArgumentOutOfRangeException.ThrowIfGreaterThanOrEqual(level, LevelCount(width, height, tileSize));
        return Size(width, height, level);
    }

    private static (uint Width, uint Height) Size(uint width, uint height, int level) =>
        (level >= 32 ? 1u : Math.Max(1u, width >> level), level >= 32 ? 1u : Math.Max(1u, height >> level));
}
