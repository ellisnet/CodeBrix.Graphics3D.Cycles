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
    Byte4 = 17
}
