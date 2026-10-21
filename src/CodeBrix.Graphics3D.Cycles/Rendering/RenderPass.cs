// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles.Rendering;

/// <summary>
/// Identifies the kind of pass a <see cref="FilmBuffer"/> holds; <see cref="FilmBuffer.PassName"/>
/// carries the exact name.
/// </summary>
public enum RenderPass
{
    /// <summary>The combined beauty pass: scene-linear RGBA, alpha is coverage. Every render writes it.</summary>
    Combined = 0,

    /// <summary>
    /// A named pass or AOV enabled with a pass node (<see cref="CyclesScene.AddPass"/>) and read with
    /// <see cref="CyclesScene.ReadPass"/>. The WebGPU device writes only <see cref="Combined"/> today.
    /// </summary>
    Named = 1
}
