// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System.Collections.Generic;
using System.Numerics;

namespace CodeBrix.Graphics3D.Cycles.GltfRender;

/// <summary>
/// What <see cref="GltfSceneConverter"/> created, and every simplification it made.
/// </summary>
public sealed class GltfConversionResult
{
    /// <summary>
    /// Gets the number of Cycles meshes created (one per glTF mesh that has triangles).
    /// </summary>
    public int Meshes { get; internal set; }

    /// <summary>
    /// Gets the number of Cycles objects created for meshes (one per node instance; glTF meshes
    /// shared by several nodes, and EXT_mesh_gpu_instancing instances, become several objects that
    /// share one Cycles mesh).
    /// </summary>
    public int MeshObjects { get; internal set; }

    /// <summary>
    /// Gets the total number of triangles over all Cycles meshes (each mesh counted once).
    /// </summary>
    public int Triangles { get; internal set; }

    /// <summary>
    /// Gets the number of Cycles shaders created for glTF materials (including the default
    /// material for primitives without one).
    /// </summary>
    public int Materials { get; internal set; }

    /// <summary>
    /// Gets the number of lights created (KHR_lights_punctual lights, or the default sun).
    /// </summary>
    public int Lights { get; internal set; }

    /// <summary>
    /// Gets a value indicating whether the default sun was added because the file has no lights.
    /// </summary>
    public bool DefaultLightAdded { get; internal set; }

    /// <summary>
    /// Gets a value indicating whether a default camera framing the scene bounds was used.
    /// </summary>
    public bool DefaultCameraUsed { get; internal set; }

    /// <summary>
    /// Gets the minimum corner of the world-space bounds of every mesh object.
    /// </summary>
    public Vector3 BoundsMinimum { get; internal set; }

    /// <summary>
    /// Gets the maximum corner of the world-space bounds of every mesh object.
    /// </summary>
    public Vector3 BoundsMaximum { get; internal set; }

    /// <summary>
    /// Gets the warnings about glTF content that was ignored or simplified.
    /// </summary>
    public IReadOnlyList<string> Warnings => warnings;

    internal readonly List<string> warnings = new();

    /// <summary>
    /// Gets the conversion report of the <see cref="GltfMaterialMapping.Blender"/> mapping: one
    /// line per material describing the shader graph built (and what was deliberately not
    /// applied, such as occlusion), plus sampler notes.
    /// </summary>
    public IReadOnlyList<string> Notes => notes;

    private readonly List<string> notes = new();

    internal void Note(string message)
    {
        if (!notes.Contains(message)) notes.Add(message);
    }

    internal void Warn(string message)
    {
        if (!warnings.Contains(message)) warnings.Add(message);
    }
}
