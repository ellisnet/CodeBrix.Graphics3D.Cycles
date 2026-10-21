// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// Socket property flags, as reported by <see cref="PropertyInfo"/> and the registry. The values
/// equal the <c>CBC_PROPERTY_*</c> constants of the native host ABI.
/// </summary>
[Flags]
public enum PropertyFlags : uint
{
    /// <summary>No flags.</summary>
    None = 0,

    /// <summary>The socket holds an array of elements.</summary>
    Array = 1,

    /// <summary>The socket can be the target of a shader graph link.</summary>
    Linkable = 2,

    /// <summary>The socket is internal to native Cycles.</summary>
    Internal = 4,

    /// <summary>The socket has a stored value that can be read.</summary>
    Readable = 8,

    /// <summary>The socket has a stored value that can be written.</summary>
    Writable = 16
}
