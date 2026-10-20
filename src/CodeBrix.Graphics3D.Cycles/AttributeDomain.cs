// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// The geometry element an attribute is stored per. The values equal the <c>CBC_ATTR_*</c>
/// domain constants of the native host ABI.
/// </summary>
public enum AttributeDomain : uint
{
    /// <summary>One value for the whole mesh.</summary>
    Mesh = 1,

    /// <summary>One value per face.</summary>
    Face = 2,

    /// <summary>One value per vertex.</summary>
    Vertex = 3,

    /// <summary>One value per face corner.</summary>
    Corner = 4,

    /// <summary>One value per curve.</summary>
    Curve = 5,

    /// <summary>One value per curve key.</summary>
    CurveKey = 6,

    /// <summary>One packed normal per vertex.</summary>
    VertexNormal = 7,

    /// <summary>One packed normal per face corner.</summary>
    CornerNormal = 8,

    /// <summary>Four bytes per face corner.</summary>
    CornerByte = 9,

    /// <summary>One packed normal per curve key.</summary>
    CurveKeyNormal = 10
}
