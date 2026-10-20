// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// Options for geometry attribute calls. The values equal the <c>CBC_ATTR_STANDARD</c> and
/// <c>CBC_ATTR_SUBDIVISION</c> constants of the native host ABI.
/// </summary>
[Flags]
public enum AttributeFlags : uint
{
    /// <summary>A custom (named) attribute on the base geometry.</summary>
    None = 0,

    /// <summary>
    /// A standard attribute; the name selects it (for example <c>P</c>, <c>N</c>, <c>uv</c>,
    /// <c>generated</c>, <c>radius</c>, <c>tangent</c> or <c>tangent_sign</c>).
    /// </summary>
    Standard = 1,

    /// <summary>The attribute belongs to the subdivision attribute set.</summary>
    Subdivision = 2
}
