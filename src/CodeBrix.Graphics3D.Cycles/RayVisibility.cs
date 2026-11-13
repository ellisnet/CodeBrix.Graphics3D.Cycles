// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// The ray types an object or the world is visible to: the bits of the native <c>visibility</c> socket of
/// <see cref="Generated.Scene.ObjectNode"/> and <see cref="Generated.Scene.BackgroundNode"/>. The values are
/// Cycles' path ray visibility bits.
/// </summary>
[Flags]
public enum RayVisibility : uint
{
    /// <summary>Visible to no ray.</summary>
    None = 0,

    /// <summary>Camera rays.</summary>
    Camera = 1u << 0,

    /// <summary>Transmission rays (through refraction and transparency).</summary>
    Transmit = 1u << 1,

    /// <summary>Diffuse bounce rays.</summary>
    Diffuse = 1u << 2,

    /// <summary>Glossy bounce rays.</summary>
    Glossy = 1u << 3,

    /// <summary>Volume scatter rays.</summary>
    VolumeScatter = 1u << 4,

    /// <summary>Opaque shadow rays.</summary>
    ShadowOpaque = 1u << 5,

    /// <summary>Transparent shadow rays.</summary>
    ShadowTransparent = 1u << 6,

    /// <summary>Every shadow ray (<see cref="ShadowOpaque"/> and <see cref="ShadowTransparent"/>).</summary>
    Shadow = ShadowOpaque | ShadowTransparent,

    /// <summary>Rays of the Raycast shader node.</summary>
    Raycast = 1u << 7,

    /// <summary>Every ray type (the default of objects and the world).</summary>
    All = (1u << 8) - 1,
}
