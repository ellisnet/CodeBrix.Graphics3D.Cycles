// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Numerics;

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// One particle of a particle system: the values the Particle Info shader node returns.
/// </summary>
/// <param name="Index">The particle's index output (its random output is a hash of this).</param>
/// <param name="Age">The age output.</param>
/// <param name="Lifetime">The lifetime output.</param>
/// <param name="Location">The location output.</param>
/// <param name="Rotation">The rotation as a quaternion (stored; the node has no rotation output).</param>
/// <param name="Size">The size output.</param>
/// <param name="Velocity">The velocity output.</param>
/// <param name="AngularVelocity">The angular velocity output.</param>
public readonly record struct Particle(uint Index, float Age, float Lifetime, Vector3 Location, Vector4 Rotation, float Size, Vector3 Velocity, Vector3 AngularVelocity)
{
    internal const int FloatCount = 16;

    internal void CopyValuesTo(Span<float> values)
    {
        values[0] = Age;
        values[1] = Lifetime;
        values[2] = Location.X;
        values[3] = Location.Y;
        values[4] = Location.Z;
        values[5] = Rotation.X;
        values[6] = Rotation.Y;
        values[7] = Rotation.Z;
        values[8] = Rotation.W;
        values[9] = Size;
        values[10] = Velocity.X;
        values[11] = Velocity.Y;
        values[12] = Velocity.Z;
        values[13] = AngularVelocity.X;
        values[14] = AngularVelocity.Y;
        values[15] = AngularVelocity.Z;
    }
}
