// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.Numerics;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Lights;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.Rendering;

namespace CodeBrix.Graphics3D.Cycles.Validation;

// The small scenes rendered on the GPU (WebGPU) backend and on the CPU reference backend and
// compared with the repository criteria. Shared source: compiled into the managed test project
// (Linux x64) and into the Android validation app (tools/hosttests/AndroidHost), so both run the
// exact same scene descriptions. Every scene stays inside what the WebGPU backend renders today:
// perspective camera, Combined pass only, light tree/denoise/adaptive/guiding off, and a zero
// shadow-terminator geometry offset on smooth-shaded objects.
internal static class WebGpuValidationScenes
{
    internal const string Glass = "glass";
    internal const string DiffuseSun = "diffuse-sun";
    internal const string TexturedPlane = "textured-plane";

    internal static readonly string[] Names = [Glass, DiffuseSun, TexturedPlane];

    internal static readonly RenderSettings Settings = new(32, 24, 16);

    internal static void Build(CyclesScene scene, string name)
    {
        IntegratorNode integrator = scene.Integrator;
        integrator.UseGuiding = false;
        integrator.UseLightTree = false;
        integrator.UseAdaptiveSampling = false;
        integrator.UseDenoise = false;
        integrator.CausticsReflective = false;
        integrator.CausticsRefractive = false;
        integrator.MinBounce = 2;
        integrator.MaxBounce = 6;
        integrator.MaxDiffuseBounce = 4;
        integrator.SamplingPattern = IntegratorSamplingPattern.TabulatedSobol;
        scene.Camera.CameraType = CameraCameraType.Perspective;
        scene.Camera.Fov = .9f;
        // Cycles cameras look along +Z: a camera above and in front of the origin, tilted down.
        Matrix4x4 camera = Matrix4x4.CreateRotationX(.45f) * Matrix4x4.CreateTranslation(0, 2.2f, -4.2f);
        scene.Camera.Matrix = Transform3x4.FromMatrix(camera);

        ShaderNode world = scene.CreateShader();
        BackgroundShaderNode background = world.Add<BackgroundShaderNode>();
        background.Color = new Vector3(.3f, .35f, .45f);
        background.Strength = 1f;
        world.Connect(background.BackgroundOutput, world.Output.SurfaceInput);
        world.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        scene.Background.Shader = world;

        switch (name)
        {
            case Glass:
                AddFloor(scene, Diffuse(scene, new Vector3(.7f, .65f, .6f)));
                AddSun(scene);
                ShaderNode glass = scene.CreateShader();
                GlassBsdfNode bsdf = glass.Add<GlassBsdfNode>();
                bsdf.Color = new Vector3(.95f, .97f, 1f);
                bsdf.Ior = 1.45f;
                bsdf.Roughness = .05f;
                bsdf.Distribution = GlassBsdfDistribution.Ggx;
                glass.Connect(bsdf.BsdfOutput, glass.Output.SurfaceInput);
                glass.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
                AddSphere(scene, glass, new Vector3(0, .8f, 0), .8f);
                break;
            case DiffuseSun:
                AddFloor(scene, Diffuse(scene, new Vector3(.6f, .6f, .6f)));
                AddSun(scene);
                AddBox(scene, Diffuse(scene, new Vector3(.8f, .3f, .1f)), new Vector3(-.7f, 0, .2f), .9f);
                AddBox(scene, Diffuse(scene, new Vector3(.1f, .4f, .8f)), new Vector3(.8f, 0, -.3f), .6f);
                break;
            case TexturedPlane:
                AddSun(scene);
                ShaderNode material = scene.CreateShader();
                DiffuseBsdfNode diffuse = material.Add<DiffuseBsdfNode>();
                ImageTextureNode texture = material.Add<ImageTextureNode>();
                UvmapNode uv = material.Add<UvmapNode>();
                texture.Interpolation = ImageTextureInterpolation.Closest;
                material.Connect(uv.UvOutput, texture.VectorInput);
                material.Connect(texture.ColorOutput, diffuse.ColorInput);
                material.Connect(diffuse.BsdfOutput, material.Output.SurfaceInput);
                material.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
                MeshNode plane = AddFloor(scene, material);
                plane.SetAttribute("uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard,
                    [0, 0, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1]);
                using (CyclesImage image = scene.CreateImage<byte>(8, 8, ImageFormat.Byte4, Checker(8, 8)))
                    texture.Bind(image);
                break;
            default:
                throw new ArgumentException("Unknown validation scene: " + name, nameof(name));
        }
    }

    // An 8-bit RGBA checker with a colour ramp, row 0 = v 0.
    internal static byte[] Checker(int width, int height)
    {
        byte[] pixels = new byte[width * height * 4];
        for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
        {
            int i = (y * width + x) * 4;
            bool dark = ((x + y) & 1) == 0;
            pixels[i] = (byte)(dark ? 30 : 40 + 200 * x / width);
            pixels[i + 1] = (byte)(dark ? 30 : 220 - 150 * y / height);
            pixels[i + 2] = (byte)(dark ? 60 : 90);
            pixels[i + 3] = 255;
        }
        return pixels;
    }

    // Practical: |a - b| > 5e-5 + 2e-4 |b| fails; strict: |a - b| > 1e-7 + 1e-7 |b| counts.
    internal static (double Maximum, int Practical, int Strict) Compare(ReadOnlySpan<float> actual, ReadOnlySpan<float> reference)
    {
        if (actual.Length != reference.Length) throw new ArgumentException("The films differ in size.");
        double maximum = 0;
        int practical = 0, strict = 0;
        for (int i = 0; i < actual.Length; ++i)
        {
            if (!float.IsFinite(actual[i]) || !float.IsFinite(reference[i])) throw new ArgumentException("Non-finite film value at " + i);
            double error = Math.Abs((double)actual[i] - reference[i]);
            maximum = Math.Max(maximum, error);
            if (error > 5e-5 + 2e-4 * Math.Abs(reference[i])) ++practical;
            if (error > 1e-7 + 1e-7 * Math.Abs(reference[i])) ++strict;
        }
        return (maximum, practical, strict);
    }

    private static ShaderNode Diffuse(CyclesScene scene, Vector3 color)
    {
        ShaderNode shader = scene.CreateShader();
        DiffuseBsdfNode diffuse = shader.Add<DiffuseBsdfNode>();
        diffuse.Color = color;
        shader.Connect(diffuse.BsdfOutput, shader.Output.SurfaceInput);
        shader.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        return shader;
    }

    private static MeshNode AddFloor(CyclesScene scene, ShaderNode shader)
    {
        MeshNode mesh = scene.CreateMesh([-3, 0, -3, 3, 0, -3, 3, 0, 3, -3, 0, 3], [0, 2, 1, 0, 3, 2], shader);
        scene.CreateObject(mesh);
        return mesh;
    }

    private static void AddSun(CyclesScene scene)
    {
        ShaderNode emission = scene.CreateShader();
        EmissionNode emitter = emission.Add<EmissionNode>();
        emitter.Color = Vector3.One;
        emitter.Strength = 1f;
        emission.Connect(emitter.EmissionOutput, emission.Output.SurfaceInput);
        SunLightNode sun = scene.CreateLight<SunLightNode>();
        sun.Strength = new Vector3(3f, 2.9f, 2.7f);
        sun.Angle = .05f;
        sun.UsedShaders = [emission];
        // Sun light travels along the object's -Z: tilt -Z towards the floor (-Y).
        scene.CreateObject(sun, Transform3x4.FromMatrix(Matrix4x4.CreateRotationX(-1.1f) * Matrix4x4.CreateRotationY(.5f)));
    }

    private static void AddBox(CyclesScene scene, ShaderNode shader, Vector3 position, float size)
    {
        float h = size / 2;
        float[] p =
        [
            -h, 0, -h, h, 0, -h, h, size, -h, -h, size, -h,
            -h, 0, h, h, 0, h, h, size, h, -h, size, h
        ];
        uint[] i = [0, 2, 1, 0, 3, 2, 4, 5, 6, 4, 6, 7, 0, 4, 7, 0, 7, 3, 1, 2, 6, 1, 6, 5, 3, 7, 6, 3, 6, 2, 0, 1, 5, 0, 5, 4];
        MeshNode mesh = scene.CreateMesh(p, i, shader);
        scene.CreateObject(mesh, Transform3x4.CreateTranslation(position.X, position.Y, position.Z));
    }

    private static void AddSphere(CyclesScene scene, ShaderNode shader, Vector3 center, float radius)
    {
        const int rings = 10, segments = 16;
        List<float> positions = new(), normals = new();
        List<uint> indices = new();
        for (int r = 0; r <= rings; ++r)
        {
            float theta = MathF.PI * r / rings;
            for (int s = 0; s <= segments; ++s)
            {
                float phi = 2 * MathF.PI * s / segments;
                Vector3 n = new(MathF.Sin(theta) * MathF.Cos(phi), MathF.Cos(theta), MathF.Sin(theta) * MathF.Sin(phi));
                positions.Add(n.X * radius); positions.Add(n.Y * radius); positions.Add(n.Z * radius);
                normals.Add(n.X); normals.Add(n.Y); normals.Add(n.Z);
            }
        }
        for (int r = 0; r < rings; ++r)
        for (int s = 0; s < segments; ++s)
        {
            uint a = (uint)(r * (segments + 1) + s), b = a + segments + 1;
            indices.Add(a); indices.Add(a + 1); indices.Add(b);
            indices.Add(a + 1); indices.Add(b + 1); indices.Add(b);
        }
        MeshNode mesh = scene.CreateMesh(positions.ToArray(), indices.ToArray(), shader);
        mesh.SetAttribute("N", AttributeValue.Normal, AttributeDomain.VertexNormal, AttributeFlags.Standard, normals.ToArray());
        bool[] smooth = new bool[indices.Count / 3];
        Array.Fill(smooth, true);
        mesh.Smooth = smooth;
        // Default shadow-terminator offsets (geometry 0.1): the WebGPU backend implements them.
        scene.CreateObject(mesh, Transform3x4.CreateTranslation(center.X, center.Y, center.Z));
    }
}
