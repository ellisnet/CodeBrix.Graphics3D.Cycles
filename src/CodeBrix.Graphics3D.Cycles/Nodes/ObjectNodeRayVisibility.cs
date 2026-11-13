// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles.Generated.Scene;

public sealed partial class ObjectNode
{
    /// <summary>
    /// Gets or sets the ray types this object is visible to: the <c>visibility</c> socket (<see cref="Visibility"/>) as
    /// flags. Default <see cref="RayVisibility.All"/>.
    /// </summary>
    public RayVisibility RayVisibility { get => (RayVisibility)Visibility; set => Visibility = (uint)value; }
}
