// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// The stable value kind of a socket, as reported by <see cref="PropertyInfo"/> and the registry.
/// The values equal the <c>CBC_VALUE_*</c> constants of the native host ABI.
/// </summary>
public enum ValueKind : uint
{
    /// <summary>A value the C ABI does not expose.</summary>
    Unsupported = 0,

    /// <summary>A boolean (read and written as 0 or 1 integers).</summary>
    Boolean = 1,

    /// <summary>A 32-bit signed integer.</summary>
    Int = 2,

    /// <summary>A 32-bit unsigned integer.</summary>
    UInt = 3,

    /// <summary>A 64-bit unsigned integer.</summary>
    UInt64 = 4,

    /// <summary>A 32-bit float.</summary>
    Float = 5,

    /// <summary>An RGB color (three floats).</summary>
    Color = 6,

    /// <summary>A direction vector (three floats).</summary>
    Vector = 7,

    /// <summary>A position (three floats).</summary>
    Point = 8,

    /// <summary>A normal (three floats).</summary>
    Normal = 9,

    /// <summary>A two-component point (two floats).</summary>
    Point2 = 10,

    /// <summary>A UTF-8 string.</summary>
    String = 11,

    /// <summary>An enumeration (set by item name or value, read as its value).</summary>
    Enum = 12,

    /// <summary>A 3x4 row-major affine transform (twelve floats).</summary>
    Transform = 13,

    /// <summary>A reference to another node.</summary>
    Node = 14,

    /// <summary>A shader closure; it has no stored value and is only a link target or source.</summary>
    Closure = 15,

    /// <summary>A four-component float value (geometry attributes only).</summary>
    Float4 = 16,

    /// <summary>A four-byte value (geometry attributes only).</summary>
    Byte4 = 17
}
