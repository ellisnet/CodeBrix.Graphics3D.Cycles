// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles.Nodes;

/// <summary>
/// An output socket of a shader graph node, the source of <c>ShaderNode.Connect</c>. The generated
/// node classes expose one per output (for example <c>DiffuseBsdfNode.BsdfOutput</c>).
/// </summary>
/// <param name="Node">The shader graph node that owns the socket.</param>
/// <param name="Socket">The socket's link name (its registry UI name, for example <c>BSDF</c>).</param>
public readonly record struct ShaderOutput(ShaderGraphNode Node, string Socket);
