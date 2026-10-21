// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;

namespace CodeBrix.Graphics3D.Cycles.Nodes;

/// <summary>
/// One native node type: its registry metadata and the generated class that represents it.
/// </summary>
public sealed class NodeTypeInfo
{
    internal NodeTypeInfo(string name, NodeCategory category, NodeCreation creation, string baseName, bool hasFactory,
                          Type clrType, Func<CyclesScene, Node, ShaderNode, CyclesNode> factory,
                          IReadOnlyList<SocketInfo> inputs, IReadOnlyList<SocketInfo> outputs)
    {
        Name = name;
        Category = category;
        Creation = creation;
        BaseName = baseName;
        HasFactory = hasFactory;
        ClrType = clrType;
        Factory = factory;
        Inputs = inputs;
        Outputs = outputs;
    }

    /// <summary>Gets the native registry type name (for example <c>mesh</c> or <c>diffuse_bsdf</c>).</summary>
    public string Name { get; }

    /// <summary>Gets the type's category.</summary>
    public NodeCategory Category { get; }

    /// <summary>Gets how an instance is obtained through the native host ABI.</summary>
    public NodeCreation Creation { get; }

    /// <summary>Gets the native base type name, or <see langword="null"/> for a root type.</summary>
    public string BaseName { get; }

    /// <summary>Gets a value indicating whether native Cycles registers a factory for the type.</summary>
    public bool HasFactory { get; }

    /// <summary>Gets the generated class that represents the type.</summary>
    public Type ClrType { get; }

    /// <summary>Gets the input sockets, including those inherited from the base type, in registry order.</summary>
    public IReadOnlyList<SocketInfo> Inputs { get; }

    /// <summary>Gets the output sockets (shader graph nodes only), in registry order.</summary>
    public IReadOnlyList<SocketInfo> Outputs { get; }

    internal Func<CyclesScene, Node, ShaderNode, CyclesNode> Factory { get; }

    /// <summary>Returns the native type name.</summary>
    /// <returns>The native type name.</returns>
    public override string ToString() => Name;
}
