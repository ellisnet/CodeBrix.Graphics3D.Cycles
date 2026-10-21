// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using CodeBrix.Graphics3D.Cycles.Generated.Scene;

namespace CodeBrix.Graphics3D.Cycles.Nodes;

/// <summary>
/// The base class of every generated shader graph node: a node that lives inside one shader graph
/// (a <see cref="ShaderNode"/>) and links to other nodes of the same graph.
/// </summary>
public abstract class ShaderGraphNode : CyclesNode
{
    internal ShaderGraphNode(CyclesScene scene, Node handle, ShaderNode graph) : base(scene, handle)
    {
        Graph = graph;
    }

    /// <summary>
    /// Gets the shader graph this node belongs to.
    /// </summary>
    public ShaderNode Graph { get; }
}
