// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles.Rendering;

/// <summary>
/// The stage a <see cref="RenderProgress"/> report describes.
/// </summary>
public enum RenderStage
{
    /// <summary>The render was submitted to the native host.</summary>
    Starting,

    /// <summary>The render is running (scene update, compilation or sampling).</summary>
    Rendering,

    /// <summary>The render finished and the film was written.</summary>
    Completed,

    /// <summary>The render was cancelled; the destination was not changed.</summary>
    Cancelled,

    /// <summary>The render failed; the destination was not changed.</summary>
    Failed
}
