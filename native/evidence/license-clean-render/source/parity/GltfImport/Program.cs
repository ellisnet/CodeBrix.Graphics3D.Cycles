// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
// Test input adapter only. Cycles scene preparation and rendering stay native.
using System.Numerics;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using CodeBrix.Graphics3D.Gltf2.Runtime;
using CodeBrix.Graphics3D.Gltf2.Schema2;
using CodeBrix.Graphics3D.Gltf2.Transforms;

if (args.Length != 2) throw new ArgumentException("Usage: GltfImport MODEL OUTPUT_TRIANGLES");
var path = Path.GetFullPath(args[0]);
var model = ModelRoot.Load(path); // Strict schema/link/data validation.
if (model.LogicalSkins.Count != 0 || model.LogicalAnimations.Count != 0)
    throw new NotSupportedException("This geometry gate requires a static, unskinned model.");
var decoded = model.LogicalMeshes.Decode();
var scene = model.DefaultScene ?? throw new NotSupportedException("A default scene is required.");
var instance = SceneTemplate.Create(scene).CreateInstance();
var points = new List<Vector3>();
var assignments = new List<int>();
var minimum = new Vector3(float.PositiveInfinity);
var maximum = new Vector3(float.NegativeInfinity);
var instances = 0;
foreach (var drawable in instance)
{
    IReadOnlyList<RigidTransform> transforms = drawable.Transform switch
    {
        InstancingTransform instancing => instancing.WorldTransforms,
        RigidTransform rigid => new[] { rigid },
        _ => throw new NotSupportedException("Only rigid transforms are supported by this gate.")
    };
    foreach (var transform in transforms)
    {
        if (!transform.Visible) continue;
        instances++;
        foreach (var primitive in decoded[drawable.Template.LogicalMeshIndex].Primitives)
        {
            if (primitive.MorphTargetsCount != 0 || primitive.JointsWeightsCount != 0)
                throw new NotSupportedException("Morph targets and skinning are outside this gate.");
            var triangles = primitive.TriangleIndices.ToArray();
            if (primitive.LineIndices.Any() || triangles.Length == 0)
                throw new NotSupportedException("Only triangle primitives are supported by this gate.");
            foreach (var (a, b, c) in triangles)
            {
                foreach (var index in transform.FlipFaces ? new[] { a, c, b } : new[] { a, b, c })
                {
                    var p = primitive.GetPosition(index, transform);
                    if (!float.IsFinite(p.X) || !float.IsFinite(p.Y) || !float.IsFinite(p.Z))
                        throw new InvalidDataException("Nonfinite transformed position.");
                    points.Add(p); minimum = Vector3.Min(minimum, p); maximum = Vector3.Max(maximum, p);
                }
                assignments.Add(primitive.Material?.LogicalIndex ?? -1);
            }
        }
    }
}
if (assignments.Count == 0 || assignments.Count > 500_000)
    throw new NotSupportedException("Geometry gate accepts 1..500,000 triangles.");
using (var output = new BinaryWriter(File.Create(args[1])))
{
    output.Write(Encoding.ASCII.GetBytes("CBCTRI1\0"));
    output.Write((uint)assignments.Count);
    foreach (var p in points) { output.Write(p.X); output.Write(p.Y); output.Write(p.Z); }
}
// Preserve provenance and assignments for review. This pass intentionally renders
// neutral clay; it makes no claim to preserve imported material appearance.
var report = new
{
    source = path,
    sourceSha256 = Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(path))),
    loaderPackage = "CodeBrix.Graphics3D.Gltf2.MitLicenseForever",
    loaderVersion = "1.0.290.996", validation = "Strict",
    geometryOnly = true, rendering = "Native Cycles BVH preparation and native WebGPU clay shading",
    triangles = assignments.Count, rigidInstances = instances,
    worldBounds = new[] { new[] { minimum.X, minimum.Y, minimum.Z }, new[] { maximum.X, maximum.Y, maximum.Z } },
    coordinateSystem = "glTF world coordinates, Y up; native test host applies documented presentation transform",
    materials = model.LogicalMaterials.Select(m => new { index = m.LogicalIndex, name = m.Name, alpha = m.Alpha.ToString() }),
    images = model.LogicalImages.Count,
    materialTriangles = assignments.GroupBy(i => i).ToDictionary(g => g.Key.ToString(), g => g.Count()),
    appearanceNotEvaluated = new[] { "materials", "textures", "alpha", "vertex normals", "vertex colors" },
    triangleFileSha256 = Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(args[1])))
};
File.WriteAllText(args[1] + ".json", JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true }) + "\n");
Console.WriteLine($"Strict Gltf2 load: {assignments.Count} triangles, {instances} rigid instances; geometry-only output.");
