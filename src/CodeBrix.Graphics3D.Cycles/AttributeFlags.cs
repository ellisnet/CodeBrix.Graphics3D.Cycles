// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// Options for geometry attribute calls. The values equal the <c>CBC_ATTR_STANDARD</c>,
/// <c>CBC_ATTR_SUBDIVISION</c> and <c>CBC_ATTR_SPLAT_BOUNDING_RADIUS</c> constants of the native host ABI.
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
    Subdivision = 2,

    /// <summary>
    /// Reads only (<c>GetAttribute</c>): the bounding radius Cycles computed for each splat of a Gaussian
    /// splat cloud (<c>RenderAs</c> <c>GaussianSplats</c>) at the last render or bake, from its opacity and scale, instead
    /// of the authored radius that a read without this flag returns. The name must be <c>radius</c>. A
    /// <see cref="CyclesException"/> with <see cref="NativeStatus.NotReady"/> until a render has computed it for the cloud
    /// as it is (before the first render and after an edit of the cloud); <see cref="NativeStatus.Invalid"/> for any other
    /// geometry.
    /// </summary>
    SplatBoundingRadius = 4
}
