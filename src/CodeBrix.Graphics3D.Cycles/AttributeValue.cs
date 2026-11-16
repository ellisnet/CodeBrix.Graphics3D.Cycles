// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// The element type of a geometry attribute. The values equal the matching <c>CBC_VALUE_*</c>
/// value kinds of the native host ABI.
/// </summary>
public enum AttributeValue : uint
{
    /// <summary>One float per element.</summary>
    Float = 5,

    /// <summary>An RGB color, three floats per element.</summary>
    Color = 6,

    /// <summary>A direction vector, three floats per element.</summary>
    Vector = 7,

    /// <summary>A position, three floats per element.</summary>
    Point = 8,

    /// <summary>A normal, three floats per element.</summary>
    Normal = 9,

    /// <summary>A two-component value such as a UV coordinate, two floats per element.</summary>
    Point2 = 10,

    /// <summary>A row-major 3x4 transform, twelve floats per element.</summary>
    Transform = 13,

    /// <summary>A four-component float value per element.</summary>
    Float4 = 16,

    /// <summary>Four bytes per element, uploaded with <see cref="CyclesHost.SetAttributeBytes"/>.</summary>
    Byte4 = 17,

    /// <summary>
    /// A quaternion, four floats per element in Cycles' order w, x, y, z, stored and read back exactly. Accepted on meshes
    /// and point clouds. An Attribute node returns w, x, y as its Color and Vector, their average as Fac and z as Alpha.
    /// Provisional: the native ABI marks this value kind provisional.
    /// </summary>
    Quaternion = 18,

    /// <summary>
    /// The spherical-harmonics coefficients of bands 1 to 3 (band 0 is not included), 45 floats per element: 15 RGB
    /// coefficients in Cycles' order (L1 M-1, L1 M0, L1 M1, L2 M-2, ..., L3 M3), each in [-1, 1]. Values outside that
    /// range are refused (<see cref="NativeStatus.Invalid"/>), never clipped. Cycles stores each coefficient at 8 bits
    /// over [-1, 1], so a read returns the stored value, within 1/255 of the value set. Accepted on meshes and point
    /// clouds, not as a subdivision attribute. An Attribute node finds it and returns black with alpha 1 (Cycles has no
    /// shader reading of it). Provisional: the native ABI marks this value kind provisional.
    /// </summary>
    SphericalHarmonicsRest = 19
}
