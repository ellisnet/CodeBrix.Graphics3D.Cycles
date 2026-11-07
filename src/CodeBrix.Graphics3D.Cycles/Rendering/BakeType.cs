// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles.Rendering;

/// <summary>
/// What a bake writes (<see cref="CyclesScene.Bake(Generated.Scene.ObjectNode, BakeSettings)"/>), one per bake.
/// The values are the native <c>CBC_BAKE_*</c> constants.
/// </summary>
public enum BakeType
{
    /// <summary>The full light of the surface: four floats (RGBA), alpha 0 on pixels no triangle covers and 1 on covered pixels.</summary>
    Combined = 1,

    /// <summary>Light the surface itself emits: three floats.</summary>
    Emission = 2,

    /// <summary>
    /// The world, evaluated in the direction of each surface point as seen from the world origin (not the surface's own shading):
    /// three floats. A closed mesh around the origin bakes the environment onto that mesh's layout.
    /// </summary>
    Background = 3,

    /// <summary>Ambient occlusion, up to the integrator's AO distance: three floats, 1 where unoccluded.</summary>
    AmbientOcclusion = 4,

    /// <summary>Direct and indirect diffuse light, divided by the diffuse colour unless <see cref="BakeSettings.IncludeAlbedo"/>: three floats.</summary>
    Diffuse = 5,

    /// <summary>Direct diffuse light, divided by the diffuse colour unless <see cref="BakeSettings.IncludeAlbedo"/>: three floats.</summary>
    DiffuseDirect = 6,

    /// <summary>Indirect diffuse light, divided by the diffuse colour unless <see cref="BakeSettings.IncludeAlbedo"/>: three floats.</summary>
    DiffuseIndirect = 7,

    /// <summary>The diffuse colour: three floats.</summary>
    DiffuseColor = 8,

    /// <summary>Direct and indirect glossy light, divided by the glossy colour unless <see cref="BakeSettings.IncludeAlbedo"/>: three floats.</summary>
    Glossy = 9,

    /// <summary>Direct glossy light, divided by the glossy colour unless <see cref="BakeSettings.IncludeAlbedo"/>: three floats.</summary>
    GlossyDirect = 10,

    /// <summary>Indirect glossy light, divided by the glossy colour unless <see cref="BakeSettings.IncludeAlbedo"/>: three floats.</summary>
    GlossyIndirect = 11,

    /// <summary>The glossy colour: three floats.</summary>
    GlossyColor = 12,

    /// <summary>Direct and indirect transmitted light, divided by the transmission colour unless <see cref="BakeSettings.IncludeAlbedo"/>: three floats.</summary>
    Transmission = 13,

    /// <summary>Direct transmitted light, divided by the transmission colour unless <see cref="BakeSettings.IncludeAlbedo"/>: three floats.</summary>
    TransmissionDirect = 14,

    /// <summary>Indirect transmitted light, divided by the transmission colour unless <see cref="BakeSettings.IncludeAlbedo"/>: three floats.</summary>
    TransmissionIndirect = 15,

    /// <summary>The transmission colour: three floats.</summary>
    TransmissionColor = 16,

    /// <summary>World-space position at the texel centre: three floats. Always one sample, whatever <see cref="BakeSettings.Samples"/> says.</summary>
    Position = 17,

    /// <summary>World-space shading normal: three floats. Tangent and object space are not provided; convert in the application.</summary>
    Normal = 18,

    /// <summary>The mesh's standard <c>uv</c> attribute: three floats, the third 1 on covered pixels.</summary>
    Uv = 19,

    /// <summary>The surface's roughness: one float.</summary>
    Roughness = 20,

    /// <summary>The object's <c>PassId</c>: one float.</summary>
    ObjectId = 21,

    /// <summary>The shader's <c>PassId</c>: one float.</summary>
    MaterialId = 22,

    /// <summary>Distance along the camera axis: one float, 1e10 on pixels no triangle covers.</summary>
    Depth = 23,

    /// <summary>Mist from the film's mist sockets: one float, 1 on pixels no triangle covers.</summary>
    Mist = 24,

    /// <summary>An AOV colour: four floats. The scene must have an AOV colour pass named <see cref="BakeSettings.AovName"/>.</summary>
    AovColor = 25,

    /// <summary>An AOV value: one float. The scene must have an AOV value pass named <see cref="BakeSettings.AovName"/>.</summary>
    AovValue = 26
}
