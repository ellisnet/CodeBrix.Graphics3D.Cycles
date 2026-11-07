// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles.Generated.Scene;

public sealed partial class ParticleSystemNode
{
    /// <summary>
    /// Replaces this system's particles, which the Particle Info shader node reads for objects whose
    /// <c>particle_system</c> socket names this system and whose <c>particle_index</c> socket names a particle in it.
    /// </summary>
    /// <param name="particles">The particles.</param>
    /// <exception cref="NativeFailure">A value is nonfinite.</exception>
    public void SetParticles(ReadOnlySpan<Particle> particles) => Scene.Host.Particles(Handle, particles);
}
