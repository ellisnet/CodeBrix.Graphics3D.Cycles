// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.IO;
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
        report.Events.Should().Equal("status", "progress", "error", "film");
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
        report.BackendScopes["cpu-embree"].PassesNote.Should().NotBeNullOrEmpty();
        if (CyclesScene.ResolveBackend(CyclesBackend.Gpu) == Backend.Metal)
        {
            // Apple Silicon: Cycles' own Metal device, the full upstream feature set.
            report.CompiledBackends.Should().Equal("cpu", "metal", "cpu-embree");
            report.GpuBackendName.Should().Be("metal");
            report.BackendScopes["metal"].CameraTypes.Should().Equal("perspective", "orthograph", "panorama");
            report.BackendScopes["metal"].PassesNote.Should().NotBeNullOrEmpty();
            report.BackendScopes["metal"].RenderTimeRejections.Should().Equal(RejectionsOf("metal"));
            return;
        }
        report.CompiledBackends.Should().Equal("cpu", "webgpu", "cpu-embree");
        report.GpuBackendName.Should().Be("webgpu");
        report.BackendScopes["webgpu"].CameraTypes.Should().Equal("perspective", "orthograph", "panorama");
        report.BackendScopes["webgpu"].WrittenPasses.Should().Equal("combined", "emission", "background", "ao", "diffuse", "diffuse_direct",
            "diffuse_indirect", "diffuse_color", "glossy", "glossy_direct", "glossy_indirect", "glossy_color", "transmission", "transmission_direct",
            "transmission_indirect", "transmission_color", "depth", "position", "normal", "roughness", "uv", "object_id", "material_id", "motion",
            "motion_weight", "cryptomatte", "aov_color", "aov_value", "mist", "render_time", "sample_count", "shadow_catcher", "shadow_catcher_sample_count",
            "shadow_catcher_matte", "denoising_albedo", "denoising_normal", "denoising_specular_albedo", "denoising_roughness", "denoising_depth",
            "denoising_backward_motion", "denoising_specular_motion", "volume_majorant", "volume_majorant_sample_count", "volume_direct", "volume_indirect");
        report.BackendScopes["webgpu"].RenderTimeRejections.Should().NotBeEmpty();
    }

    // The largest image side per backend (provisional backendScope.<backend>.maxImageDimension): Cycles' Metal device refuses a
    // side above 16384 (MetalDevice::image_alloc), the WGSL device one above INT32_MAX / 2 (WebGPUDevice::mem_alloc), and the
    // host refuses one above INT32_MAX / 2 on the CPU backends, whose kernels compute twice the side in int for the Mirror
    // extension; limits.maxImageDimension stays the largest side the API accepts. The largest tile side of a tiled image per
    // backend (provisional backendScope.<backend>.maxImageTileSize): 128 on Metal, 1024 elsewhere (TiledImageTests). On Metal
    // the image limit is measured: 16384 x 1 renders, 16385 x 1 fails with Cycles' message (GpuRenderTests); on the CPU
    // 1073741824 x 1 is refused by name (TextureLimitTests).
    [Fact]
    public void the_report_states_the_largest_image_side_and_tile_size_of_every_backend()
    {
        //Arrange
        TestScenes.RequireNativeHost();

        //Act
        ICapabilityReport report = CyclesScene.GetBinaryCapabilities();
        using CyclesScene cpu = TestScenes.CreateCpuScene();
        ICapabilityReport device = cpu.GetCapabilities();

        //Assert
        report.Limits["maxImageDimension"].Should().Be(int.MaxValue);
        report.BackendScopes["cpu"].MaxImageDimension.Should().Be(1073741823L);
        report.BackendScopes["cpu-embree"].MaxImageDimension.Should().Be(1073741823L);
        device.Scope.MaxImageDimension.Should().Be(1073741823L);
        report.BackendScopes["cpu"].MaxImageTileSize.Should().Be(1024L);
        report.BackendScopes["cpu-embree"].MaxImageTileSize.Should().Be(1024L);
        device.Scope.MaxImageTileSize.Should().Be(TiledImage.MaxTileSize);
        if (report.CompiledBackends.Contains("metal"))
        {
            report.BackendScopes["metal"].MaxImageDimension.Should().Be(16384L);
            report.BackendScopes["metal"].MaxImageTileSize.Should().Be(128L);
        }
        if (report.CompiledBackends.Contains("webgpu"))
        {
            report.BackendScopes["webgpu"].MaxImageDimension.Should().Be(1073741823L);
            report.BackendScopes["webgpu"].MaxImageTileSize.Should().Be(1024L);
        }
        report.BackendScopes.Values.All(scope => scope.MaxImageDimension.HasValue && scope.MaxImageTileSize.HasValue).Should().BeTrue();
        report.BackendScopes.Values.Max(scope => scope.MaxImageTileSize).Should().Be(TiledImage.MaxTileSize);
        BackendScope bare = CapabilityReport.Parse("{\"schemaVersion\":1,\"backendScope\":{\"cpu\":{\"device\":\"x\"}}}").BackendScopes["cpu"];
        bare.MaxImageDimension.Should().BeNull();
        bare.MaxImageTileSize.Should().BeNull();
    }

    // AGENT-README.txt, RENDER-TIME REJECTIONS: the "  - " entries under each group heading, in order.
    private static readonly string[] RejectionGroups = ["Every backend:", "The WebGPU backend:"];

    private static Dictionary<string, List<string>> ReadmeRejections()
    {
        string[] lines = File.ReadAllLines(Path.Combine(RepositoryPaths.Root, "AGENT-README.txt"));
        int start = Array.IndexOf(lines, "RENDER-TIME REJECTIONS");
        Dictionary<string, List<string>> groups = [];
        List<string> current = null;
        for (int i = start + 2; i < lines.Length && !lines[i].StartsWith("Why.", StringComparison.Ordinal); i++)
        {
            if (RejectionGroups.Contains(lines[i])) groups[lines[i]] = current = [];
            else if (lines[i].StartsWith("  - ", StringComparison.Ordinal)) current.Add(lines[i][4..]);
        }
        return groups;
    }

    // The entries AGENT-README.txt lists for one backend scope of the capability report.
    private static IReadOnlyList<string> RejectionsOf(string backend)
    {
        Dictionary<string, List<string>> groups = ReadmeRejections();
        IEnumerable<string> entries = groups[RejectionGroups[0]];
        if (backend == "webgpu") entries = entries.Concat(groups[RejectionGroups[1]]);
        return entries.ToList();
    }

    [Fact]
    public void render_time_rejections_of_every_backend_match_the_agent_readme_list_word_for_word()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        Assert.SkipWhen(RepositoryPaths.Root == null, "The tests are not running from inside the repository working tree.");

        //Act
        ICapabilityReport report = CyclesScene.GetBinaryCapabilities();
        Dictionary<string, List<string>> groups = ReadmeRejections();

        //Assert
        groups.Keys.Should().BeEquivalentTo(RejectionGroups);
        groups.Values.All(group => group.Count > 0).Should().BeTrue();
        report.BackendScopes.Count.Should().Be(report.CompiledBackends.Count);
        foreach (string backend in report.CompiledBackends)
            report.BackendScopes[backend].RenderTimeRejections.Should().Equal(RejectionsOf(backend), backend);
    }

    // The quaternion and spherical-harmonics attribute kinds render on the CPU and Metal backends; only the WebGPU device
    // refuses a scene that reads one, so the entry is in the WebGPU group and in no other backend's scope.
    [Fact]
    public void quaternion_and_spherical_harmonics_attributes_are_a_rejection_of_the_webgpu_backend_only()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        Assert.SkipWhen(RepositoryPaths.Root == null, "The tests are not running from inside the repository working tree.");
        const string entry = "quaternion and spherical-harmonics attributes read by a shader";

        //Act
        ICapabilityReport report = CyclesScene.GetBinaryCapabilities();
        Dictionary<string, List<string>> groups = ReadmeRejections();

        //Assert
        groups[RejectionGroups[1]].Should().Contain(entry);
        groups[RejectionGroups[0]].Should().NotContain(entry);
        foreach (string backend in report.CompiledBackends)
            report.BackendScopes[backend].RenderTimeRejections.Contains(entry).Should().Be(backend == "webgpu", backend);
    }

    // Gaussian splats render on the CPU and Metal backends; the host refuses them on the WebGPU backend only, by that
    // device's words, so the entry is in the WebGPU group and in no other backend's scope.
    [Fact]
    public void point_clouds_rendered_as_gaussian_splats_are_a_rejection_of_the_webgpu_backend_only()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        Assert.SkipWhen(RepositoryPaths.Root == null, "The tests are not running from inside the repository working tree.");
        const string entry = "point clouds rendered as Gaussian splats";

        //Act
        ICapabilityReport report = CyclesScene.GetBinaryCapabilities();
        Dictionary<string, List<string>> groups = ReadmeRejections();

        //Assert
        groups[RejectionGroups[1]].Should().Contain(entry);
        groups[RejectionGroups[0]].Should().NotContain(entry);
        foreach (string backend in report.CompiledBackends)
            report.BackendScopes[backend].RenderTimeRejections.Contains(entry).Should().Be(backend == "webgpu", backend);
    }

    // Area lights with an axis of infinities (provisional): the host refuses them on every backend, so the entry is in the
    // every-backend group and in every backend's scope, and the WebGPU device's own entry for degenerate area lights no
    // longer names that case.
    [Fact]
    public void area_lights_with_an_axis_of_infinities_are_a_rejection_of_every_backend()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        Assert.SkipWhen(RepositoryPaths.Root == null, "The tests are not running from inside the repository working tree.");
        const string entry = "area lights with an axis of infinities (an axis with no zero component whose length underflows to zero)";

        //Act
        ICapabilityReport report = CyclesScene.GetBinaryCapabilities();
        Dictionary<string, List<string>> groups = ReadmeRejections();

        //Assert
        groups[RejectionGroups[0]].Should().Contain(entry);
        groups[RejectionGroups[1]].Should().NotContain(entry);
        groups[RejectionGroups[1]].Any(other => other.Contains("infinities")).Should().BeFalse();
        foreach (string backend in report.CompiledBackends)
            report.BackendScopes[backend].RenderTimeRejections.Should().Contain(entry, backend);
    }

    // UDIM tiles (provisional cbc_host_image_bind_tiles) render on every backend: no group and no backend's scope has the
    // entry the WebGPU backend had.
    [Fact]
    public void udim_tiles_are_a_rejection_of_no_backend()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        Assert.SkipWhen(RepositoryPaths.Root == null, "The tests are not running from inside the repository working tree.");
        const string entry = "UDIM tiles (several images bound to one Image Texture node)";

        //Act
        ICapabilityReport report = CyclesScene.GetBinaryCapabilities();
        Dictionary<string, List<string>> groups = ReadmeRejections();

        //Assert
        groups[RejectionGroups[1]].Should().NotContain(entry);
        groups[RejectionGroups[0]].Should().NotContain(entry);
        foreach (string backend in report.CompiledBackends)
            report.BackendScopes[backend].RenderTimeRejections.Should().NotContain(entry, backend);
    }

    // Tiled rendering (provisional CyclesScene.TileSize) renders on every backend: no group and no backend's scope has the
    // entry the WebGPU backend had.
    [Fact]
    public void tiled_rendering_is_a_rejection_of_no_backend()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        Assert.SkipWhen(RepositoryPaths.Root == null, "The tests are not running from inside the repository working tree.");
        const string entry = "tiled rendering (a tile size other than 0)";

        //Act
        ICapabilityReport report = CyclesScene.GetBinaryCapabilities();
        Dictionary<string, List<string>> groups = ReadmeRejections();

        //Assert
        groups[RejectionGroups[1]].Should().NotContain(entry);
        groups[RejectionGroups[0]].Should().NotContain(entry);
        foreach (string backend in report.CompiledBackends)
            report.BackendScopes[backend].RenderTimeRejections.Should().NotContain(entry, backend);
    }

    // BVH spatial splits (provisional CyclesScene.UseBvhSpatialSplit) apply on every backend: no group and no backend's
    // scope has the entry the WebGPU backend had.
    [Fact]
    public void bvh_spatial_splits_are_a_rejection_of_no_backend()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        Assert.SkipWhen(RepositoryPaths.Root == null, "The tests are not running from inside the repository working tree.");
        const string entry = "BVH spatial splits";

        //Act
        ICapabilityReport report = CyclesScene.GetBinaryCapabilities();
        Dictionary<string, List<string>> groups = ReadmeRejections();

        //Assert
        groups[RejectionGroups[1]].Should().NotContain(entry);
        groups[RejectionGroups[0]].Should().NotContain(entry);
        foreach (string backend in report.CompiledBackends)
            report.BackendScopes[backend].RenderTimeRejections.Should().NotContain(entry, backend);
    }

    // The texture limit (provisional CyclesScene.TextureLimit) reduces images on the host for every device: no group and no
    // backend's scope has the entry the WebGPU backend had.
    [Fact]
    public void a_texture_limit_is_a_rejection_of_no_backend()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        Assert.SkipWhen(RepositoryPaths.Root == null, "The tests are not running from inside the repository working tree.");
        const string entry = "a texture limit (images reduced when they load)";

        //Act
        ICapabilityReport report = CyclesScene.GetBinaryCapabilities();
        Dictionary<string, List<string>> groups = ReadmeRejections();

        //Assert
        groups[RejectionGroups[1]].Should().NotContain(entry);
        groups[RejectionGroups[0]].Should().NotContain(entry);
        foreach (string backend in report.CompiledBackends)
            report.BackendScopes[backend].RenderTimeRejections.Should().NotContain(entry, backend);
    }

    // Progressive results (provisional HostFlags.ProgressiveResults) are delivered on every backend: no group and no
    // backend's scope has the entry the WebGPU backend had.
    [Fact]
    public void progressive_results_are_a_rejection_of_no_backend()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        Assert.SkipWhen(RepositoryPaths.Root == null, "The tests are not running from inside the repository working tree.");
        const string entry = "progressive results (intermediate films during a render)";

        //Act
        ICapabilityReport report = CyclesScene.GetBinaryCapabilities();
        Dictionary<string, List<string>> groups = ReadmeRejections();

        //Assert
        groups[RejectionGroups[1]].Should().NotContain(entry);
        groups[RejectionGroups[0]].Should().NotContain(entry);
        foreach (string backend in report.CompiledBackends)
            report.BackendScopes[backend].RenderTimeRejections.Should().NotContain(entry, backend);
        report.Events.Should().Contain("film");
    }

    // Tiled images (provisional CyclesScene.CreateTiledImage) render through Cycles' texture cache on the CPU and Metal
    // backends; the host refuses one on the WebGPU backend only ("WebGPU tiled images not implemented"), whose existing
    // entry "sparse or tiled images" names it, in the WebGPU group and in no other backend's scope.
    [Fact]
    public void sparse_or_tiled_images_are_a_rejection_of_the_webgpu_backend_only()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        Assert.SkipWhen(RepositoryPaths.Root == null, "The tests are not running from inside the repository working tree.");
        const string entry = "sparse or tiled images";

        //Act
        ICapabilityReport report = CyclesScene.GetBinaryCapabilities();
        Dictionary<string, List<string>> groups = ReadmeRejections();

        //Assert
        groups[RejectionGroups[1]].Should().Contain(entry);
        groups[RejectionGroups[0]].Should().NotContain(entry);
        foreach (string backend in report.CompiledBackends)
            report.BackendScopes[backend].RenderTimeRejections.Contains(entry).Should().Be(backend == "webgpu", backend);
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
    public void Parse_reads_the_pipeline_cache_state()
    {
        //Arrange
        const string active = "{\"abiVersion\":1,\"backend\":\"webgpu\",\"pipelineCache\":{\"active\":true,\"reason\":\"active\",\"directory\":\"/cache\"}}";
        const string off = "{\"abiVersion\":1,\"backend\":\"cpu\",\"pipelineCache\":{\"active\":false,\"reason\":\"backend\",\"directory\":null}}";

        //Act
        CapabilityReport on = CapabilityReport.Parse(active);
        CapabilityReport none = CapabilityReport.Parse(off);

        //Assert
        on.PipelineCacheActive.Should().BeTrue();
        on.PipelineCacheDirectory.Should().Be("/cache");
        on.PipelineCacheReason.Should().Be("active");
        none.PipelineCacheActive.Should().BeFalse();
        none.PipelineCacheDirectory.Should().BeNull();
        none.PipelineCacheReason.Should().Be("backend");
    }

    [Fact]
    public void a_cpu_device_reports_no_pipeline_cache_and_the_binary_report_says_nothing_of_one()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();

        //Act
        ICapabilityReport device = scene.GetCapabilities();
        ICapabilityReport binary = CyclesScene.GetBinaryCapabilities();

        //Assert
        device.PipelineCacheActive.Should().BeFalse();
        device.PipelineCacheDirectory.Should().BeNull();
        device.PipelineCacheReason.Should().Be("backend");
        binary.PipelineCacheActive.Should().BeFalse();
        binary.PipelineCacheReason.Should().BeNull();
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
