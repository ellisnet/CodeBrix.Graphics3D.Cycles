// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System.Linq;
using CodeBrix.Graphics3D.Cycles.Diagnostics;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// The native capability report (cbc_host_capabilities_json) parsed into ICapabilityReport.
public class CapabilityReportTests
{
    [Fact]
    public void scene_report_parses_the_native_capabilities_with_the_device()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();

        //Act
        ICapabilityReport report = scene.GetCapabilities();

        //Assert
        report.SchemaVersion.Should().Be(1);
        report.AbiVersion.Should().Be(CyclesVersion.ExpectedAbi);
        report.LibraryVersion.Should().Be(CyclesVersion.NativeLibraryVersion);
        report.CyclesRevision.Should().Be(CyclesVersion.CyclesRevision);
        report.HasDevice.Should().BeTrue();
        report.BackendName.Should().Be("cpu");
        report.Backend.Should().Be(Backend.CpuReference);
        report.IsGpu.Should().BeFalse();
        report.DeviceDescription.Should().NotBeNullOrEmpty();
        report.CompiledBackends.Should().Contain("cpu");
        report.GpuFallsBackToCpu.Should().BeFalse();
        report.Scope.Should().NotBeNull();
        report.Scope.CameraTypes.Should().Contain("perspective");
        report.ImageFormats.Should().Equal("float", "float4", "half", "half4", "byte", "byte4", "ushort", "ushort4");
        report.PassTypes["combined"].Should().Be(1);
        report.Events.Should().Equal("status", "progress", "error");
        report.Limits["maxMotionSteps"].Should().Be(129L);
        report.DeterminismReference.Should().NotBeNullOrEmpty();
        report.Json.Should().Contain("\"host\"");
    }

    [Fact]
    public void cpu_scope_states_the_fixed_baseline_oracle_arithmetic()
    {
        //Arrange
        TestScenes.RequireNativeHost();

        //Act
        ICapabilityReport report = CyclesScene.GetBinaryCapabilities();

        //Assert
        // The in-tree hosts are CPU oracles at the fixed baseline (native/DEVICE.md, "Fixed-baseline
        // CPU oracle"): no FMA contraction or reciprocal math, at the architecture's base level
        // (SSE4.2 kernels on Linux x64, plain ARM64 on Apple Silicon).
        System.Runtime.InteropServices.Architecture architecture = System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture;
        Assert.SkipUnless(architecture is System.Runtime.InteropServices.Architecture.X64 or System.Runtime.InteropServices.Architecture.Arm64,
            "the fixed-baseline oracle definitions cover x64 and ARM64 hosts");
        string isa = architecture == System.Runtime.InteropServices.Architecture.X64 ? "x86-64 SSE4.2" : "ARM64";
        report.Json.Should().Contain("\"arithmetic\":{\"isa\":\"" + isa + "\",\"fpContract\":\"off\",\"reciprocalMath\":\"off\"}");
    }

    [Fact]
    public void binary_report_has_no_device_and_states_the_platform_gpu_scope()
    {
        //Arrange
        TestScenes.RequireNativeHost();

        //Act
        ICapabilityReport report = CyclesScene.GetBinaryCapabilities();

        //Assert
        report.HasDevice.Should().BeFalse();
        report.BackendName.Should().BeNull();
        report.Backend.Should().Be(Backend.Gpu);
        report.IsGpu.Should().BeFalse();
        report.Scope.Should().BeNull();
        report.BackendScopes["cpu"].PassesNote.Should().NotBeNullOrEmpty();
        if (CyclesScene.ResolveBackend(CyclesBackend.Gpu) == Backend.Metal)
        {
            // Apple Silicon: Cycles' own Metal device, the full upstream feature set.
            report.CompiledBackends.Should().Equal("cpu", "metal");
            report.GpuBackendName.Should().Be("metal");
            report.BackendScopes["metal"].CameraTypes.Should().Equal("perspective", "orthograph", "panorama");
            report.BackendScopes["metal"].PassesNote.Should().NotBeNullOrEmpty();
            report.BackendScopes["metal"].RenderTimeRejections.Should().BeEmpty();
            return;
        }
        report.CompiledBackends.Should().Equal("cpu", "webgpu");
        report.GpuBackendName.Should().Be("webgpu");
        report.BackendScopes["webgpu"].CameraTypes.Should().Equal("perspective");
        report.BackendScopes["webgpu"].WrittenPasses.Should().Equal("combined", "shadow_catcher", "shadow_catcher_sample_count", "shadow_catcher_matte");
        report.BackendScopes["webgpu"].RenderTimeRejections.Should().NotBeEmpty();
    }

    [Fact]
    public void Parse_accepts_a_bare_device_report()
    {
        //Arrange
        const string json = "{\"abiVersion\":1,\"backend\":\"webgpu\",\"pipelines\":{\"tier\":\"conservative\",\"warnings\":[\"slow\"]}}";

        //Act
        CapabilityReport report = CapabilityReport.Parse(json);

        //Assert
        report.SchemaVersion.Should().Be(0);
        report.Backend.Should().Be(Backend.WebGpu);
        report.IsGpu.Should().BeTrue();
        report.PipelineTier.Should().Be("conservative");
        report.Warnings.Single().Should().Be("slow");
        report.LibraryVersion.Should().BeNull();
        report.CompiledBackends.Should().BeEmpty();
    }

    [Fact]
    public void parse_carries_the_pipeline_size_guard_warning_in_warnings()
    {
        //Arrange: the device report shape of a WebGPU build whose entry crossed the size guard
        // (pipelines.h); the guard warns without marking the device degraded.
        const string warning = "pipeline 'prepare_surface' (surface module) inlines to 1250000 bytes of WGSL, at or above the 1200000-byte size guard: "
            + "about 62500 backend instructions estimated (56561 to 73964); Intel Mesa drivers without upstream fix b7e1ac830984 can miscompile "
            + "or crash above 65,535; the compiled variant and results are unchanged";
        const string json = "{\"abiVersion\":1,\"backend\":\"webgpu\",\"pipelines\":{\"tier\":\"full\",\"degraded\":false,\"warnings\":[\"" + warning
            + "\"],\"sizeGuard\":{\"thresholdBytes\":1200000,\"largestEntryBytes\":1250000,\"largestEntry\":\"prepare_surface\",\"estimatedInstructions\":62500,\"warnings\":1}}}";

        //Act
        CapabilityReport report = CapabilityReport.Parse(json);

        //Assert
        report.PipelineTier.Should().Be("full");
        report.Warnings.Single().Should().Be(warning);
    }
}
