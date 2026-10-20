// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Exercises the managed wrapper against the native host built in this working
// tree with the CPU reference backend. Skips (never fails) when that build is absent.
public class CyclesHostTests
{
    private static void RequireNativeHost()
    {
        if (!NativeHostLibrary.EnsureLoaded()) Assert.Skip(NativeHostLibrary.SkipReason);
    }

    private static CyclesHost CreateCpuHost()
    {
        RequireNativeHost();
        return new CyclesHost(Backend.CpuReference);
    }

    [Fact]
    public void can_create_cpu_reference_host()
    {
        //Act
        using CyclesHost host = CreateCpuHost();

        //Assert
        host.IsRendering.Should().BeFalse();
    }

    [Fact]
    public void Cancel_returns_false_when_idle()
    {
        //Arrange
        using CyclesHost host = CreateCpuHost();

        //Act
        bool cancelled = host.Cancel();

        //Assert
        cancelled.Should().BeFalse();
    }

    [Fact]
    public void instance_disposed_correctly()
    {
        //Arrange
        CyclesHost host = CreateCpuHost();

        //Act
        host.Dispose();
        Action secondDispose = host.Dispose;
        Action useAfterDispose = () => host.SceneNode("camera");

        //Assert
        secondDispose.Should().NotThrow();
        useAfterDispose.Should().Throw<ObjectDisposedException>();
        secondDispose.Should().NotThrow();
    }

    [Fact]
    public void RegistryJson_round_trips_through_a_json_parser()
    {
        //Arrange
        RequireNativeHost();

        //Act
        string registry = CyclesHost.RegistryJson();
        JsonNode parsed = JsonNode.Parse(registry);
        JsonNode reparsed = JsonNode.Parse(parsed.ToJsonString());

        //Assert
        JsonNode.DeepEquals(parsed, reparsed).Should().BeTrue();
        JsonArray nodeTypes = parsed["nodeTypes"].AsArray();
        nodeTypes.Count.Should().BeGreaterThan(0);
        nodeTypes.All(node => node["inputs"] is JsonArray && node["outputs"] is JsonArray).Should().BeTrue();
    }

    [Fact]
    public void RegistryJson_is_stable_between_calls()
    {
        //Arrange
        RequireNativeHost();

        //Act
        string first = CyclesHost.RegistryJson();
        string second = CyclesHost.RegistryJson();

        //Assert
        second.Should().Be(first);
    }

    [Fact]
    public void DeviceReportJson_reports_the_cpu_backend()
    {
        //Arrange
        using CyclesHost host = CreateCpuHost();

        //Act
        using JsonDocument report = JsonDocument.Parse(host.DeviceReportJson());

        //Assert
        report.RootElement.GetProperty("backend").GetString().Should().Be("cpu");
    }

    [Fact]
    public void SetFloats_round_trips_a_transform_array()
    {
        //Arrange
        using CyclesHost host = CreateCpuHost();
        Node instance = host.CreateNode("object");
        float[] motion = [1, 0, 0, 2, 0, 1, 0, 3, 0, 0, 1, 4, 2, 0, 0, 5, 0, 2, 0, 6, 0, 0, 2, 7];

        //Act
        host.SetFloats(instance, "motion", motion);
        bool fitsEmpty = host.GetFloats(instance, "motion", Span<float>.Empty, out ulong required);
        float[] copied = new float[(int)required];
        bool copiedAll = host.GetFloats(instance, "motion", copied, out _);
        PropertyInfo info = host.Property(instance, "motion");

        //Assert
        fitsEmpty.Should().BeFalse();
        required.Should().Be(24ul);
        copiedAll.Should().BeTrue();
        copied.Should().Equal(motion);
        info.ValueType.Should().Be((uint)AttributeValue.Transform);
        info.Components.Should().Be(12u);
        info.Count.Should().Be(2ul);
    }

    [Fact]
    public void SetString_round_trips_utf8_text()
    {
        //Arrange
        using CyclesHost host = CreateCpuHost();
        Node instance = host.CreateNode("object");
        const string name = "Matériau — 三角形";

        //Act
        host.SetString(instance, "asset_name", name);
        host.GetUtf8(instance, "asset_name", 0, Span<byte>.Empty, out ulong size);
        byte[] utf8 = new byte[(int)size];
        bool copied = host.GetUtf8(instance, "asset_name", 0, utf8, out _);

        //Assert
        copied.Should().BeTrue();
        utf8[^1].Should().Be((byte)0);
        Encoding.UTF8.GetString(utf8, 0, utf8.Length - 1).Should().Be(name);
    }

    [Fact]
    public void SetUInt_round_trips_the_full_64_bit_range()
    {
        //Arrange
        using CyclesHost host = CreateCpuHost();
        Node instance = host.CreateNode("object");

        //Act
        host.SetUInt(instance, "light_set_membership", ulong.MaxValue);

        //Assert
        host.GetUInt(instance, "light_set_membership").Should().Be(ulong.MaxValue);
    }

    [Fact]
    public void SetNodes_round_trips_node_identifiers()
    {
        //Arrange
        using CyclesHost host = CreateCpuHost();
        Node instance = host.CreateNode("object");
        Node mesh = host.CreateNode("mesh");
        ulong[] nodes = new ulong[1];

        //Act
        host.SetNodes(instance, "geometry", [mesh]);
        bool copied = host.GetNodes(instance, "geometry", nodes, out ulong required);

        //Assert
        copied.Should().BeTrue();
        required.Should().Be(1ul);
        nodes[0].Should().Be(mesh.Value);
    }

    [Fact]
    public void Mesh_rejects_incomplete_triangles()
    {
        //Arrange
        using CyclesHost host = CreateCpuHost();
        Node mesh = host.CreateNode("mesh");

        //Act
        Action act = () => host.Mesh(mesh, [0, 0, 0, 1, 0, 0, 0, 1, 0], [0, 1]);

        //Assert
        act.Should().Throw<ArgumentException>();
    }

    [Fact]
    public void SetFloats_rejects_a_node_from_another_host()
    {
        //Arrange
        using CyclesHost host = CreateCpuHost();
        using CyclesHost other = CreateCpuHost();
        Node foreignCamera = other.SceneNode("camera");

        //Act
        Action act = () => host.SetFloats(foreignCamera, "fov", [1f]);

        //Assert
        act.Should().Throw<NativeFailure>().Which.Status.Should().Be(1);
    }

    [Fact]
    public void Render_produces_finite_pixels_for_the_readme_scene()
    {
        //Arrange
        using CyclesHost host = CreateCpuHost();
        Node camera = host.SceneNode("camera");
        host.SetFloats(camera, "fov", [1.1f]);
        host.SetFloats(camera, "matrix", [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, -4]);
        Node material = host.CreateNode("shader");
        Node diffuse = host.ShaderNode(material, "diffuse_bsdf");
        Node color = host.ShaderNode(material, "color");
        host.SetFloats(color, "value", [.7f, .2f, .1f]);
        host.Connect(color, "Color", diffuse, "Color");
        host.Connect(diffuse, "BSDF", host.ShaderOutput(material), "Surface");
        Node mesh = host.CreateNode("mesh");
        host.SetNodes(mesh, "used_shaders", [material]);
        host.Mesh(mesh, [-1.5f, -1, 0, 1.5f, -1, 0, 0, 1.5f, 0], [0, 1, 2]);
        Node triangle = host.CreateNode("object");
        host.SetNodes(triangle, "geometry", [mesh]);
        Node world = host.CreateNode("shader");
        Node background = host.ShaderNode(world, "background_shader");
        host.SetFloats(background, "color", [.25f, .4f, .6f]);
        host.Connect(background, "Background", host.ShaderOutput(world), "Surface");
        host.SetNodes(host.SceneNode("background"), "shader", [world]);
        float[] rgba = new float[32 * 24 * 4];
        Array.Fill(rgba, -99f);

        //Act
        host.Render(32, 24, 8, rgba);

        //Assert
        rgba.All(float.IsFinite).Should().BeTrue();
        rgba.Any(value => value == -99f).Should().BeFalse();
        rgba.Where((_, index) => index % 4 == 3).All(alpha => alpha >= 0f && alpha <= 1f).Should().BeTrue();
    }

    [Fact]
    public void Render_leaves_the_output_untouched_when_rejected()
    {
        //Arrange
        using CyclesHost host = CreateCpuHost();
        float[] rgba = new float[32 * 24 * 4 - 1];
        Array.Fill(rgba, -99f);

        //Act
        Action act = () => host.Render(32, 24, 1, rgba);

        //Assert
        act.Should().Throw<NativeFailure>().Which.Status.Should().Be(1);
        rgba.All(value => value == -99f).Should().BeTrue();
    }
}
