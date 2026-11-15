// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// One UDIM tile of an image texture (provisional): the image bound at tile <paramref name="Number"/>, passed to
/// <see cref="Generated.Shaders.ImageTextureNode.BindTiles"/>. The tile covering UV [u, u + 1) x [v, v + 1) is
/// 1001 + 10 * v + u, with u from 0 to 9 (1001 is the unit square, 1002 the square to its right, 1011 the one above).
/// </summary>
/// <param name="Number">The tile number, at least 1001.</param>
/// <param name="Image">The image of the tile, created by the scene of the texture.</param>
public readonly record struct UdimTile(int Number, CyclesImage Image);
