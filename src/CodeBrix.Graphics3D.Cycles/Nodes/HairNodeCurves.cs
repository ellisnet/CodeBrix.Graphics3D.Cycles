// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles.Generated.Geometry;

public sealed partial class HairNode
{
    /// <summary>
    /// Replaces this hair geometry's curves. Further curve attributes follow this call, because
    /// their element counts derive from the curves. Every curve starts on material 0 (the
    /// <see cref="CurveShader"/> socket).
    /// </summary>
    /// <param name="positions">The key positions as x, y, z triples.</param>
    /// <param name="radii">One radius per key.</param>
    /// <param name="firstKeys">The first key of each curve: the first is 0 and they strictly increase; a curve runs to the next one's first key.</param>
    /// <exception cref="ArgumentException">The positions are not triples or the radius count differs from the key count.</exception>
    /// <exception cref="NativeFailure">A value is nonfinite, a radius is negative or the curve ranges are invalid.</exception>
    public void SetCurves(ReadOnlySpan<float> positions, ReadOnlySpan<float> radii, ReadOnlySpan<uint> firstKeys) =>
        Scene.Host.Curves(Handle, positions, radii, firstKeys);

    /// <summary>
    /// Sets how this geometry's curves are rendered. The default is <see cref="CurveShape.Ribbon"/>.
    /// </summary>
    /// <param name="shape">The curve shape.</param>
    public void SetShape(CurveShape shape) => Scene.Host.SetCurveShape(Handle, shape);
}
