// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles.Generated.Geometry;

public sealed partial class PointCloudNode
{
    /// <summary>
    /// Replaces this point cloud's points and removes its motion. Further point attributes follow this call,
    /// because their element counts derive from the points. Every point starts on material 0 (the
    /// <see cref="Shader"/> socket).
    /// </summary>
    /// <param name="positions">The positions as x, y, z triples.</param>
    /// <param name="radii">One radius per point.</param>
    /// <exception cref="ArgumentException">The positions are not triples or the radius count differs from the point count.</exception>
    /// <exception cref="NativeFailure">A value is nonfinite or a radius is negative.</exception>
    public void SetPoints(ReadOnlySpan<float> positions, ReadOnlySpan<float> radii) => Scene.Host.Points(Handle, positions, radii);

    /// <summary>
    /// Sets this point cloud's deformation motion: <paramref name="steps"/> time-ordered sets of positions and
    /// radii, the center set equal to the points bit for bit.
    /// </summary>
    /// <param name="steps">The motion step count, odd from 3 to 129.</param>
    /// <param name="positions">Time-ordered positions as x, y, z triples.</param>
    /// <param name="radii">One time-ordered radius per position.</param>
    /// <exception cref="ArgumentException">The positions are not triples or the radius count differs from the position count.</exception>
    /// <exception cref="NativeFailure">The step count or the array sizes are invalid, a value is nonfinite, or the center step differs from the points.</exception>
    public void SetMotion(uint steps, ReadOnlySpan<float> positions, ReadOnlySpan<float> radii) => Scene.Host.PointMotion(Handle, steps, positions, radii);

    /// <summary>
    /// Removes this point cloud's deformation motion.
    /// </summary>
    public void RemoveMotion() => Scene.Host.PointMotion(Handle, 1, [], []);
}
