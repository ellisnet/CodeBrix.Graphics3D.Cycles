// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// Supplies the pixels of a tiled image (provisional; <see cref="CyclesScene.CreateTiledImage"/>, Cycles' texture cache):
/// a render loads only the tiles (64 x 64, or the image's tile size) and mip levels it samples, and asks for each through
/// <see cref="ReadTile"/>.
/// The reader does the file I/O and decoding itself, for example with CodeBrix.Imaging; this library reads no image
/// files.
/// </summary>
/// <remarks>
/// Level 0 is the image; level L is <see cref="TiledImage.LevelSize"/> of it, and the levels run from 0 to
/// <see cref="TiledImage.LevelCount"/> - 1 (the first level whose sides are both at most the tile size, 64 by default; an
/// image smaller than its tile size in both sides has level 0 only). The pixels of every level are the reader's:
/// Cycles' own levels are 2 x 2 box reductions of the level above.
/// <para>
/// Threading: <see cref="ReadTile"/> runs on native render threads, only while a render or bake of the scene runs, and one
/// call at a time per scene (never concurrently with itself or with the scene's other tiled images). It must not call
/// the scene, its host, its nodes or its images (the native host refuses re-entrant calls), and must not wait for the
/// thread that renders. A reader that throws fails the render or bake: it then throws a <see cref="CyclesException"/>
/// (<see cref="NativeStatus.Failed"/>) whose message names the image, the level and the rectangle and whose
/// <see cref="Exception.InnerException"/> is the reader's exception. A cancelled render stops reading after the call in
/// progress. The scene keeps the reader alive until the image is released, unbound and freed, or the scene is disposed.
/// </para>
/// </remarks>
public interface ITiledImageReader
{
    /// <summary>
    /// Writes one rectangle of one mip level into <paramref name="destination"/>: <paramref name="width"/> x
    /// <paramref name="height"/> pixels starting at column <paramref name="x"/> and row <paramref name="y"/> of the level,
    /// rows bottom first (row y at v = y / level height, as for <see cref="CyclesScene.CreateImage{T}"/>), each
    /// <paramref name="rowBytes"/> bytes apart, tightly packed in the image's <see cref="ImageFormat"/>.
    /// </summary>
    /// <param name="level">The mip level, 0 to <see cref="TiledImage.LevelCount"/> - 1.</param>
    /// <param name="x">The first column, within the level.</param>
    /// <param name="y">The first (bottom) row, within the level.</param>
    /// <param name="width">The number of columns.</param>
    /// <param name="height">The number of rows.</param>
    /// <param name="destination">The native buffer, valid only during the call.</param>
    /// <param name="rowBytes">The distance between rows in bytes.</param>
    void ReadTile(int level, int x, int y, int width, int height, Span<byte> destination, int rowBytes);
}
