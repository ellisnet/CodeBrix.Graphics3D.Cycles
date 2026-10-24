// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Numerics;
using System.Text.Json.Nodes;
using CodeBrix.Graphics3D.Cycles.Bundles;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Lights;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.Rendering;
using CodeBrix.Graphics3D.Gltf2.Schema2;
using GltfNode = CodeBrix.Graphics3D.Gltf2.Schema2.Node;
using static CodeBrix.Graphics3D.Cycles.GltfRender.SceneDocument;

namespace CodeBrix.Graphics3D.Cycles.GltfRender;

/// <summary>
/// Converts a loaded glTF 2.0 model (CodeBrix.Graphics3D.Gltf2) into a Cycles scene. This is an
/// example client. The conversion produces a self-contained <c>cbcOracleScene</c> version 1
/// bundle (<see cref="ToBundle"/>); <see cref="Convert"/> builds that bundle into a scene with
/// the library's <see cref="OracleSceneBundle"/> loader, so a rendered glTF file and a saved
/// bundle (rendered by the managed loader on any device, or by the C oracle runner) are the
/// same scene. Two material mappings exist (<see cref="GltfMaterialMapping"/>).
/// </summary>
public static class GltfSceneConverter
{
    /// <summary>
    /// The help text describing the material mappings and their limits.
    /// </summary>
    public const string MappingSummary =
        "Material mapping 'closures' (the default without --textures; factors only):\n" +
        "  baseColorFactor (rgb)          -> Diffuse BSDF (Lambert) colour\n" +
        "  metallicFactor > 0             -> Mix Closure(fac = metallic) of Diffuse and Glossy BSDF (GGX, colour = base colour)\n" +
        "  roughnessFactor                -> Glossy roughness (glTF and Cycles both use perceptual roughness)\n" +
        "  emissiveFactor (x strength)    -> Emission added with Add Closure; emission sampling off (forward hits only)\n" +
        "  KHR_materials_unlit            -> Emission(base colour, 1) only\n" +
        "  alphaMode BLEND                -> Mix Closure(fac = alpha) of Transparent BSDF and the surface\n" +
        "  alphaMode MASK                 -> fully transparent when alpha < alphaCutoff, otherwise opaque\n" +
        "  textures, normal/occlusion maps, material extensions, KHR_texture_transform -> ignored with a warning\n" +
        "Material mapping 'blender' (--material-mapping blender, implied by --textures DIR; the node graph Blender's glTF\n" +
        "  importer builds, rendered by Cycles):\n" +
        "  Principled BSDF (multiscatter GGX, IOR 1.5 or KHR_materials_ior)\n" +
        "  baseColorTexture (sRGB) x baseColorFactor -> Image Texture (scene_linear_srgb: kept 8-bit, decoded in the kernel, as\n" +
        "                                 Blender does) -> Mix Color MULTIPLY(fac 1, B = factor)\n" +
        "                                 -> Base Color (no Mix node when the factor is white); factor alone -> Base Color\n" +
        "  metallicRoughnessTexture (Non-Color) -> Image Texture (data) -> Separate Color RGB: B x metallicFactor -> Metallic,\n" +
        "                                 G x roughnessFactor -> Roughness (Math MULTIPLY only when the factor is not 1)\n" +
        "  normalTexture (Non-Color, OpenGL +Y) -> Image Texture (data) -> Normal Map (tangent space, strength = scale) -> Normal;\n" +
        "                                 the tangent frame is the glTF TANGENT (xyz -> 'tangent', w -> 'tangent_sign', corner domain)\n" +
        "  occlusionTexture               -> NOT applied (Blender's importer keeps it out of the shading tree; Cycles has no input)\n" +
        "  emissiveFactor x texture x KHR_materials_emissive_strength -> Emission Color / Emission Strength\n" +
        "  alphaMode OPAQUE -> alpha ignored; BLEND -> Alpha = base alpha; MASK -> Alpha = 1 - (alpha < cutoff)\n" +
        "  doubleSided                    -> no effect (Cycles shades both sides, as in Blender)\n" +
        "  sampler: magFilter NEAREST -> Closest, otherwise Linear; mipmaps do not exist in Cycles (full-resolution\n" +
        "  sampling); wrap REPEAT -> periodic, CLAMP_TO_EDGE -> clamp, MIRRORED_REPEAT -> mirror (wrapS; a different wrapT\n" +
        "  is reported). KHR_texture_transform, texCoord > 0 and other KHR_materials_* are reported and not applied.\n" +
        "Geometry: triangles (lists/strips/fans), POSITION, NORMAL (smooth shading), TEXCOORD_0 (v flipped); one Cycles mesh per\n" +
        "  glTF mesh, one Cycles object per node instance (shared meshes and EXT_mesh_gpu_instancing become instances).\n" +
        "  Skins and morph targets are ignored (rest pose); points and lines are skipped.\n" +
        "Lights: KHR_lights_punctual directional -> sun (strength = intensity), point -> point (strength = intensity x 4 pi),\n" +
        "  spot -> spot (angle = 2 x outer cone, smooth from the inner cone); a default sun is added when the file has none.\n" +
        "Cameras: perspective only (yfov, converted to Cycles' smaller-dimension fov); otherwise a default camera framing the bounds.";

    /// <summary>
    /// Applies the integrator settings the WebGPU backend requires (it rejects light trees,
    /// denoising, adaptive sampling and path guiding) plus deterministic sampling, so CPU and GPU
    /// renders of the same scene can be compared.
    /// </summary>
    /// <param name="scene">The scene.</param>
    public static void ApplyDeterministicIntegrator(CyclesScene scene)
    {
        ArgumentNullException.ThrowIfNull(scene);
        IntegratorNode integrator = scene.Integrator;
        integrator.UseGuiding = false;
        integrator.UseLightTree = false;
        integrator.UseAdaptiveSampling = false;
        integrator.UseDenoise = false;
        integrator.CausticsReflective = false;
        integrator.CausticsRefractive = false;
        integrator.MinBounce = 2;
        integrator.MaxBounce = 4;
        integrator.MaxDiffuseBounce = 4;
        integrator.SamplingPattern = IntegratorSamplingPattern.TabulatedSobol;
    }

    /// <summary>
    /// Converts the model's default scene (or its first scene) into <paramref name="scene"/>
    /// (through the bundle <see cref="ToBundle"/> produces). Call this on a fresh scene, before
    /// the first render.
    /// </summary>
    /// <param name="model">The loaded glTF model.</param>
    /// <param name="scene">The Cycles scene to fill.</param>
    /// <param name="options">Conversion options; null uses the defaults.</param>
    /// <returns>What was created and every simplification made.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="model"/> or <paramref name="scene"/> is null.</exception>
    /// <exception cref="InvalidOperationException">The model has no scene or no triangles, or a normal map has no tangents.</exception>
    public static GltfConversionResult Convert(ModelRoot model, CyclesScene scene, GltfConversionOptions options = null)
    {
        ArgumentNullException.ThrowIfNull(model);
        ArgumentNullException.ThrowIfNull(scene);
        options ??= new GltfConversionOptions();
        OracleSceneBundle bundle = ToBundle(model, options, new RenderSettings(options.Width, options.Height, 16), out GltfConversionResult result);
        bundle.Apply(scene);
        return result;
    }

    /// <summary>
    /// Converts the model's default scene (or its first scene) into a self-contained
    /// <c>cbcOracleScene</c> version 1 bundle: the document plus raw geometry, attribute and
    /// pixel files. Save it with <see cref="OracleSceneBundle.Save"/>.
    /// </summary>
    /// <param name="model">The loaded glTF model.</param>
    /// <param name="options">Conversion options; null uses the defaults.</param>
    /// <param name="render">The film size and samples recorded in the document.</param>
    /// <param name="result">What was created and every simplification made.</param>
    /// <param name="id">The scene id recorded in the document (default "gltf/scene").</param>
    /// <param name="description">The description recorded in the document.</param>
    /// <param name="repeats">How many repeat renders the document asks for (default 2).</param>
    /// <returns>The bundle.</returns>
    /// <exception cref="InvalidOperationException">The model has no scene or no triangles, or a normal map has no tangents.</exception>
    public static OracleSceneBundle ToBundle(ModelRoot model, GltfConversionOptions options, RenderSettings render, out GltfConversionResult result,
                                             string id = null, string description = null, uint repeats = 2)
    {
        ArgumentNullException.ThrowIfNull(model);
        options ??= new GltfConversionOptions();
        if (options.Textures != null && options.MaterialMapping != GltfMaterialMapping.Blender)
            throw new ArgumentException("Decoded textures are used by the Blender-importer material mapping only (GltfMaterialMapping.Blender).", nameof(options));
        Converter converter = new(model, options);
        result = converter.Run();
        return converter.Document.ToBundle(id ?? "gltf/scene", description ?? "glTF scene converted by tools/gltf/CycleGltfRender",
                                           "tools/gltf/CycleGltfRender", render, repeats);
    }

    private sealed class Converter
    {
        private readonly ModelRoot model;
        private readonly GltfConversionOptions options;
        private readonly GltfConversionResult result = new();
        private readonly Dictionary<int, (string Handle, bool NeedsTangents)> materials = new();
        private readonly Dictionary<int, (string Handle, Vector3 Minimum, Vector3 Maximum)> meshes = new();
        private readonly HashSet<int> images = new();
        private Vector3 minimum = new(float.PositiveInfinity), maximum = new(float.NegativeInfinity);
        private int objects;
        private string lightShader;

        internal Converter(ModelRoot model, GltfConversionOptions options)
        {
            this.model = model;
            this.options = options;
        }

        internal SceneDocument Document { get; } = new();

        internal GltfConversionResult Run()
        {
            Scene source = model.DefaultScene ?? model.LogicalScenes.FirstOrDefault();
            if (source == null) throw new InvalidOperationException("The glTF file has no scene.");
            if (options.ApplyDeterministicIntegrator)
            {
                JsonObject integrator = Document.Scene("integrator", "integrator");
                Set(integrator, "use_guiding", false);
                Set(integrator, "use_light_tree", false);
                Set(integrator, "use_adaptive_sampling", false);
                Set(integrator, "use_denoise", false);
                Set(integrator, "caustics_reflective", false);
                Set(integrator, "caustics_refractive", false);
                Set(integrator, "min_bounce", 2);
                Set(integrator, "max_bounce", 4);
                Set(integrator, "max_diffuse_bounce", 4);
                Set(integrator, "sampling_pattern", "tabulated_sobol");
            }
            if (model.LogicalAnimations.Count != 0) result.Warn("Animations are ignored; the rest pose is rendered.");
            foreach (string extension in model.ExtensionsUsed)
                if (extension is not ("KHR_lights_punctual" or "KHR_materials_unlit" or "KHR_materials_emissive_strength" or "KHR_materials_ior" or "EXT_mesh_gpu_instancing" or "KHR_mesh_quantization"))
                    result.Warn($"glTF extension {extension} is not converted.");

            List<GltfNode> nodes = GltfNode.Flatten(source).ToList();
            List<(GltfNode Node, CameraPerspective Perspective)> cameras = new();
            foreach (GltfNode node in nodes)
            {
                if (node.Mesh != null) AddMeshInstances(node);
                if (node.PunctualLight != null) AddLight(node.PunctualLight, node.WorldMatrix);
                if (node.Camera != null)
                {
                    if (node.Camera.Settings is CameraPerspective perspective) cameras.Add((node, perspective));
                    else result.Warn("Orthographic glTF cameras are not supported on the WebGPU backend and are ignored.");
                }
            }
            if (result.MeshObjects == 0) throw new InvalidOperationException("The glTF scene has no triangle geometry.");
            result.BoundsMinimum = minimum;
            result.BoundsMaximum = maximum;

            if (result.Lights == 0)
            {
                AddDefaultSun();
                result.DefaultLightAdded = true;
            }
            AddBackground();

            int chosen = options.CameraIndex < 0 ? 0 : options.CameraIndex;
            if (options.ForceDefaultCamera || cameras.Count == 0 || chosen >= cameras.Count)
            {
                if (!options.ForceDefaultCamera && options.CameraIndex >= 0 && options.CameraIndex >= cameras.Count)
                    result.Warn($"Camera index {options.CameraIndex} does not exist (the file has {cameras.Count} perspective cameras); a default camera is used.");
                FrameBounds();
                result.DefaultCameraUsed = true;
            }
            else
            {
                (GltfNode node, CameraPerspective perspective) = cameras[chosen];
                SetCamera(node.WorldMatrix, perspective.VerticalFOV);
            }
            return result;
        }

        private void AddMeshInstances(GltfNode node)
        {
            if (node.Skin != null) result.Warn("Skins are ignored; skinned meshes are rendered in their bind pose.");
            (string mesh, Vector3 low, Vector3 high) = GetOrCreateMesh(node.Mesh);
            if (mesh == null) return;
            MeshGpuInstancing instancing = node.GetGpuInstancing();
            IEnumerable<Matrix4x4> transforms = instancing == null || instancing.Count == 0
                ? new[] { node.WorldMatrix }
                : instancing.LocalTransforms.Select(local => local.Matrix * node.WorldMatrix);
            foreach (Matrix4x4 world in transforms)
            {
                JsonObject instance = Document.Create("object-" + objects++.ToString(CultureInfo.InvariantCulture), ObjectNode.NativeTypeName);
                Set(instance, "geometry", mesh);
                Set(instance, "tfm", Transform(Transform3x4.FromMatrix(world)));
                ++result.MeshObjects;
                for (int corner = 0; corner < 8; ++corner)
                {
                    Vector3 point = new((corner & 1) == 0 ? low.X : high.X, (corner & 2) == 0 ? low.Y : high.Y, (corner & 4) == 0 ? low.Z : high.Z);
                    Vector3 transformed = Vector3.Transform(point, world);
                    minimum = Vector3.Min(minimum, transformed);
                    maximum = Vector3.Max(maximum, transformed);
                }
            }
        }

        private (string Handle, Vector3 Minimum, Vector3 Maximum) GetOrCreateMesh(Mesh source)
        {
            if (meshes.TryGetValue(source.LogicalIndex, out var existing)) return existing;
            List<float> positions = new(), normals = new(), uvs = new(), tangents = new(), signs = new();
            List<uint> indices = new();
            List<int> shaderIndices = new();
            List<string> used = new();
            bool allNormals = true, anyUv = false, anyTangentNeed = false;
            Vector3 low = new(float.PositiveInfinity), high = new(float.NegativeInfinity);
            foreach (MeshPrimitive primitive in source.Primitives)
            {
                if (primitive.DrawPrimitiveType is PrimitiveType.POINTS or PrimitiveType.LINES or PrimitiveType.LINE_LOOP or PrimitiveType.LINE_STRIP)
                {
                    result.Warn("Point and line primitives are skipped.");
                    continue;
                }
                if (primitive.MorphTargetsCount != 0) result.Warn("Morph targets are ignored; the base shape is rendered.");
                Accessor positionAccessor = primitive.GetVertexAccessor("POSITION");
                if (positionAccessor == null) continue;
                IList<Vector3> points = positionAccessor.AsVector3Array();
                IList<Vector3> vertexNormals = primitive.GetVertexAccessor("NORMAL")?.AsVector3Array();
                IList<Vector2> texcoords = primitive.GetVertexAccessor("TEXCOORD_0")?.AsVector2Array();
                IList<Vector4> vertexTangents = primitive.GetVertexAccessor("TANGENT")?.AsVector4Array();
                List<(int A, int B, int C)> triangles = primitive.GetTriangleIndices().ToList();
                if (triangles.Count == 0) continue;
                (string shader, bool needsTangents) = GetOrCreateMaterial(primitive.Material);
                if (needsTangents && vertexTangents == null)
                    throw new InvalidOperationException($"Material '{primitive.Material?.Name}' has a normal map but a primitive of mesh '{source.Name}' has no TANGENT attribute; " +
                                                        "MikkTSpace tangent generation is not implemented, so the normal map cannot be applied.");
                anyTangentNeed |= needsTangents;
                int slot = used.IndexOf(shader);
                if (slot < 0)
                {
                    slot = used.Count;
                    used.Add(shader);
                }
                uint baseVertex = (uint)(positions.Count / 3);
                foreach (Vector3 p in points)
                {
                    positions.Add(p.X); positions.Add(p.Y); positions.Add(p.Z);
                    low = Vector3.Min(low, p);
                    high = Vector3.Max(high, p);
                }
                if (vertexNormals == null) allNormals = false;
                else foreach (Vector3 n in vertexNormals) { normals.Add(n.X); normals.Add(n.Y); normals.Add(n.Z); }
                anyUv |= texcoords != null;
                foreach ((int a, int b, int c) in triangles)
                {
                    indices.Add(baseVertex + (uint)a); indices.Add(baseVertex + (uint)b); indices.Add(baseVertex + (uint)c);
                    shaderIndices.Add(slot);
                    foreach (int corner in new[] { a, b, c })
                    {
                        Vector2 uv = texcoords == null ? Vector2.Zero : texcoords[corner];
                        uvs.Add(uv.X); uvs.Add(1f - uv.Y);
                        // glTF TANGENT: xyz = tangent, w = bitangent sign (B = cross(N, T) * w), the
                        // meaning of Cycles' "tangent" and "tangent_sign" corner attributes.
                        Vector4 tangent = vertexTangents == null ? Vector4.Zero : vertexTangents[corner];
                        tangents.Add(tangent.X); tangents.Add(tangent.Y); tangents.Add(tangent.Z);
                        signs.Add(tangent.W < 0f ? -1f : 1f);
                    }
                }
            }
            if (indices.Count == 0)
            {
                meshes[source.LogicalIndex] = (null, low, high);
                return (null, low, high);
            }
            string handle = "mesh-" + source.LogicalIndex.ToString(CultureInfo.InvariantCulture);
            JsonObject entry = Document.Create(handle, MeshNode.NativeTypeName);
            JsonArray attributes = new();
            JsonObject mesh = new() { ["positionsFile"] = handle + ".positions.f32", ["indicesFile"] = handle + ".indices.u32", ["attributes"] = attributes };
            entry["mesh"] = mesh;
            Document.AddFloats(handle + ".positions.f32", positions.ToArray());
            Document.AddUInts(handle + ".indices.u32", indices.ToArray());
            void Attribute(string name, string value, string domain, float[] data)
            {
                string file = $"{handle}.{name}.f32";
                attributes.Add(new JsonObject { ["name"] = name, ["value"] = value, ["domain"] = domain, ["flags"] = new JsonArray("standard"), ["file"] = file });
                Document.AddFloats(file, data);
            }
            Set(entry, "used_shaders", new JsonArray(used.Select(name => (JsonNode)name).ToArray()));
            if (used.Count > 1) Set(entry, "shader", new JsonArray(shaderIndices.Select(index => (JsonNode)index).ToArray()));
            if (allNormals)
            {
                Attribute("N", "normal", "vertex_normal", normals.ToArray());
                Set(entry, "smooth", new JsonArray(Enumerable.Repeat((JsonNode)true, shaderIndices.Count).Select(node => node.DeepClone()).ToArray()));
            }
            if (anyUv) Attribute("uv", "point2", "corner", uvs.ToArray());
            if (anyTangentNeed)
            {
                Attribute("tangent", "vector", "corner", tangents.ToArray());
                Attribute("tangent_sign", "float", "corner", signs.ToArray());
            }
            ++result.Meshes;
            result.Triangles += shaderIndices.Count;
            var created = (handle, low, high);
            meshes[source.LogicalIndex] = created;
            return created;
        }

        private (string Handle, bool NeedsTangents) GetOrCreateMaterial(Material material)
        {
            int key = material?.LogicalIndex ?? -1;
            if (materials.TryGetValue(key, out var existing)) return existing;
            string handle = key < 0 ? "material-default" : "material-" + key.ToString(CultureInfo.InvariantCulture);
            JsonObject shader = Document.Create(handle, ShaderNode.NativeTypeName);
            Set(shader, "emission_sampling_method", "none");
            bool needsTangents = options.MaterialMapping == GltfMaterialMapping.Blender
                ? BlenderMaterial(material, shader, handle)
                : ClosureMaterial(material, shader);
            var created = (handle, needsTangents);
            materials[key] = created;
            ++result.Materials;
            return created;
        }

        // The factor-only mapping (unchanged behaviour): Diffuse/Glossy/Emission/Transparent closures.
        private bool ClosureMaterial(Material material, JsonObject shader)
        {
            Vector4 baseColor = Vector4.One;
            float metallic = 1f, roughness = 1f, emissiveStrength = 1f;
            Vector3 emissive = Vector3.Zero;
            bool unlit = false;
            AlphaMode alphaMode = AlphaMode.OPAQUE;
            float alphaCutoff = .5f;
            if (material != null)
            {
                unlit = material.Unlit;
                alphaMode = material.Alpha;
                alphaCutoff = material.AlphaCutoff;
                if (material.TryGetChannel("BaseColor", out MaterialChannel baseChannel))
                {
                    baseColor = baseChannel.Color;
                    if (baseChannel.Texture != null) result.Warn("Textures are ignored (Gltf2 does not decode image bytes); base-colour factors only.");
                }
                if (material.TryGetChannel("MetallicRoughness", out MaterialChannel metalChannel))
                {
                    metallic = Factor(metalChannel, "MetallicFactor", 1f);
                    roughness = Factor(metalChannel, "RoughnessFactor", 1f);
                    if (metalChannel.Texture != null) result.Warn("Textures are ignored (Gltf2 does not decode image bytes); base-colour factors only.");
                }
                if (material.TryGetChannel("Emissive", out MaterialChannel emissiveChannel))
                {
                    Vector4 color = emissiveChannel.Color;
                    emissive = new Vector3(color.X, color.Y, color.Z);
                    emissiveStrength = Factor(emissiveChannel, "EmissiveStrength", 1f);
                    if (emissiveChannel.Texture != null) result.Warn("Textures are ignored (Gltf2 does not decode image bytes); base-colour factors only.");
                }
                foreach (string ignored in new[] { "Normal", "Occlusion" })
                    if (material.TryGetChannel(ignored, out MaterialChannel channel) && channel.Texture != null)
                        result.Warn("Normal and occlusion maps are ignored.");
                foreach (string ignored in new[] { "ClearCoat", "Transmission", "SheenColor", "SpecularColor", "VolumeThickness", "Iridescence", "Anisotropy" })
                    if (material.TryGetChannel(ignored, out _)) result.Warn("glTF material extensions (clearcoat, transmission, sheen, specular, volume, iridescence, anisotropy) are ignored.");
            }
            Vector3 rgb = new(baseColor.X, baseColor.Y, baseColor.Z);
            float alpha = Math.Clamp(baseColor.W, 0f, 1f);

            Graph graph = GraphOf(shader);
            string surface, surfaceSocket;
            if (unlit)
            {
                JsonObject emission = graph.Node("emission", EmissionNode.NativeTypeName);
                Set(emission, "color", Floats(rgb.X, rgb.Y, rgb.Z));
                Set(emission, "strength", 1f);
                (surface, surfaceSocket) = ("emission", "Emission");
            }
            else
            {
                JsonObject diffuse = graph.Node("diffuse", DiffuseBsdfNode.NativeTypeName);
                Set(diffuse, "color", Floats(rgb.X, rgb.Y, rgb.Z));
                (surface, surfaceSocket) = ("diffuse", "BSDF");
                if (metallic > 0f)
                {
                    JsonObject glossy = graph.Node("glossy", GlossyBsdfNode.NativeTypeName);
                    Set(glossy, "distribution", "ggx");
                    Set(glossy, "color", Floats(rgb.X, rgb.Y, rgb.Z));
                    Set(glossy, "roughness", Math.Clamp(roughness, 0f, 1f));
                    if (metallic >= 1f) (surface, surfaceSocket) = ("glossy", "BSDF");
                    else
                    {
                        JsonObject mix = graph.Node("metal-mix", MixClosureNode.NativeTypeName);
                        Set(mix, "fac", metallic);
                        graph.Connect("diffuse", "BSDF", "metal-mix", "Closure1");
                        graph.Connect("glossy", "BSDF", "metal-mix", "Closure2");
                        (surface, surfaceSocket) = ("metal-mix", "Closure");
                    }
                }
                if (emissive != Vector3.Zero && emissiveStrength > 0f)
                {
                    JsonObject emission = graph.Node("emission", EmissionNode.NativeTypeName);
                    Set(emission, "color", Floats(emissive.X, emissive.Y, emissive.Z));
                    Set(emission, "strength", emissiveStrength);
                    graph.Node("emission-add", AddClosureNode.NativeTypeName);
                    graph.Connect(surface, surfaceSocket, "emission-add", "Closure1");
                    graph.Connect("emission", "Emission", "emission-add", "Closure2");
                    (surface, surfaceSocket) = ("emission-add", "Closure");
                }
            }
            float coverage = alphaMode switch
            {
                AlphaMode.BLEND => alpha,
                AlphaMode.MASK => alpha < alphaCutoff ? 0f : 1f,
                _ => 1f
            };
            if (coverage < 1f)
            {
                JsonObject transparent = graph.Node("transparent", TransparentBsdfNode.NativeTypeName);
                Set(transparent, "color", Floats(1f, 1f, 1f));
                JsonObject mix = graph.Node("alpha-mix", MixClosureNode.NativeTypeName);
                Set(mix, "fac", coverage);
                graph.Connect("transparent", "BSDF", "alpha-mix", "Closure1");
                graph.Connect(surface, surfaceSocket, "alpha-mix", "Closure2");
                (surface, surfaceSocket) = ("alpha-mix", "Closure");
            }
            graph.Connect(surface, surfaceSocket, "output", "Surface");
            return false;
        }

        // The node graph Blender's glTF importer builds (Principled BSDF with image textures).
        private bool BlenderMaterial(Material material, JsonObject shader, string handle)
        {
            string name = material?.Name ?? "(default)";
            Graph graph = GraphOf(shader);
            JsonObject bsdf = graph.Node("principled", PrincipledBsdfNode.NativeTypeName);
            Set(bsdf, "distribution", "multi_ggx");
            Set(bsdf, "ior", material != null && material.IndexOfRefraction > 0f ? material.IndexOfRefraction : 1.5f);
            List<string> notes = new();
            bool needsTangents = false;
            if (material == null)
            {
                // glTF default material: white base colour, metallic 1, roughness 1.
                Set(bsdf, "base_color", Floats(1f, 1f, 1f));
                Set(bsdf, "metallic", 1f);
                Set(bsdf, "roughness", 1f);
                graph.Connect("principled", "BSDF", "output", "Surface");
                result.Note($"{handle} '{name}': glTF default material -> Principled BSDF(base 1, metallic 1, roughness 1)");
                return false;
            }
            if (material.Unlit)
            {
                material.TryGetChannel("BaseColor", out MaterialChannel unlitBase);
                Vector4 color = unlitBase.Color;
                JsonObject emission = graph.Node("emission", EmissionNode.NativeTypeName);
                Set(emission, "color", Floats(color.X, color.Y, color.Z));
                Set(emission, "strength", 1f);
                graph.Connect("emission", "Emission", "output", "Surface");
                if (unlitBase.Texture != null) result.Warn($"{handle} '{name}': KHR_materials_unlit base-colour textures are not applied (factor only).");
                result.Note($"{handle} '{name}': KHR_materials_unlit -> Emission(base colour factor, 1)");
                return false;
            }

            // Base colour (sRGB texture x factor).
            material.TryGetChannel("BaseColor", out MaterialChannel baseChannel);
            Vector4 factor = baseChannel.Color;
            string baseAlphaNode = null;
            if (Texture(graph, baseChannel, "base-texture", "scene_linear_srgb", handle, name, "baseColorTexture"))
            {
                if (factor.X != 1f || factor.Y != 1f || factor.Z != 1f)
                {
                    JsonObject mix = graph.Node("base-factor", MixColorNode.NativeTypeName);
                    Set(mix, "blend_type", "multiply");
                    Set(mix, "fac", 1f);
                    Set(mix, "b", Floats(factor.X, factor.Y, factor.Z));
                    graph.Connect("base-texture", "Color", "base-factor", "A");
                    graph.Connect("base-factor", "Result", "principled", "Base Color");
                    notes.Add($"Base Color = Mix Color MULTIPLY(image {baseChannel.Texture.PrimaryImage.LogicalIndex} sRGB, factor {Vec(factor.X, factor.Y, factor.Z)})");
                }
                else
                {
                    graph.Connect("base-texture", "Color", "principled", "Base Color");
                    notes.Add($"Base Color = image {baseChannel.Texture.PrimaryImage.LogicalIndex} (sRGB)");
                }
                baseAlphaNode = "base-texture";
            }
            else
            {
                Set(bsdf, "base_color", Floats(factor.X, factor.Y, factor.Z));
                notes.Add($"Base Color = {Vec(factor.X, factor.Y, factor.Z)}");
            }

            // Metallic (B) and roughness (G).
            material.TryGetChannel("MetallicRoughness", out MaterialChannel metalChannel);
            float metallic = Factor(metalChannel, "MetallicFactor", 1f), roughness = Factor(metalChannel, "RoughnessFactor", 1f);
            if (Texture(graph, metalChannel, "metal-rough-texture", "data", handle, name, "metallicRoughnessTexture"))
            {
                JsonObject separate = graph.Node("metal-rough-separate", SeparateColorNode.NativeTypeName);
                Set(separate, "color_type", "rgb");
                graph.Connect("metal-rough-texture", "Color", "metal-rough-separate", "Color");
                Channel(graph, "Blue", metallic, "metallic-factor", "Metallic");
                Channel(graph, "Green", roughness, "roughness-factor", "Roughness");
                notes.Add($"Metallic = image {metalChannel.Texture.PrimaryImage.LogicalIndex} B x {Num(metallic)}; Roughness = G x {Num(roughness)} (Separate Color RGB, data)");
            }
            else
            {
                Set(bsdf, "metallic", metallic);
                Set(bsdf, "roughness", roughness);
                notes.Add($"Metallic = {Num(metallic)}; Roughness = {Num(roughness)}");
            }

            // Normal map (tangent space, glTF TANGENT frame).
            if (material.TryGetChannel("Normal", out MaterialChannel normalChannel) &&
                Texture(graph, normalChannel, "normal-texture", "data", handle, name, "normalTexture"))
            {
                float scale = Factor(normalChannel, "NormalScale", 1f);
                JsonObject map = graph.Node("normal-map", NormalMapNode.NativeTypeName);
                Set(map, "space", "tangent");
                Set(map, "convention", "opengl");
                Set(map, "strength", scale);
                graph.Connect("normal-texture", "Color", "normal-map", "Color");
                graph.Connect("normal-map", "Normal", "principled", "Normal");
                needsTangents = true;
                notes.Add($"Normal = Normal Map(tangent space, OpenGL, strength {Num(scale)}, image {normalChannel.Texture.PrimaryImage.LogicalIndex} data; glTF TANGENT)");
            }

            // Occlusion: reported, never applied.
            if (material.TryGetChannel("Occlusion", out MaterialChannel occlusionChannel) && occlusionChannel.Texture != null)
                notes.Add($"occlusionTexture (image {occlusionChannel.Texture.PrimaryImage.LogicalIndex}, strength {Num(Factor(occlusionChannel, "OcclusionStrength", 1f))}) NOT applied: " +
                          "Blender's importer keeps occlusion out of the shading tree and Cycles has no occlusion input");

            // Emission.
            if (material.TryGetChannel("Emissive", out MaterialChannel emissiveChannel))
            {
                Vector4 emissive = emissiveChannel.Color;
                float strength = Factor(emissiveChannel, "EmissiveStrength", 1f);
                bool textured = Texture(graph, emissiveChannel, "emission-texture", "scene_linear_srgb", handle, name, "emissiveTexture");
                bool black = emissive.X == 0f && emissive.Y == 0f && emissive.Z == 0f;
                if (textured && !black)
                {
                    if (emissive.X != 1f || emissive.Y != 1f || emissive.Z != 1f)
                    {
                        JsonObject mix = graph.Node("emission-factor", MixColorNode.NativeTypeName);
                        Set(mix, "blend_type", "multiply");
                        Set(mix, "fac", 1f);
                        Set(mix, "b", Floats(emissive.X, emissive.Y, emissive.Z));
                        graph.Connect("emission-texture", "Color", "emission-factor", "A");
                        graph.Connect("emission-factor", "Result", "principled", "Emission Color");
                    }
                    else graph.Connect("emission-texture", "Color", "principled", "Emission Color");
                    Set(bsdf, "emission_strength", strength);
                    notes.Add($"Emission = image {emissiveChannel.Texture.PrimaryImage.LogicalIndex} (sRGB) x {Vec(emissive.X, emissive.Y, emissive.Z)}, strength {Num(strength)}");
                }
                else if (!black)
                {
                    Set(bsdf, "emission_color", Floats(emissive.X, emissive.Y, emissive.Z));
                    Set(bsdf, "emission_strength", strength);
                    notes.Add($"Emission = {Vec(emissive.X, emissive.Y, emissive.Z)}, strength {Num(strength)}");
                }
            }

            // Alpha.
            if (material.Alpha != AlphaMode.OPAQUE)
            {
                string alphaSource = null;
                if (factor.W != 1f || baseAlphaNode == null)
                {
                    if (baseAlphaNode != null)
                    {
                        JsonObject multiply = graph.Node("alpha-factor", MathNode.NativeTypeName);
                        Set(multiply, "math_type", "multiply");
                        Set(multiply, "value2", factor.W);
                        graph.Connect(baseAlphaNode, "Alpha", "alpha-factor", "Value1");
                        alphaSource = "alpha-factor";
                    }
                }
                else alphaSource = baseAlphaNode;
                if (material.Alpha == AlphaMode.MASK)
                {
                    JsonObject less = graph.Node("alpha-cutoff", MathNode.NativeTypeName);
                    Set(less, "math_type", "less_than");
                    Set(less, "value1", factor.W);
                    Set(less, "value2", material.AlphaCutoff);
                    if (alphaSource != null) graph.Connect(alphaSource, alphaSource == baseAlphaNode ? "Alpha" : "Value", "alpha-cutoff", "Value1");
                    JsonObject invert = graph.Node("alpha-mask", MathNode.NativeTypeName);
                    Set(invert, "math_type", "subtract");
                    Set(invert, "value1", 1f);
                    graph.Connect("alpha-cutoff", "Value", "alpha-mask", "Value2");
                    graph.Connect("alpha-mask", "Value", "principled", "Alpha");
                    notes.Add($"Alpha = 1 - (alpha < {Num(material.AlphaCutoff)}) (MASK)");
                }
                else
                {
                    if (alphaSource != null) graph.Connect(alphaSource, alphaSource == baseAlphaNode ? "Alpha" : "Value", "principled", "Alpha");
                    else Set(bsdf, "alpha", factor.W);
                    notes.Add("Alpha = base colour alpha (BLEND)");
                }
            }
            if (!material.DoubleSided) notes.Add("doubleSided false has no effect (Cycles shades both sides, as in Blender)");
            foreach (string ignored in new[] { "ClearCoat", "ClearCoatRoughness", "ClearCoatNormal", "Transmission", "SheenColor", "SheenRoughness", "SpecularColor", "SpecularFactor",
                                               "VolumeThickness", "VolumeAttenuation", "Iridescence", "IridescenceThickness", "Anisotropy", "Diffuse", "SpecularGlossiness" })
                if (material.TryGetChannel(ignored, out _)) result.Warn($"{handle} '{name}': glTF material channel {ignored} (a KHR_materials_* extension) is not converted.");

            graph.Connect("principled", "BSDF", "output", "Surface");
            result.Note($"{handle} '{name}': Principled BSDF(multiscatter GGX, IOR {Num(material.IndexOfRefraction > 0f ? material.IndexOfRefraction : 1.5f)}); " + string.Join("; ", notes));
            return needsTangents;
        }

        // Math MULTIPLY by the factor only when it is not 1 (as Blender's importer does).
        private static void Channel(Graph graph, string component, float factor, string multiplyHandle, string input)
        {
            if (factor == 1f)
            {
                graph.Connect("metal-rough-separate", component, "principled", input);
                return;
            }
            JsonObject multiply = graph.Node(multiplyHandle, MathNode.NativeTypeName);
            Set(multiply, "math_type", "multiply");
            Set(multiply, "value2", factor);
            graph.Connect("metal-rough-separate", component, multiplyHandle, "Value1");
            graph.Connect(multiplyHandle, "Value", "principled", input);
        }

        // Adds an Image Texture for a channel's texture when the decoded image exists; false otherwise.
        private bool Texture(Graph graph, MaterialChannel channel, string nodeHandle, string colorspace, string handle, string name, string slot)
        {
            Gltf2.Schema2.Texture texture = channel.Texture;
            if (texture == null) return false;
            Gltf2.Schema2.Image image = texture.PrimaryImage;
            if (image == null)
            {
                result.Warn($"{handle} '{name}': {slot} has no image source and is not applied.");
                return false;
            }
            if (options.Textures == null || !options.Textures.Images.TryGetValue(image.LogicalIndex, out DecodedImage decoded))
            {
                result.Warn($"{handle} '{name}': {slot} (image {image.LogicalIndex}) is not applied: no decoded pixels (GltfConversionOptions.Textures has no image for it).");
                return false;
            }
            if (channel.TextureCoordinate != 0) result.Warn($"{handle} '{name}': {slot} uses TEXCOORD_{channel.TextureCoordinate}; only TEXCOORD_0 is converted, so TEXCOORD_0 is sampled.");
            if (channel.TextureTransform != null) result.Warn($"{handle} '{name}': {slot} has KHR_texture_transform, which is not converted (the untransformed UVs are sampled).");
            string imageHandle = "image-" + image.LogicalIndex.ToString(CultureInfo.InvariantCulture);
            if (images.Add(image.LogicalIndex))
            {
                Document.AddImage(imageHandle, decoded.Width, decoded.Height, "byte4", decoded.FileName, colorspace == "scene_linear_srgb" ? "srgb" : "linear");
                Document.AddFile(decoded.FileName, decoded.Pixels);
            }
            TextureSampler sampler = texture.Sampler;
            string interpolation = sampler != null && sampler.MagFilter == TextureInterpolationFilter.NEAREST ? "closest" : "linear";
            TextureWrapMode wrapS = sampler?.WrapS ?? TextureWrapMode.REPEAT, wrapT = sampler?.WrapT ?? TextureWrapMode.REPEAT;
            string extension = wrapS switch { TextureWrapMode.CLAMP_TO_EDGE => "clamp", TextureWrapMode.MIRRORED_REPEAT => "mirror", _ => "periodic" };
            if (wrapS != wrapT) result.Warn($"{handle} '{name}': {slot} has wrapS {wrapS} and wrapT {wrapT}; Cycles has one extension mode, so {extension} (wrapS) is used.");
            if (sampler != null && sampler.MinFilter is TextureMipMapFilter.LINEAR_MIPMAP_LINEAR or TextureMipMapFilter.LINEAR_MIPMAP_NEAREST
                    or TextureMipMapFilter.NEAREST_MIPMAP_LINEAR or TextureMipMapFilter.NEAREST_MIPMAP_NEAREST)
                result.Note($"sampler minFilter {sampler.MinFilter}: Cycles has no mipmaps; textures are sampled at full resolution with {interpolation} interpolation (as Blender's Cycles does)");
            JsonObject node = graph.Node(nodeHandle, ImageTextureNode.NativeTypeName);
            Set(node, "image", imageHandle);
            Set(node, "colorspace", colorspace);
            Set(node, "alpha_type", colorspace == "data" ? "channel_packed" : "unassociated");
            Set(node, "interpolation", interpolation);
            Set(node, "extension", extension);
            return true;
        }

        private static string Num(float value) => value.ToString("R", CultureInfo.InvariantCulture);

        private static string Vec(float x, float y, float z) => $"({Num(x)}, {Num(y)}, {Num(z)})";

        private static float Factor(MaterialChannel channel, string name, float fallback)
        {
            foreach (IMaterialParameter parameter in channel.Parameters)
                if (parameter.Name == name && parameter.Value is float value) return value;
            return fallback;
        }

        private void AddLight(PunctualLight light, Matrix4x4 world)
        {
            Vector3 strength = light.Color * light.Intensity;
            string shader = LightShader();
            string handle = "light-" + result.Lights.ToString(CultureInfo.InvariantCulture);
            JsonObject node;
            switch (light.LightType)
            {
                case PunctualLightType.Directional:
                    node = Document.Create(handle, SunLightNode.NativeTypeName);
                    break;
                case PunctualLightType.Spot:
                    node = Document.Create(handle, SpotLightNode.NativeTypeName);
                    float outer = Math.Clamp(light.OuterConeAngle, 1e-4f, MathF.PI / 2);
                    Set(node, "angle", 2f * outer);
                    Set(node, "smooth", Math.Clamp((outer - light.InnerConeAngle) / outer, 0f, 1f));
                    strength *= 4f * MathF.PI;
                    break;
                default:
                    node = Document.Create(handle, PointLightNode.NativeTypeName);
                    strength *= 4f * MathF.PI;
                    break;
            }
            if (!float.IsPositiveInfinity(light.Range) && light.Range > 0) result.Warn("Punctual light range is ignored (Cycles lights have no cutoff).");
            Set(node, "strength", Floats(strength.X, strength.Y, strength.Z));
            Set(node, "used_shaders", new JsonArray(shader));
            JsonObject instance = Document.Create("object-" + objects++.ToString(CultureInfo.InvariantCulture), ObjectNode.NativeTypeName);
            Set(instance, "geometry", handle);
            Set(instance, "tfm", Transform(Transform3x4.FromMatrix(RemoveScale(world))));
            ++result.Lights;
        }

        private string LightShader()
        {
            if (lightShader != null) return lightShader;
            lightShader = "light-shader";
            JsonObject shader = Document.Create(lightShader, ShaderNode.NativeTypeName);
            Graph graph = GraphOf(shader);
            JsonObject emission = graph.Node("emission", EmissionNode.NativeTypeName);
            Set(emission, "color", Floats(1f, 1f, 1f));
            Set(emission, "strength", 1f);
            graph.Connect("emission", "Emission", "output", "Surface");
            return lightShader;
        }

        private void AddDefaultSun()
        {
            // Light travels along the light's local -Z axis (the glTF and Cycles convention).
            Vector3 travel = Vector3.Normalize(new Vector3(-.35f, -1f, -.55f));
            Matrix4x4 frame = Basis(-travel, Vector3.Zero);
            string shader = LightShader();
            string handle = "light-" + result.Lights.ToString(CultureInfo.InvariantCulture);
            JsonObject sun = Document.Create(handle, SunLightNode.NativeTypeName);
            Set(sun, "strength", Floats(options.DefaultSunStrength, options.DefaultSunStrength, options.DefaultSunStrength));
            Set(sun, "angle", .05f);
            Set(sun, "used_shaders", new JsonArray(shader));
            JsonObject instance = Document.Create("object-" + objects++.ToString(CultureInfo.InvariantCulture), ObjectNode.NativeTypeName);
            Set(instance, "geometry", handle);
            Set(instance, "tfm", Transform(Transform3x4.FromMatrix(frame)));
            ++result.Lights;
        }

        private void AddBackground()
        {
            JsonObject world = Document.Create("world", ShaderNode.NativeTypeName);
            Graph graph = GraphOf(world);
            JsonObject background = graph.Node("background", BackgroundShaderNode.NativeTypeName);
            Vector3 color = options.BackgroundColor;
            Set(background, "color", Floats(color.X, color.Y, color.Z));
            Set(background, "strength", options.BackgroundStrength);
            graph.Connect("background", "Background", "output", "Surface");
            Set(world, "emission_sampling_method", "none");
            Set(Document.Scene("background", "background"), "shader", "world");
        }

        private void FrameBounds()
        {
            Vector3 center = (minimum + maximum) / 2f;
            float radius = Math.Max(Vector3.Distance(minimum, maximum) / 2f, 1e-3f);
            const float verticalFov = .8f;
            float aspect = options.Width / (float)Math.Max(1u, options.Height);
            float limitingHalfFov = MathF.Atan(MathF.Tan(verticalFov / 2f) * Math.Min(1f, aspect));
            float distance = radius / MathF.Sin(limitingHalfFov) * 1.05f;
            Vector3 viewFrom = Vector3.Normalize(new Vector3(.55f, .45f, 1f));
            Vector3 eye = center + viewFrom * distance;
            // A glTF-style camera: looks along its local -Z with +Y up.
            Vector3 back = Vector3.Normalize(eye - center);
            Matrix4x4 cameraWorld = Basis(back, eye);
            SetCamera(cameraWorld, verticalFov);
        }

        // A right-handed frame whose +Z axis is 'z' (world +Y as the up hint), positioned at 'origin'.
        private static Matrix4x4 Basis(Vector3 z, Vector3 origin)
        {
            Vector3 up = MathF.Abs(Vector3.Dot(z, Vector3.UnitY)) > .99f ? Vector3.UnitZ : Vector3.UnitY;
            Vector3 x = Vector3.Normalize(Vector3.Cross(up, z));
            Vector3 y = Vector3.Cross(z, x);
            return new Matrix4x4(x.X, x.Y, x.Z, 0, y.X, y.Y, y.Z, 0, z.X, z.Y, z.Z, 0, origin.X, origin.Y, origin.Z, 1);
        }

        private static Matrix4x4 RemoveScale(Matrix4x4 m)
        {
            Vector3 x = Vector3.Normalize(new Vector3(m.M11, m.M12, m.M13));
            Vector3 y = Vector3.Normalize(new Vector3(m.M21, m.M22, m.M23));
            Vector3 z = Vector3.Normalize(new Vector3(m.M31, m.M32, m.M33));
            return new Matrix4x4(x.X, x.Y, x.Z, 0, y.X, y.Y, y.Z, 0, z.X, z.Y, z.Z, 0, m.M41, m.M42, m.M43, 1);
        }

        // glTF cameras look along local -Z; Cycles cameras look along local +Z, so the camera's Z axis
        // is flipped (the same conversion Blender applies). Cycles' fov spans the smaller film side.
        private void SetCamera(Matrix4x4 world, float verticalFov)
        {
            Matrix4x4 flipped = Matrix4x4.CreateScale(1f, 1f, -1f) * RemoveScale(world);
            float aspect = options.Width / (float)Math.Max(1u, options.Height);
            JsonObject camera = Document.Scene("camera", "camera");
            Set(camera, "camera_type", "perspective");
            Set(camera, "matrix", Transform(Transform3x4.FromMatrix(flipped)));
            Set(camera, "fov", aspect >= 1f ? verticalFov : 2f * MathF.Atan(MathF.Tan(verticalFov / 2f) * aspect));
        }
    }
}
