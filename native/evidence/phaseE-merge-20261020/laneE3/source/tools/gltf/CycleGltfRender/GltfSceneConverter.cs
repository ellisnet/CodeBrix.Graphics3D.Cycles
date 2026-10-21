// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Linq;
using System.Numerics;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Lights;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.Nodes;
using CodeBrix.Graphics3D.Gltf2.Schema2;
using GltfNode = CodeBrix.Graphics3D.Gltf2.Schema2.Node;

namespace CodeBrix.Graphics3D.Cycles.GltfRender;

/// <summary>
/// Converts a loaded glTF 2.0 model (CodeBrix.Graphics3D.Gltf2) into a Cycles scene through the
/// typed API. This is an example client: it maps glTF metallic-roughness materials to the closest
/// closures the WebGPU backend has today (Principled BSDF is not available on that backend yet)
/// and uses base-colour factors only, because Gltf2 carries image bytes without decoding them.
/// </summary>
public static class GltfSceneConverter
{
    /// <summary>
    /// The help text describing the material mapping and its limits.
    /// </summary>
    public const string MappingSummary =
        "Material mapping (closest closures the WebGPU backend has today; Principled BSDF is NOT available there yet):\n" +
        "  baseColorFactor (rgb)          -> Diffuse BSDF (Lambert) colour\n" +
        "  metallicFactor > 0             -> Mix Closure(fac = metallic) of Diffuse and Glossy BSDF (GGX, colour = base colour)\n" +
        "  roughnessFactor                -> Glossy roughness (glTF and Cycles both use perceptual roughness)\n" +
        "  emissiveFactor (x strength)    -> Emission added with Add Closure; emission sampling off (forward hits only)\n" +
        "  KHR_materials_unlit            -> Emission(base colour, 1) only\n" +
        "  alphaMode BLEND                -> Mix Closure(fac = alpha) of Transparent BSDF and the surface\n" +
        "  alphaMode MASK                 -> fully transparent when alpha < alphaCutoff, otherwise opaque\n" +
        "  textures (any channel)         -> IGNORED with a warning: Gltf2 does not decode image bytes\n" +
        "  dielectric specular, normal/occlusion maps, clearcoat, transmission, sheen, KHR_texture_transform -> ignored\n" +
        "Geometry: triangles (lists/strips/fans), POSITION, NORMAL (smooth shading), TEXCOORD_0 (v flipped); one Cycles mesh per\n" +
        "  glTF mesh, one Cycles object per node instance (shared meshes and EXT_mesh_gpu_instancing become instances).\n" +
        "  Skins and morph targets are ignored (rest pose); points and lines are skipped. Each object's shadow-terminator\n" +
        "  geometry offset is set to 0 (the WebGPU backend does not implement it; smooth meshes lit by a light fail otherwise).\n" +
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
    /// Converts the model's default scene (or its first scene) into <paramref name="scene"/>.
    /// Call this on a fresh scene, before the first render.
    /// </summary>
    /// <param name="model">The loaded glTF model.</param>
    /// <param name="scene">The Cycles scene to fill.</param>
    /// <param name="options">Conversion options; null uses the defaults.</param>
    /// <returns>What was created and every simplification made.</returns>
    /// <exception cref="ArgumentNullException"><paramref name="model"/> or <paramref name="scene"/> is null.</exception>
    /// <exception cref="InvalidOperationException">The model has no scene or no triangles.</exception>
    public static GltfConversionResult Convert(ModelRoot model, CyclesScene scene, GltfConversionOptions options = null)
    {
        ArgumentNullException.ThrowIfNull(model);
        ArgumentNullException.ThrowIfNull(scene);
        options ??= new GltfConversionOptions();
        return new Converter(model, scene, options).Run();
    }

    private sealed class Converter
    {
        private readonly ModelRoot model;
        private readonly CyclesScene scene;
        private readonly GltfConversionOptions options;
        private readonly GltfConversionResult result = new();
        private readonly Dictionary<int, ShaderNode> materials = new();
        private readonly Dictionary<int, (MeshNode Mesh, Vector3 Minimum, Vector3 Maximum)> meshes = new();
        private Vector3 minimum = new(float.PositiveInfinity), maximum = new(float.NegativeInfinity);

        internal Converter(ModelRoot model, CyclesScene scene, GltfConversionOptions options)
        {
            this.model = model;
            this.scene = scene;
            this.options = options;
        }

        internal GltfConversionResult Run()
        {
            Scene source = model.DefaultScene ?? model.LogicalScenes.FirstOrDefault();
            if (source == null) throw new InvalidOperationException("The glTF file has no scene.");
            if (options.ApplyDeterministicIntegrator) ApplyDeterministicIntegrator(scene);
            if (model.LogicalAnimations.Count != 0) result.Warn("Animations are ignored; the rest pose is rendered.");

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
            (MeshNode mesh, Vector3 low, Vector3 high) = GetOrCreateMesh(node.Mesh);
            if (mesh == null) return;
            MeshGpuInstancing instancing = node.GetGpuInstancing();
            IEnumerable<Matrix4x4> transforms = instancing == null || instancing.Count == 0
                ? new[] { node.WorldMatrix }
                : instancing.LocalTransforms.Select(local => local.Matrix * node.WorldMatrix);
            foreach (Matrix4x4 world in transforms)
            {
                scene.CreateObject(mesh, Transform3x4.FromMatrix(world));
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

        private (MeshNode Mesh, Vector3 Minimum, Vector3 Maximum) GetOrCreateMesh(Mesh source)
        {
            if (meshes.TryGetValue(source.LogicalIndex, out var existing)) return existing;
            List<float> positions = new(), normals = new(), uvs = new();
            List<uint> indices = new();
            List<int> shaderIndices = new();
            List<ShaderNode> used = new();
            bool allNormals = true, anyUv = false;
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
                List<(int A, int B, int C)> triangles = primitive.GetTriangleIndices().ToList();
                if (triangles.Count == 0) continue;
                ShaderNode shader = GetOrCreateMaterial(primitive.Material);
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
                    }
                }
            }
            if (indices.Count == 0)
            {
                meshes[source.LogicalIndex] = (null, low, high);
                return (null, low, high);
            }
            MeshNode mesh = scene.CreateMesh(positions.ToArray(), indices.ToArray(), used[0]);
            if (used.Count > 1)
            {
                mesh.UsedShaders = used.ToArray();
                mesh.Shader = shaderIndices.ToArray();
            }
            if (allNormals)
            {
                mesh.SetAttribute("N", AttributeValue.Normal, AttributeDomain.VertexNormal, AttributeFlags.Standard, normals.ToArray());
                mesh.Smooth = Enumerable.Repeat(true, shaderIndices.Count).ToArray();
            }
            if (anyUv) mesh.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard, uvs.ToArray());
            ++result.Meshes;
            result.Triangles += shaderIndices.Count;
            var created = (mesh, low, high);
            meshes[source.LogicalIndex] = created;
            return created;
        }

        private ShaderNode GetOrCreateMaterial(Material material)
        {
            int key = material?.LogicalIndex ?? -1;
            if (materials.TryGetValue(key, out ShaderNode existing)) return existing;
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

            ShaderNode shader = scene.CreateShader();
            shader.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
            ShaderOutput surface;
            if (unlit)
            {
                EmissionNode emission = shader.Add<EmissionNode>();
                emission.Color = rgb;
                emission.Strength = 1f;
                surface = emission.EmissionOutput;
            }
            else
            {
                DiffuseBsdfNode diffuse = shader.Add<DiffuseBsdfNode>();
                diffuse.Color = rgb;
                surface = diffuse.BsdfOutput;
                if (metallic > 0f)
                {
                    GlossyBsdfNode glossy = shader.Add<GlossyBsdfNode>();
                    glossy.Distribution = GlossyBsdfDistribution.Ggx;
                    glossy.Color = rgb;
                    glossy.Roughness = Math.Clamp(roughness, 0f, 1f);
                    if (metallic >= 1f) surface = glossy.BsdfOutput;
                    else
                    {
                        MixClosureNode mix = shader.Add<MixClosureNode>();
                        mix.Fac = metallic;
                        shader.Connect(diffuse.BsdfOutput, mix.Closure1Input);
                        shader.Connect(glossy.BsdfOutput, mix.Closure2Input);
                        surface = mix.ClosureOutput;
                    }
                }
                if (emissive != Vector3.Zero && emissiveStrength > 0f)
                {
                    EmissionNode emission = shader.Add<EmissionNode>();
                    emission.Color = emissive;
                    emission.Strength = emissiveStrength;
                    AddClosureNode add = shader.Add<AddClosureNode>();
                    shader.Connect(surface, add.Closure1Input);
                    shader.Connect(emission.EmissionOutput, add.Closure2Input);
                    surface = add.ClosureOutput;
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
                TransparentBsdfNode transparent = shader.Add<TransparentBsdfNode>();
                transparent.Color = Vector3.One;
                MixClosureNode mix = shader.Add<MixClosureNode>();
                mix.Fac = coverage;
                shader.Connect(transparent.BsdfOutput, mix.Closure1Input);
                shader.Connect(surface, mix.Closure2Input);
                surface = mix.ClosureOutput;
            }
            shader.Connect(surface, shader.Output.SurfaceInput);
            materials[key] = shader;
            ++result.Materials;
            return shader;
        }

        private static float Factor(MaterialChannel channel, string name, float fallback)
        {
            foreach (IMaterialParameter parameter in channel.Parameters)
                if (parameter.Name == name && parameter.Value is float value) return value;
            return fallback;
        }

        private void AddLight(PunctualLight light, Matrix4x4 world)
        {
            Vector3 strength = light.Color * light.Intensity;
            LightNode node;
            switch (light.LightType)
            {
                case PunctualLightType.Directional:
                    node = scene.CreateLight<SunLightNode>();
                    break;
                case PunctualLightType.Spot:
                    SpotLightNode spot = scene.CreateLight<SpotLightNode>();
                    float outer = Math.Clamp(light.OuterConeAngle, 1e-4f, MathF.PI / 2);
                    spot.Angle = 2f * outer;
                    spot.Smooth = Math.Clamp((outer - light.InnerConeAngle) / outer, 0f, 1f);
                    strength *= 4f * MathF.PI;
                    node = spot;
                    break;
                default:
                    node = scene.CreateLight<PointLightNode>();
                    strength *= 4f * MathF.PI;
                    break;
            }
            if (!float.IsPositiveInfinity(light.Range) && light.Range > 0) result.Warn("Punctual light range is ignored (Cycles lights have no cutoff).");
            node.Strength = strength;
            node.UsedShaders = new[] { LightShader() };
            scene.CreateObject(node, Transform3x4.FromMatrix(RemoveScale(world)));
            ++result.Lights;
        }

        private ShaderNode lightShader;

        private ShaderNode LightShader()
        {
            if (lightShader != null) return lightShader;
            lightShader = scene.CreateShader();
            EmissionNode emission = lightShader.Add<EmissionNode>();
            emission.Color = Vector3.One;
            emission.Strength = 1f;
            lightShader.Connect(emission.EmissionOutput, lightShader.Output.SurfaceInput);
            return lightShader;
        }

        private void AddDefaultSun()
        {
            // Light travels along the light's local -Z axis (the glTF and Cycles convention).
            Vector3 travel = Vector3.Normalize(new Vector3(-.35f, -1f, -.55f));
            Matrix4x4 frame = Basis(-travel, Vector3.Zero);
            SunLightNode sun = scene.CreateLight<SunLightNode>();
            sun.Strength = new Vector3(options.DefaultSunStrength);
            sun.Angle = .05f;
            sun.UsedShaders = new[] { LightShader() };
            scene.CreateObject(sun, Transform3x4.FromMatrix(frame));
            ++result.Lights;
        }

        private void AddBackground()
        {
            ShaderNode world = scene.CreateShader();
            BackgroundShaderNode background = world.Add<BackgroundShaderNode>();
            background.Color = options.BackgroundColor;
            background.Strength = options.BackgroundStrength;
            world.Connect(background.BackgroundOutput, world.Output.SurfaceInput);
            world.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
            scene.Background.Shader = world;
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
            CameraNode camera = scene.Camera;
            camera.CameraType = CameraCameraType.Perspective;
            camera.Matrix = Transform3x4.FromMatrix(flipped);
            float aspect = options.Width / (float)Math.Max(1u, options.Height);
            camera.Fov = aspect >= 1f ? verticalFov : 2f * MathF.Atan(MathF.Tan(verticalFov / 2f) * aspect);
        }
    }
}
