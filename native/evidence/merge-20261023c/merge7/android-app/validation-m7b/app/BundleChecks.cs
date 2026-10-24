// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using CodeBrix.Graphics3D.Cycles;
using CodeBrix.Graphics3D.Cycles.Bundles;
using CodeBrix.Graphics3D.Cycles.Diagnostics;
using CodeBrix.Graphics3D.Cycles.Rendering;

// The app's bundle mode (am start --es mode bundle): loads a cbcOracleScene version 1 bundle that
// native/tools/android_app_gate.py --mode bundle copied into files/bundle, builds it with the
// library's OracleSceneBundle loader and renders it on CyclesBackend.Gpu and, as an in-app
// diagnostic, on CyclesBackend.CpuReference. Films are written as raw float32 RGBA for the
// runner, which judges the GPU film against the Linux (glibc) CPU film of the same bundle.
internal static class BundleChecks
{
    [DllImport("libc", EntryPoint = "setenv")]
    private static extern int SetEnvironment(string name, string value, int overwrite);

    public static async Task<Dictionary<string, object>> Run(string bundleDirectory, string output, string? referenceOracle)
    {
        Directory.CreateDirectory(output);
        // The reference oracle's arithmetic adapters (native/cycles/reference_policy.h) are read by the
        // native host when it creates the WebGPU device, so they must be in the C environment first.
        if (!string.IsNullOrEmpty(referenceOracle) && SetEnvironment("CODEBRIX_CYCLES_REFERENCE_ORACLE", referenceOracle, 1) != 0)
            throw new Exception("setenv failed");
        string scenePath = Path.Combine(bundleDirectory, "scene.json");
        OracleSceneBundle bundle = OracleSceneBundle.Load(scenePath);
        Dictionary<string, string> inputs = new() { ["scene.json"] = Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(scenePath))) };
        foreach (string name in bundle.FileNames) inputs[name] = Convert.ToHexStringLower(SHA256.HashData(bundle.ReadFile(name)));
        RenderSettings settings = bundle.Render;

        Stopwatch clock = Stopwatch.StartNew();
        float[] cpu;
        using (CyclesScene reference = new(CyclesBackend.CpuReference))
        {
            bundle.Apply(reference);
            cpu = reference.Render(settings).ToArray();
        }
        double cpuSeconds = clock.Elapsed.TotalSeconds;

        using CyclesScene gpu = new(CyclesBackend.Gpu);
        if (gpu.Backend != Backend.WebGpu) throw new Exception("CyclesBackend.Gpu resolved to " + gpu.Backend);
        clock.Restart();
        bundle.Apply(gpu);
        double applySeconds = clock.Elapsed.TotalSeconds;
        int progressEvents = 0, errorEvents = 0;
        List<string> errors = new();
        gpu.RenderEventReceived += (sender, e) =>
        {
            if (e.Kind == RenderEventKind.Progress) Interlocked.Increment(ref progressEvents);
            if (e.Kind == RenderEventKind.Error) { Interlocked.Increment(ref errorEvents); lock (errors) errors.Add(e.Message); }
        };
        List<double> gpuSeconds = new();
        List<string> gpuHashes = new();
        float[] first = Array.Empty<float>();
        for (uint repeat = 0; repeat < Math.Max(1u, bundle.Repeats); ++repeat)
        {
            clock.Restart();
            FilmBuffer film = await gpu.RenderAsync(settings, null, CancellationToken.None);
            gpuSeconds.Add(clock.Elapsed.TotalSeconds);
            if (gpu.IsFaulted) throw new Exception("the WebGPU device faulted");
            float[] pixels = film.ToArray();
            byte[] bytes = MemoryMarshal.AsBytes(pixels.AsSpan()).ToArray();
            File.WriteAllBytes(Path.Combine(output, $"gpu-{repeat}.f32"), bytes);
            gpuHashes.Add(Convert.ToHexStringLower(SHA256.HashData(bytes)));
            if (repeat == 0) first = pixels;
        }
        byte[] cpuBytes = MemoryMarshal.AsBytes(cpu.AsSpan()).ToArray();
        File.WriteAllBytes(Path.Combine(output, "cpu-0.f32"), cpuBytes);
        (double maximum, int practical, int strict) = Compare(first, cpu);
        ICapabilityReport report = gpu.GetCapabilities();
        return new Dictionary<string, object>
        {
            ["passed"] = errorEvents == 0, ["mode"] = "bundle", ["sceneId"] = bundle.Id ?? "", ["inputs"] = inputs,
            ["width"] = settings.Width, ["height"] = settings.Height, ["samples"] = settings.Samples, ["repeats"] = gpuHashes.Count,
            ["referenceOracle"] = referenceOracle ?? "", ["cpuSha256"] = Convert.ToHexStringLower(SHA256.HashData(cpuBytes)), ["gpuSha256"] = gpuHashes,
            ["cpuSeconds"] = cpuSeconds, ["applySeconds"] = applySeconds, ["gpuSeconds"] = gpuSeconds, ["progressEvents"] = progressEvents,
            ["errorEvents"] = errors, ["diagnosticGpuVersusInAppCpu"] = new Dictionary<string, object>
            {
                ["maximumAbsoluteError"] = maximum, ["practicalFailures"] = practical, ["strictDifferences"] = strict, ["role"] = "diagnostic",
            },
            ["capability"] = new Dictionary<string, object>
            {
                ["deviceDescription"] = report.DeviceDescription ?? "", ["pipelineTier"] = report.PipelineTier ?? "",
                ["warnings"] = report.Warnings.ToArray(), ["json"] = report.Json,
            },
        };
    }

    private static (double Maximum, int Practical, int Strict) Compare(float[] actual, float[] reference)
    {
        double maximum = 0; int practical = 0, strict = 0;
        for (int i = 0; i < actual.Length; ++i)
        {
            double difference = Math.Abs((double)actual[i] - reference[i]);
            maximum = Math.Max(maximum, difference);
            if (difference > 5e-5 + 2e-4 * Math.Abs(reference[i])) ++practical;
            if (difference > 1e-7 + 1e-7 * Math.Abs(reference[i])) ++strict;
        }
        return (maximum, practical, strict);
    }
}
