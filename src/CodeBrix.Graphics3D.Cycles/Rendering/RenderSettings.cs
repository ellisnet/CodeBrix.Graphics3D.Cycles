// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles.Rendering;

/// <summary>
/// The size and sample budget of one render. Scene settings (camera, film, integrator) live on the
/// scene's typed singletons, not here; passes beyond Combined are pass nodes in the scene
/// (<see cref="CyclesScene.AddPass"/>).
/// </summary>
/// <param name="Width">The image width in pixels.</param>
/// <param name="Height">The image height in pixels.</param>
/// <param name="Samples">The number of samples per pixel.</param>
public readonly record struct RenderSettings(uint Width, uint Height, uint Samples)
{

    /// <summary>
    /// Gets the number of floats one RGBA film of this size holds (<c>Width * Height * 4</c>).
    /// </summary>
    public long FloatCount => (long)Width * Height * FilmBuffer.Channels;
}
