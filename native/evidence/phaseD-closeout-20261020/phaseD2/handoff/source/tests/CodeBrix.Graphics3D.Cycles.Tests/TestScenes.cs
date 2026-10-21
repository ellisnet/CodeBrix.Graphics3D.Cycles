// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System.Numerics;
using CodeBrix.Graphics3D.Cycles.Generated.Geometry;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Generated.Shaders;
using CodeBrix.Graphics3D.Cycles.Interop;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Builds the same lit-triangle scene (the README scene with deterministic integrator settings)
// through each managed tier, so renders can be compared bit for bit.
internal static class TestScenes
{
    internal static readonly float[] TrianglePositions = [-1.5f, -1, 0, 1.5f, -1, 0, 0, 1.5f, 0];
    internal static readonly uint[] TriangleIndices = [0, 1, 2];

    internal static void RequireNativeHost()
    {
        if (!NativeHostLibrary.EnsureLoaded()) Assert.Skip(NativeHostLibrary.SkipReason);
    }

    internal static CyclesScene CreateCpuScene()
    {
        RequireNativeHost();
        return new CyclesScene(CyclesBackend.CpuReference);
    }

    // The typed facade.
    internal static (ShaderNode Material, DiffuseBsdfNode Diffuse, MeshNode Mesh) BuildTyped(CyclesScene scene)
    {
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
        scene.Camera.Fov = 1.1f;
        scene.Camera.Matrix = Transform3x4.CreateTranslation(0, 0, -4);

        ShaderNode material = scene.CreateShader();
        DiffuseBsdfNode diffuse = material.Add<DiffuseBsdfNode>();
        ColorNode color = material.Add<ColorNode>();
        color.Value = new Vector3(.7f, .2f, .1f);
        diffuse.Roughness = .3f;
        material.Connect(color.ColorOutput, diffuse.ColorInput);
        material.Connect(diffuse.BsdfOutput, material.Output.SurfaceInput);
        material.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;

        MeshNode mesh = scene.CreateMesh(TrianglePositions, TriangleIndices, material);
        scene.CreateObject(mesh);

        ShaderNode world = scene.CreateShader();
        BackgroundShaderNode background = world.Add<BackgroundShaderNode>();
        background.Color = new Vector3(.25f, .4f, .6f);
        background.Strength = .8f;
        world.Connect(background.BackgroundOutput, world.Output.SurfaceInput);
        world.EmissionSamplingMethod = ShaderEmissionSamplingMethod.None;
        scene.Background.Shader = world;
        return (material, diffuse, mesh);
    }

    // The by-name CyclesHost tier.
    internal static void BuildByName(CyclesHost host)
    {
        Node integrator = host.SceneNode("integrator");
        foreach (string setting in new[] { "use_guiding", "use_light_tree", "use_adaptive_sampling", "use_denoise", "caustics_reflective", "caustics_refractive" })
            host.SetInts(integrator, setting, [0]);
        host.SetInts(integrator, "min_bounce", [2]);
        host.SetInts(integrator, "max_bounce", [4]);
        host.SetInts(integrator, "max_diffuse_bounce", [4]);
        host.SetString(integrator, "sampling_pattern", "tabulated_sobol");
        Node camera = host.SceneNode("camera");
        host.SetFloats(camera, "fov", [1.1f]);
        host.SetFloats(camera, "matrix", [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -4]);
        Node material = host.CreateNode("shader"), diffuse = host.ShaderNode(material, "diffuse_bsdf"), color = host.ShaderNode(material, "color");
        host.SetFloats(color, "value", [.7f, .2f, .1f]);
        host.SetFloats(diffuse, "roughness", [.3f]);
        host.Connect(color, "Color", diffuse, "Color");
        host.Connect(diffuse, "BSDF", host.ShaderOutput(material), "Surface");
        host.SetString(material, "emission_sampling_method", "none");
        Node mesh = host.CreateNode("mesh");
        host.SetNodes(mesh, "used_shaders", [material]);
        host.Mesh(mesh, TrianglePositions, TriangleIndices);
        Node instance = host.CreateNode("object");
        host.SetNodes(instance, "geometry", [mesh]);
        Node world = host.CreateNode("shader"), background = host.ShaderNode(world, "background_shader");
        host.SetFloats(background, "color", [.25f, .4f, .6f]);
        host.SetFloats(background, "strength", [.8f]);
        host.Connect(background, "Background", host.ShaderOutput(world), "Surface");
        host.SetString(world, "emission_sampling_method", "none");
        host.SetNodes(host.SceneNode("background"), "shader", [world]);
    }

    // The status-code interop tier; every call must return Ok.
    internal static void BuildRaw(ulong host)
    {
        static void Ok(NativeStatus status) => Assert.Equal(NativeStatus.Ok, status);
        Ok(CyclesAbi.SceneNode(host, "integrator", out ulong integrator));
        foreach (string setting in new[] { "use_guiding", "use_light_tree", "use_adaptive_sampling", "use_denoise", "caustics_reflective", "caustics_refractive" })
            Ok(CyclesAbi.SetInts(host, integrator, setting, [0]));
        Ok(CyclesAbi.SetInts(host, integrator, "min_bounce", [2]));
        Ok(CyclesAbi.SetInts(host, integrator, "max_bounce", [4]));
        Ok(CyclesAbi.SetInts(host, integrator, "max_diffuse_bounce", [4]));
        Ok(CyclesAbi.SetString(host, integrator, "sampling_pattern", "tabulated_sobol"));
        Ok(CyclesAbi.SceneNode(host, "camera", out ulong camera));
        Ok(CyclesAbi.SetFloats(host, camera, "fov", [1.1f]));
        Ok(CyclesAbi.SetFloats(host, camera, "matrix", [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -4]));
        Ok(CyclesAbi.CreateNode(host, "shader", out ulong material));
        Ok(CyclesAbi.ShaderNode(host, material, "diffuse_bsdf", out ulong diffuse));
        Ok(CyclesAbi.ShaderNode(host, material, "color", out ulong color));
        Ok(CyclesAbi.SetFloats(host, color, "value", [.7f, .2f, .1f]));
        Ok(CyclesAbi.SetFloats(host, diffuse, "roughness", [.3f]));
        Ok(CyclesAbi.ShaderOutput(host, material, out ulong output));
        Ok(CyclesAbi.Connect(host, color, "Color", diffuse, "Color"));
        Ok(CyclesAbi.Connect(host, diffuse, "BSDF", output, "Surface"));
        Ok(CyclesAbi.SetString(host, material, "emission_sampling_method", "none"));
        Ok(CyclesAbi.CreateNode(host, "mesh", out ulong mesh));
        Ok(CyclesAbi.SetNodes(host, mesh, "used_shaders", [material]));
        Ok(CyclesAbi.Mesh(host, mesh, TrianglePositions, TriangleIndices));
        Ok(CyclesAbi.CreateNode(host, "object", out ulong instance));
        Ok(CyclesAbi.SetNodes(host, instance, "geometry", [mesh]));
        Ok(CyclesAbi.CreateNode(host, "shader", out ulong world));
        Ok(CyclesAbi.ShaderNode(host, world, "background_shader", out ulong background));
        Ok(CyclesAbi.SetFloats(host, background, "color", [.25f, .4f, .6f]));
        Ok(CyclesAbi.SetFloats(host, background, "strength", [.8f]));
        Ok(CyclesAbi.ShaderOutput(host, world, out ulong worldOutput));
        Ok(CyclesAbi.Connect(host, background, "Background", worldOutput, "Surface"));
        Ok(CyclesAbi.SetString(host, world, "emission_sampling_method", "none"));
        Ok(CyclesAbi.SceneNode(host, "background", out ulong backgroundNode));
        Ok(CyclesAbi.SetNodes(host, backgroundNode, "shader", [world]));
    }
}
