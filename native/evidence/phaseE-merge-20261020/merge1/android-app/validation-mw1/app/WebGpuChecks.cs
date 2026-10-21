// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
using System.Diagnostics;
using System.Security.Cryptography;
using CodeBrix.Graphics3D.Cycles;
using CodeBrix.Graphics3D.Cycles.Diagnostics;
using CodeBrix.Graphics3D.Cycles.Rendering;
using CodeBrix.Graphics3D.Cycles.Validation;

// The app's WebGPU mode (am start --es mode webgpu): renders the shared validation scenes
// (WebGpuValidationScenes.cs, also used by the Linux managed tests) on CyclesBackend.Gpu and on
// CyclesBackend.CpuReference inside the installed app, compares the two films with the repository
// criteria, checks progress events and cancellation on the GPU backend, and records the
// capability report (tier decision, compile budget, warnings). Films are written for an
// independent comparison by native/tools/android_app_gate.py.
internal static class WebGpuChecks
{
    public static async Task<Dictionary<string, object>> Run(string output)
    {
        Directory.CreateDirectory(output);
        List<Dictionary<string, object>> scenes = new();
        Dictionary<string, object> capability = new();
        bool passed = true;
        foreach (string name in WebGpuValidationScenes.Names)
        {
            Stopwatch clock = Stopwatch.StartNew();
            float[] cpu;
            using (CyclesScene reference = new(CyclesBackend.CpuReference))
            {
                WebGpuValidationScenes.Build(reference, name);
                cpu = reference.Render(WebGpuValidationScenes.Settings).ToArray();
            }
            double cpuSeconds = clock.Elapsed.TotalSeconds;
            using CyclesScene gpu = new(CyclesBackend.Gpu);
            if (gpu.Backend != Backend.WebGpu) throw new Exception("CyclesBackend.Gpu resolved to " + gpu.Backend);
            WebGpuValidationScenes.Build(gpu, name);
            int progressEvents = 0, errorEvents = 0, progressReports = 0;
            gpu.RenderEventReceived += (sender, e) =>
            {
                if (e.Kind == RenderEventKind.Progress) Interlocked.Increment(ref progressEvents);
                if (e.Kind == RenderEventKind.Error) Interlocked.Increment(ref errorEvents);
            };
            clock.Restart();
            FilmBuffer film = await gpu.RenderAsync(WebGpuValidationScenes.Settings, new Counter(() => Interlocked.Increment(ref progressReports)), CancellationToken.None);
            double gpuSeconds = clock.Elapsed.TotalSeconds;
            if (gpu.IsFaulted) throw new Exception(name + ": the WebGPU device faulted");
            float[] pixels = film.ToArray();
            (double maximum, int practical, int strict) = WebGpuValidationScenes.Compare(pixels, cpu);
            byte[] gpuBytes = System.Runtime.InteropServices.MemoryMarshal.AsBytes(pixels.AsSpan()).ToArray();
            byte[] cpuBytes = System.Runtime.InteropServices.MemoryMarshal.AsBytes(cpu.AsSpan()).ToArray();
            File.WriteAllBytes(Path.Combine(output, name + ".gpu.rgba.bin"), gpuBytes);
            File.WriteAllBytes(Path.Combine(output, name + ".cpu.rgba.bin"), cpuBytes);
            ICapabilityReport report = gpu.GetCapabilities();
            passed &= practical == 0 && progressEvents > 0 && errorEvents == 0;
            scenes.Add(new Dictionary<string, object>
            {
                ["name"] = name, ["width"] = WebGpuValidationScenes.Settings.Width, ["height"] = WebGpuValidationScenes.Settings.Height,
                ["samples"] = WebGpuValidationScenes.Settings.Samples,
                ["cpuSha256"] = Convert.ToHexStringLower(SHA256.HashData(cpuBytes)), ["gpuSha256"] = Convert.ToHexStringLower(SHA256.HashData(gpuBytes)),
                ["maximumAbsoluteError"] = maximum, ["practicalFailures"] = practical, ["strictDifferences"] = strict,
                ["cpuSeconds"] = cpuSeconds, ["gpuSeconds"] = gpuSeconds, ["progressEvents"] = progressEvents,
                ["progressReports"] = progressReports, ["errorEvents"] = errorEvents,
                ["pipelineTier"] = report.PipelineTier ?? "", ["warnings"] = report.Warnings.ToArray()
            });
            if (capability.Count == 0)
            {
                capability["backendName"] = report.BackendName ?? "";
                capability["pipelineTier"] = report.PipelineTier ?? "";
                capability["deviceDescription"] = report.DeviceDescription ?? "";
                capability["deviceId"] = report.DeviceId ?? "";
                capability["warnings"] = report.Warnings.ToArray();
                capability["writtenPasses"] = report.Scope.WrittenPasses.ToArray();
                capability["renderTimeRejections"] = report.Scope.RenderTimeRejections.ToArray();
                capability["json"] = report.Json;
            }
        }

        // Cancellation on the GPU backend: cancel from the first progress event, then render again.
        Dictionary<string, object> cancellation = new();
        using (CyclesScene gpu = new(CyclesBackend.Gpu))
        {
            WebGpuValidationScenes.Build(gpu, WebGpuValidationScenes.DiffuseSun);
            using CancellationTokenSource source = new();
            int seen = 0;
            void CancelOnProgress(object? sender, RenderEvent e)
            {
                if (e.Kind == RenderEventKind.Progress && Interlocked.Increment(ref seen) == 1) source.Cancel();
            }
            gpu.RenderEventReceived += CancelOnProgress;
            bool cancelled = false;
            Stopwatch clock = Stopwatch.StartNew();
            try { await gpu.RenderAsync(WebGpuValidationScenes.Settings with { Samples = 1_000_000 }, null, source.Token); }
            catch (OperationCanceledException) { cancelled = true; }
            double cancelSeconds = clock.Elapsed.TotalSeconds;
            gpu.RenderEventReceived -= CancelOnProgress;
            bool faulted = gpu.IsFaulted;
            float[] cpu;
            using (CyclesScene reference = new(CyclesBackend.CpuReference))
            {
                WebGpuValidationScenes.Build(reference, WebGpuValidationScenes.DiffuseSun);
                cpu = reference.Render(WebGpuValidationScenes.Settings).ToArray();
            }
            float[] again = (await gpu.RenderAsync(WebGpuValidationScenes.Settings, null, CancellationToken.None)).ToArray();
            (double maximum, int practical, int strict) = WebGpuValidationScenes.Compare(again, cpu);
            passed &= cancelled && !faulted && practical == 0;
            cancellation["cancelled"] = cancelled;
            cancellation["secondsUntilCancelled"] = cancelSeconds;
            cancellation["faultedAfterCancel"] = faulted;
            cancellation["reRenderMaximumAbsoluteError"] = maximum;
            cancellation["reRenderPracticalFailures"] = practical;
            cancellation["reRenderStrictDifferences"] = strict;
        }
        return new Dictionary<string, object>
        {
            ["passed"] = passed, ["mode"] = "webgpu", ["scenes"] = scenes, ["cancellation"] = cancellation, ["capability"] = capability,
            ["practicalCriterion"] = "5e-5 + 2e-4*abs(cpu)", ["strictCriterion"] = "1e-7 + 1e-7*abs(cpu)"
        };
    }

    private sealed class Counter(Action increment) : IProgress<RenderProgress>
    {
        public void Report(RenderProgress value) => increment();
    }
}
