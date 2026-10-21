// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Text;
using CodeBrix.Graphics3D.Cycles.Interop;
using CodeBrix.Graphics3D.Cycles.Rendering;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// The status-code interop tier against the native host built in this working tree.
public class CyclesAbiTests
{
    [Fact]
    public void Render_through_the_interop_tier_matches_the_typed_facade_bitwise()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        TestScenes.BuildTyped(scene);
        RenderSettings settings = new(32, 24, 8);
        CyclesAbi.Create(Backend.CpuReference, 1, out ulong host).Should().Be(NativeStatus.Ok);
        float[] raw = new float[settings.FloatCount];

        //Act
        FilmBuffer typed = scene.Render(settings);
        NativeStatus status;
        try
        {
            TestScenes.BuildRaw(host);
            status = CyclesAbi.Render(host, settings.Width, settings.Height, settings.Samples, raw);
        }
        finally
        {
            CyclesAbi.Destroy(host);
        }

        //Assert
        status.Should().Be(NativeStatus.Ok);
        typed.Span.SequenceEqual(raw).Should().BeTrue();
    }

    [Fact]
    public void failures_return_status_codes_and_the_error_text()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        CyclesAbi.Create(Backend.CpuReference, 1, out ulong host).Should().Be(NativeStatus.Ok);

        try
        {
            //Act
            NativeStatus unknownType = CyclesAbi.CreateNode(host, "no_such_type", out ulong node);
            string error = CyclesAbi.LastError();
            NativeStatus idleCancel = CyclesAbi.Cancel(host);

            //Assert
            unknownType.Should().Be(NativeStatus.Unsupported);
            node.Should().Be(0UL);
            error.Should().NotBeNullOrEmpty();
            idleCancel.Should().Be(NativeStatus.NotReady);
        }
        finally
        {
            CyclesAbi.Destroy(host);
        }
    }

    [Fact]
    public void GetString_follows_the_size_query_protocol()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        CyclesAbi.Create(Backend.CpuReference, 1, out ulong host).Should().Be(NativeStatus.Ok);
        try
        {
            CyclesAbi.CreateNode(host, "object", out ulong instance).Should().Be(NativeStatus.Ok);
            CyclesAbi.SetString(host, instance, "asset_name", "Matériau").Should().Be(NativeStatus.Ok);

            //Act
            NativeStatus query = CyclesAbi.GetString(host, instance, "asset_name", 0, Span<byte>.Empty, out ulong required);
            byte[] utf8 = new byte[required];
            NativeStatus copy = CyclesAbi.GetString(host, instance, "asset_name", 0, utf8, out required);
            byte[] typeName = new byte[16];
            NativeStatus type = CyclesAbi.NodeType(host, instance, typeName, out ulong typeLength);

            //Assert
            query.Should().Be(NativeStatus.SmallBuffer);
            copy.Should().Be(NativeStatus.Ok);
            Encoding.UTF8.GetString(utf8, 0, (int)required - 1).Should().Be("Matériau");
            type.Should().Be(NativeStatus.Ok);
            Encoding.UTF8.GetString(typeName, 0, (int)typeLength - 1).Should().Be("object");
        }
        finally
        {
            CyclesAbi.Destroy(host);
        }
    }

    [Fact]
    public void AbiVersion_matches_the_expected_version()
    {
        //Arrange
        TestScenes.RequireNativeHost();

        //Act
        uint abi = CyclesAbi.AbiVersion();

        //Assert
        abi.Should().Be(CyclesAbi.ExpectedAbiVersion);
    }
}
