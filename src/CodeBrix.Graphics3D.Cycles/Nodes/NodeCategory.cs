// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles.Nodes;

/// <summary>
/// The category a native node type belongs to in the generated typed model.
/// </summary>
public enum NodeCategory
{
    /// <summary>A scene-level node: objects, shader graphs, particle systems and scene attributes.</summary>
    Scene,

    /// <summary>The scene camera singleton.</summary>
    Camera,

    /// <summary>The film singleton (exposure, filter, pass settings).</summary>
    Film,

    /// <summary>The integrator singleton (bounces, sampling, light tree, guiding).</summary>
    Integrator,

    /// <summary>The world background singleton.</summary>
    Background,

    /// <summary>A geometry type (mesh, hair, point cloud, volume) or the geometry base.</summary>
    Geometry,

    /// <summary>A light type or the light base.</summary>
    Light,

    /// <summary>A shader graph node (BSDFs, textures, math, the graph output and so on).</summary>
    Shader,

    /// <summary>A render output description (passes, buffer and denoising parameters).</summary>
    Output
}
