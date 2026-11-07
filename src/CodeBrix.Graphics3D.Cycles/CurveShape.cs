// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// How a hair geometry's curves are rendered. The values equal the <c>CBC_CURVE_*</c> constants of
/// the native host ABI and Cycles' curve shape types.
/// </summary>
public enum CurveShape : uint
{
    /// <summary>Flat ribbons facing the ray, tessellated by <see cref="CyclesScene.SetCurveSubdivisions"/>. The default.</summary>
    Ribbon = 0,

    /// <summary>Thick Catmull-Rom curves with round cross sections.</summary>
    Thick = 1,

    /// <summary>Thick linear segments: cones between the keys, with spheres at the joints.</summary>
    ThickLinear = 2,
}
