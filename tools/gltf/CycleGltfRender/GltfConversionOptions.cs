// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System.Numerics;

namespace CodeBrix.Graphics3D.Cycles.GltfRender;

/// <summary>
/// Options for <see cref="GltfSceneConverter"/>.
/// </summary>
public sealed class GltfConversionOptions
{
    /// <summary>
    /// Gets or sets the film width the camera is framed for (pixels). Default 64.
    /// </summary>
    public uint Width { get; set; } = 64;

    /// <summary>
    /// Gets or sets the film height the camera is framed for (pixels). Default 48.
    /// </summary>
    public uint Height { get; set; } = 48;

    /// <summary>
    /// Gets or sets the index (in scene-graph order) of the glTF camera to use, or -1 to use the
    /// first perspective camera in the file. When the file has no usable perspective camera, a
    /// default camera framing the scene bounds is created. Default -1.
    /// </summary>
    public int CameraIndex { get; set; } = -1;

    /// <summary>
    /// Gets or sets a value indicating whether a default camera framing the scene bounds is used
    /// even when the file has cameras. Default false.
    /// </summary>
    public bool ForceDefaultCamera { get; set; }

    /// <summary>
    /// Gets or sets the world background colour (scene-linear RGB). Default (0.25, 0.3, 0.4).
    /// </summary>
    public Vector3 BackgroundColor { get; set; } = new(.25f, .3f, .4f);

    /// <summary>
    /// Gets or sets the world background strength. Default 1.
    /// </summary>
    public float BackgroundStrength { get; set; } = 1f;

    /// <summary>
    /// Gets or sets the strength of the default sun added when the file has no
    /// KHR_lights_punctual lights. Default 3.
    /// </summary>
    public float DefaultSunStrength { get; set; } = 3f;

    /// <summary>
    /// Gets or sets a value indicating whether the deterministic integrator settings the WebGPU
    /// backend requires (no light tree, denoising, adaptive sampling or path guiding; tabulated
    /// Sobol sampling; four bounces) are applied. Default true. Leave it on for GPU renders and for
    /// CPU/GPU comparisons.
    /// </summary>
    public bool ApplyDeterministicIntegrator { get; set; } = true;

    /// <summary>
    /// Gets or sets how glTF materials become Cycles shader graphs. Default
    /// <see cref="GltfMaterialMapping.Closures"/> (factors only).
    /// </summary>
    public GltfMaterialMapping MaterialMapping { get; set; } = GltfMaterialMapping.Closures;

    /// <summary>
    /// Gets or sets the decoded glTF images (<see cref="TextureSet.Decode"/> or <see cref="TextureSet.Load"/>) the
    /// <see cref="GltfMaterialMapping.Blender"/> mapping samples, or null for factors only.
    /// Default null.
    /// </summary>
    public TextureSet Textures { get; set; }
}

/// <summary>
/// How <see cref="GltfSceneConverter"/> turns glTF materials into Cycles shader graphs.
/// </summary>
public enum GltfMaterialMapping
{
    /// <summary>
    /// Factors only, as Diffuse, Glossy, Emission and Transparent closures mixed by the metallic
    /// factor and alpha (the original example mapping). Textures are ignored with a warning.
    /// </summary>
    Closures = 0,

    /// <summary>
    /// The node graph Blender's glTF importer builds: a Principled BSDF fed by image textures
    /// (base colour sRGB x factor, metallic B and roughness G x factors, a tangent-space normal
    /// map using the glTF TANGENT), with occlusion not applied. Needs decoded textures for
    /// textured channels (<see cref="GltfConversionOptions.Textures"/>).
    /// </summary>
    Blender = 1,
}
