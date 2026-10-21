// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles.Nodes;

/// <summary>
/// How an instance of a native node type is obtained through the native host ABI.
/// </summary>
public enum NodeCreation
{
    /// <summary>The scene owns exactly one (camera, background, film, integrator); get it from <see cref="CyclesScene"/>.</summary>
    Singleton,

    /// <summary>Created in the scene with <see cref="CyclesScene.Create{T}"/>.</summary>
    Scene,

    /// <summary>Added to a shader graph with <c>ShaderNode.Add&lt;T&gt;()</c>.</summary>
    ShaderGraph,

    /// <summary>The output node every shader graph owns; get it from <c>ShaderNode.Output</c>.</summary>
    GraphOutput,

    /// <summary>
    /// Not created directly by the current native host ABI: an abstract base type, or a type the host
    /// manages internally. It is catalogued for completeness.
    /// </summary>
    Unavailable
}
