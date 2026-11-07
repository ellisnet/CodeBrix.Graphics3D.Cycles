// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles.Rendering;

/// <summary>Native bake options; the values are the native <c>CBC_BAKE_*</c> flag constants.</summary>
[Flags]
public enum BakeFlags : uint
{
    /// <summary>No options.</summary>
    None = 0,

    /// <summary>The view direction points at the scene camera instead of along the surface normal.</summary>
    UseCamera = 1,

    /// <summary>Light passes keep the surface colour instead of being divided by it.</summary>
    IncludeAlbedo = 2
}
