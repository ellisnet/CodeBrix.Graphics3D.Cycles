// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles.Nodes;

/// <summary>
/// A linkable input socket of a shader graph node, the target of <c>ShaderNode.Connect</c>. The
/// generated node classes expose one per linkable input (for example <c>DiffuseBsdfNode.ColorInput</c>).
/// </summary>
/// <param name="Node">The shader graph node that owns the socket.</param>
/// <param name="Socket">The socket's link name (its registry UI name, for example <c>Color</c>).</param>
public readonly record struct ShaderInput(ShaderGraphNode Node, string Socket);
