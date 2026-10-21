// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System.Collections.Generic;

namespace CodeBrix.Graphics3D.Cycles.Nodes;

/// <summary>
/// One input or output socket of a native node type, as described by the native registry.
/// </summary>
/// <param name="Name">The native socket name, used by the socket getters and setters.</param>
/// <param name="UiName">The socket's display name, used by shader graph links.</param>
/// <param name="TypeName">The native socket type name (for example <c>float</c>, <c>color</c>, <c>array_int</c> or <c>closure</c>).</param>
/// <param name="Kind">The stable value kind.</param>
/// <param name="Components">The flattened component count per element.</param>
/// <param name="Flags">The property flags (always <see cref="PropertyFlags.None"/> for outputs).</param>
/// <param name="NodeTypeName">For node references, the native type the referenced node must be; otherwise <see langword="null"/>.</param>
/// <param name="EnumValues">For enumeration sockets, the items; otherwise empty.</param>
public sealed record SocketInfo(string Name, string UiName, string TypeName, ValueKind Kind, uint Components,
                                PropertyFlags Flags, string NodeTypeName, IReadOnlyList<EnumEntry> EnumValues);
