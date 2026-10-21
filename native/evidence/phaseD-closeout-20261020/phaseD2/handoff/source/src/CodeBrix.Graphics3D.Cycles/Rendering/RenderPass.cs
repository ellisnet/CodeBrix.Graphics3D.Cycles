// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles.Rendering;

/// <summary>
/// Identifies the render pass a <see cref="FilmBuffer"/> holds. The native host currently returns the
/// combined pass only; further passes and AOVs join this enumeration when the native host exposes
/// pass readback.
/// </summary>
public enum RenderPass
{
    /// <summary>The combined beauty pass: scene-linear RGBA, alpha is coverage.</summary>
    Combined = 0
}
