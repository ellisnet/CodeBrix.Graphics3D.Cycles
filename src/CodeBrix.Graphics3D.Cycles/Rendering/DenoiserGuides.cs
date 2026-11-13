// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;

namespace CodeBrix.Graphics3D.Cycles.Rendering;

/// <summary>
/// The guide passes the denoiser uses: the bits of the native integrator socket <c>denoiser_passes</c>
/// (<see cref="Generated.Scene.IntegratorNode.DenoiserGuides"/>). The values are Cycles' denoiser pass bits. Open Image
/// Denoise reads <see cref="Albedo"/> and <see cref="Normal"/> (the default); the other bits are read only by
/// denoisers this library does not include.
/// </summary>
[Flags]
public enum DenoiserGuides
{
    /// <summary>No guide passes: the denoiser works from the noisy colour alone.</summary>
    None = 0,

    /// <summary>The albedo guide.</summary>
    Albedo = 1 << 0,

    /// <summary>The specular albedo guide.</summary>
    SpecularAlbedo = 1 << 1,

    /// <summary>The normal guide (used together with <see cref="Albedo"/> by Open Image Denoise).</summary>
    Normal = 1 << 2,

    /// <summary>The roughness guide.</summary>
    Roughness = 1 << 3,

    /// <summary>The depth guide.</summary>
    Depth = 1 << 4,

    /// <summary>The motion guide.</summary>
    Motion = 1 << 5,

    /// <summary>The backward motion guide.</summary>
    BackwardMotion = 1 << 6,

    /// <summary>The specular motion guide.</summary>
    SpecularMotion = 1 << 7,
}
