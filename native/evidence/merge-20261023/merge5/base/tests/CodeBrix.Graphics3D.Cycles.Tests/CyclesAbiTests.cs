// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using CodeBrix.Graphics3D.Cycles.Interop;
using CodeBrix.Graphics3D.Cycles.Rendering;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// The status-code interop tier against the native host built in this working tree.
public unsafe class CyclesAbiTests
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

    [Fact]
    public void Version_ResolveBackend_and_GetBackend_report_the_native_values()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        CyclesAbi.Create(Backend.CpuReference, 1, out ulong host).Should().Be(NativeStatus.Ok);
        try
        {
            //Act
            NativeStatus version = CyclesAbi.Version(out uint abi, out string library, out string revision);
            NativeStatus resolved = CyclesAbi.ResolveBackend(Backend.Gpu, out Backend gpu);
            NativeStatus concrete = CyclesAbi.GetBackend(host, out Backend backend);

            //Assert
            version.Should().Be(NativeStatus.Ok);
            abi.Should().Be(CyclesAbi.ExpectedAbiVersion);
            library.Should().NotBeNullOrEmpty();
            revision.Length.Should().Be(40);
            resolved.Should().Be(NativeStatus.Ok);
            gpu.Should().Be(OperatingSystem.IsMacOS() ? Backend.Metal : Backend.WebGpu);
            concrete.Should().Be(NativeStatus.Ok);
            backend.Should().Be(Backend.CpuReference);
        }
        finally
        {
            CyclesAbi.Destroy(host);
        }
    }

    [Fact]
    public void CapabilitiesJson_follows_the_size_query_protocol_for_the_binary_and_a_host()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        CyclesAbi.Create(Backend.CpuReference, 1, out ulong host).Should().Be(NativeStatus.Ok);
        try
        {
            //Act
            NativeStatus query = CyclesAbi.CapabilitiesJson(0, Span<byte>.Empty, out ulong binarySize);
            byte[] binary = new byte[binarySize];
            NativeStatus copy = CyclesAbi.CapabilitiesJson(0, binary, out _);
            CyclesAbi.CapabilitiesJson(host, Span<byte>.Empty, out ulong hostSize);
            byte[] withHost = new byte[hostSize];
            NativeStatus hostCopy = CyclesAbi.CapabilitiesJson(host, withHost, out _);

            //Assert
            query.Should().Be(NativeStatus.SmallBuffer);
            copy.Should().Be(NativeStatus.Ok);
            hostCopy.Should().Be(NativeStatus.Ok);
            Encoding.UTF8.GetString(binary).Should().NotContain("\"host\"");
            Encoding.UTF8.GetString(withHost).Should().Contain("\"host\"");
        }
        finally
        {
            CyclesAbi.Destroy(host);
        }
    }

    [Fact]
    public void DeleteNode_refuses_a_referenced_node_until_the_reference_is_cleared()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        CyclesAbi.Create(Backend.CpuReference, 1, out ulong host).Should().Be(NativeStatus.Ok);
        try
        {
            CyclesAbi.CreateNode(host, "mesh", out ulong mesh).Should().Be(NativeStatus.Ok);
            CyclesAbi.CreateNode(host, "object", out ulong instance).Should().Be(NativeStatus.Ok);
            CyclesAbi.SetNodes(host, instance, "geometry", [mesh]).Should().Be(NativeStatus.Ok);

            //Act
            NativeStatus referenced = CyclesAbi.DeleteNode(host, mesh);
            NativeStatus holder = CyclesAbi.DeleteNode(host, instance);
            NativeStatus released = CyclesAbi.DeleteNode(host, mesh);
            NativeStatus stale = CyclesAbi.DeleteNode(host, mesh);

            //Assert
            referenced.Should().Be(NativeStatus.Invalid);
            holder.Should().Be(NativeStatus.Ok);
            released.Should().Be(NativeStatus.Ok);
            stale.Should().NotBe(NativeStatus.Ok);
        }
        finally
        {
            CyclesAbi.Destroy(host);
        }
    }

    private static int callbackEvents;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int CancelOnFirstProgress(nint userData, Native.Event* nativeEvent)
    {
        Interlocked.Increment(ref callbackEvents);
        return nativeEvent->Kind == 2 ? 1 : 0;
    }

    [Fact]
    public void a_nonzero_event_callback_return_cancels_the_render_and_passes_need_a_successful_render()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        CyclesAbi.Create(Backend.CpuReference, 1, out ulong host).Should().Be(NativeStatus.Ok);
        try
        {
            TestScenes.BuildRaw(host);
            float[] rgba = new float[32 * 24 * 4];
            Volatile.Write(ref callbackEvents, 0);

            //Act
            NativeStatus register = CyclesAbi.SetEventCallback(host, &CancelOnFirstProgress, 0);
            NativeStatus cancelled = CyclesAbi.Render(host, 32, 24, 1_000_000, rgba);
            NativeStatus notReady = CyclesAbi.PassFloats(host, "Combined", Span<float>.Empty, out _, out _);
            NativeStatus clear = CyclesAbi.SetEventCallback(host, null, 0);
            NativeStatus rendered = CyclesAbi.Render(host, 32, 24, 4, rgba);
            float[] combined = new float[rgba.Length];
            NativeStatus read = CyclesAbi.PassFloats(host, "Combined", combined, out ulong required, out uint components);

            //Assert
            register.Should().Be(NativeStatus.Ok);
            cancelled.Should().Be(NativeStatus.Cancelled);
            Volatile.Read(ref callbackEvents).Should().BeGreaterThan(0);
            notReady.Should().Be(NativeStatus.NotReady);
            clear.Should().Be(NativeStatus.Ok);
            rendered.Should().Be(NativeStatus.Ok);
            read.Should().Be(NativeStatus.Ok);
            required.Should().Be((ulong)rgba.Length);
            components.Should().Be(4u);
            combined.AsSpan().SequenceEqual(rgba).Should().BeTrue();
        }
        finally
        {
            CyclesAbi.Destroy(host);
        }
    }
}
