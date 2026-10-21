// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles.Nodes;

/// <summary>
/// One item of a native enumeration socket.
/// </summary>
/// <param name="Name">The native item name (what the string setters accept).</param>
/// <param name="Value">The native item value (what the integer getters return).</param>
public readonly record struct EnumEntry(string Name, int Value);
