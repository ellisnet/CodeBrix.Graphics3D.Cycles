// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles.Generated.Geometry;

public partial class MeshNode
{
    /// <summary>
    /// Replaces this mesh's geometry with a subdivision cage: polygon faces that each render tessellates with Cycles'
    /// adaptive dicing (<see cref="SubdDicingRate"/>, <see cref="SubdMaxLevel"/>, <see cref="SubdAdaptiveSpace"/>; pixel
    /// space measures in the render camera). Set <see cref="SubdivisionType"/> to <see cref="MeshSubdivisionType.Linear"/>;
    /// Catmull-Clark needs OpenSubdiv, which this library does not include. Every face starts on material 0, flat
    /// (<see cref="SubdShader"/>, <see cref="SubdSmooth"/>).
    /// </summary>
    /// <param name="positions">Cage positions as x, y, z triples.</param>
    /// <param name="faceCorners">The corner count of each face (at least three).</param>
    /// <param name="corners">Every face's vertex indices in order; as many as the face corner counts add up to.</param>
    /// <exception cref="ArgumentException">The positions are not triples.</exception>
    /// <exception cref="NativeFailure">A count or index is invalid or a position is nonfinite.</exception>
    public void SetSubdivisionCage(ReadOnlySpan<float> positions, ReadOnlySpan<uint> faceCorners, ReadOnlySpan<uint> corners)
        => Scene.Host.SubdivisionMesh(Handle, positions, faceCorners, corners);
}
