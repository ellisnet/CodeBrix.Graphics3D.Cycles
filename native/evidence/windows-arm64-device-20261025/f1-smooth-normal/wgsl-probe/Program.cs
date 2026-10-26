// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text.Json;
using CodeBrix.WebGpu.Native;
using GpuBuffer = CodeBrix.WebGpu.Native.Buffer;
using Adapter = CodeBrix.WebGpu.Native.Adapter;

public unsafe class Program
{
    static string mode = "";
    static Instance instance;
    static Adapter adapter;
    static Device device;
    static Queue queue;
    static volatile bool done;
    static uint status;
    static string callbackMessage = "";
    static string uncaptured = "";
    static ErrorType scopedError;

    public record Dispatch(string Entry, uint Groups);
    public record Case(string Name, string Shader, string[] Buffers, int Output, Dispatch[] Dispatches);

    public static int Main(string[] args)
    {
        try
        {
            var root = Path.GetFullPath(args[0]);
            mode = args[1]; // adreno | warp | dozen
            CodeBrixWebGpuNative.EnsureLoaded();
            InstanceExtras extras = new() { Backends = mode == "dozen" ? InstanceBackend.Vulkan : InstanceBackend.DX12 };
            if (mode == "dozen") extras.Flags = InstanceFlag.AllowUnderlyingNoncompliantAdapter;
            using var dxcPath = new WgpuUtf8String(args[2]);
            if (mode != "dozen") { extras.Dx12ShaderCompiler = Dx12Compiler.Dxc; extras.DxcPath = dxcPath.View; extras.DxcMaxShaderModel = DxcMaxShaderModel.V6_7; }
            extras.Chain.SType = InstanceExtras.StructType;
            InstanceDescriptor desc = new() { NextInChain = &extras.Chain };
            instance = WebGpu.CreateInstance(&desc);
            if (instance.IsNull) throw new Exception("No WebGPU instance");
            nuint count = WebGpu.InstanceEnumerateAdapters(instance, null, null);
            var adapters = new Adapter[(int)count];
            fixed (Adapter* ptr = adapters) WebGpu.InstanceEnumerateAdapters(instance, null, ptr);
            object? selected = null;
            foreach (var candidate in adapters)
            {
                AdapterInfo info = default;
                if (WebGpu.AdapterGetInfo(candidate, &info) != Status.Success) throw new Exception("Adapter info unavailable");
                string name = WgpuString.ToManaged(info.Device);
                bool wanted = mode == "warp" ? info.AdapterType == AdapterType.CPU : info.AdapterType is AdapterType.IntegratedGPU or AdapterType.DiscreteGPU;
                if (adapter.IsNull && wanted)
                {
                    adapter = candidate;
                    selected = new { name, vendor = WgpuString.ToManaged(info.Vendor),
                        description = WgpuString.ToManaged(info.Description), type = info.AdapterType.ToString(),
                        backend = info.BackendType.ToString(), info.VendorID, info.DeviceID };
                }
                else WebGpu.AdapterRelease(candidate);
                WebGpu.AdapterInfoFreeMembers(info);
            }
            if (adapter.IsNull) throw new Exception("No adapter for mode " + mode);
            Limits available = default;
            WebGpu.AdapterGetLimits(adapter, &available);
            DeviceDescriptor dd = default; // Baseline device; no optional features requested.
            dd.UncapturedErrorCallbackInfo.Callback = &OnError;
            dd.DeviceLostCallbackInfo.Mode = CallbackMode.AllowProcessEvents;
            dd.DeviceLostCallbackInfo.Callback = &OnLost;
            done = false;
            WebGpu.AdapterRequestDevice(adapter, &dd, new RequestDeviceCallbackInfo {
                Mode = CallbackMode.AllowProcessEvents, Callback = &OnDevice });
            Wait(false);
            if (status != (uint)RequestDeviceStatus.Success || device.IsNull) throw new Exception(callbackMessage);
            queue = WebGpu.DeviceGetQueue(device);
            Limits enabled = default;
            WebGpu.DeviceGetLimits(device, &enabled);
            var results = new List<object>();
            foreach (Case test in JsonSerializer.Deserialize<Case[]>(File.ReadAllText(Path.Combine(root, "cases.json")))!)
                results.Add(Run(root, test));
            Console.WriteLine(JsonSerializer.Serialize(new { adapter = selected,
                package = "1.0.290.1433", native = CodeBrixWebGpuNative.BuildInfo,
                availableLimits = LimitsInfo(available), enabledLimits = LimitsInfo(enabled),
                requestedOptionalFeatures = 0, results }, new JsonSerializerOptions { WriteIndented = true }));
            return 0;
        }
        catch (Exception ex) { Console.Error.WriteLine(ex); return 1; }
        finally
        {
            if (!queue.IsNull) WebGpu.QueueRelease(queue);
            if (!device.IsNull) WebGpu.DeviceRelease(device);
            if (!adapter.IsNull) WebGpu.AdapterRelease(adapter);
            if (!instance.IsNull) WebGpu.InstanceRelease(instance);
        }
    }

    static object LimitsInfo(Limits l) => new { l.MaxStorageBuffersPerShaderStage, l.MaxStorageBufferBindingSize,
        l.MaxBufferSize, l.MaxComputeWorkgroupStorageSize, l.MaxComputeInvocationsPerWorkgroup,
        l.MaxSampledTexturesPerShaderStage };

    static object Run(string root, Case test)
    {
        Console.Error.WriteLine("GPU probe: " + test.Name);
        var timer = Stopwatch.StartNew();
        WebGpu.DevicePushErrorScope(device, ErrorFilter.Validation);
        using var code = new WgpuUtf8String(File.ReadAllText(Path.Combine(root, test.Shader)));
        ShaderSourceWGSL source = new() { Code = code.View };
        source.Chain.SType = ShaderSourceWGSL.StructType;
        ShaderModuleDescriptor sm = new() { NextInChain = &source.Chain };
        ShaderModule shader = WebGpu.DeviceCreateShaderModule(device, &sm);
        CheckScope();
        var buffers = new GpuBuffer[test.Buffers.Length];
        var sizes = new ulong[buffers.Length];
        for (int i = 0; i < buffers.Length; i++)
        {
            byte[] data = File.ReadAllBytes(Path.Combine(root, test.Buffers[i]));
            sizes[i] = (ulong)data.Length;
            BufferDescriptor bd = new() { Size = sizes[i], Usage = BufferUsage.Storage | BufferUsage.CopySrc | BufferUsage.CopyDst };
            buffers[i] = WebGpu.DeviceCreateBuffer(device, &bd);
            fixed (byte* p = data) WebGpu.QueueWriteBuffer(queue, buffers[i], 0, p, (nuint)data.Length);
        }
        var pipelines = new List<ComputePipeline>();
        var groups = new List<BindGroup>();
        foreach (var dispatch in test.Dispatches)
        {
            WebGpu.DevicePushErrorScope(device, ErrorFilter.Validation);
            using var entry = new WgpuUtf8String(dispatch.Entry);
            ComputePipelineDescriptor pd = default;
            pd.Compute.Module = shader; pd.Compute.EntryPoint = entry.View;
            var pipeline = WebGpu.DeviceCreateComputePipeline(device, &pd);
            CheckScope();
            var layout = WebGpu.ComputePipelineGetBindGroupLayout(pipeline, 0);
            var entries = new BindGroupEntry[buffers.Length];
            for (int i = 0; i < entries.Length; i++) entries[i] = new() { Binding = (uint)i, Buffer = buffers[i], Size = sizes[i] };
            fixed (BindGroupEntry* ptr = entries)
            {
                WebGpu.DevicePushErrorScope(device, ErrorFilter.Validation);
                BindGroupDescriptor bg = new() { Layout = layout, EntryCount = (nuint)entries.Length, Entries = ptr };
                groups.Add(WebGpu.DeviceCreateBindGroup(device, &bg));
                CheckScope();
            }
            WebGpu.BindGroupLayoutRelease(layout);
            pipelines.Add(pipeline);
        }
        double preparationMs = timer.Elapsed.TotalMilliseconds;
        timer.Restart();
        BufferDescriptor readDesc = new() { Size = sizes[test.Output], Usage = BufferUsage.MapRead | BufferUsage.CopyDst };
        var readback = WebGpu.DeviceCreateBuffer(device, &readDesc);
        var encoder = WebGpu.DeviceCreateCommandEncoder(device, null);
        for (int i = 0; i < test.Dispatches.Length; i++)
        {
            var pass = WebGpu.CommandEncoderBeginComputePass(encoder, null);
            WebGpu.ComputePassEncoderSetPipeline(pass, pipelines[i]);
            WebGpu.ComputePassEncoderSetBindGroup(pass, 0, groups[i], 0, null);
            WebGpu.ComputePassEncoderDispatchWorkgroups(pass, test.Dispatches[i].Groups, 1, 1);
            WebGpu.ComputePassEncoderEnd(pass);
            WebGpu.ComputePassEncoderRelease(pass);
        }
        WebGpu.CommandEncoderCopyBufferToBuffer(encoder, buffers[test.Output], 0, readback, 0, sizes[test.Output]);
        WebGpu.DevicePushErrorScope(device, ErrorFilter.Validation);
        var command = WebGpu.CommandEncoderFinish(encoder, null);
        CheckScope(); // Never submit a command buffer that failed validation.
        WebGpu.QueueSubmit(queue, 1, &command);
        done = false;
        WebGpu.BufferMapAsync(readback, MapMode.Read, 0, (nuint)sizes[test.Output], new BufferMapCallbackInfo {
            Mode = CallbackMode.AllowProcessEvents, Callback = &OnMap });
        Wait(true);
        if (status != (uint)MapAsyncStatus.Success) throw new Exception(callbackMessage);
        byte[] output = new byte[checked((int)sizes[test.Output])];
        Marshal.Copy((nint)WebGpu.BufferGetConstMappedRange(readback, 0, (nuint)output.Length), output, 0, output.Length);
        WebGpu.BufferUnmap(readback);
        double executionAndReadbackMs = timer.Elapsed.TotalMilliseconds;
        if (uncaptured.Length != 0) throw new Exception(uncaptured);
        File.WriteAllBytes(Path.Combine(root, test.Name + "." + mode + ".bin"), output);
        WebGpu.CommandBufferRelease(command); WebGpu.CommandEncoderRelease(encoder);
        WebGpu.BufferRelease(readback);
        foreach (var group in groups) WebGpu.BindGroupRelease(group);
        foreach (var pipeline in pipelines) WebGpu.ComputePipelineRelease(pipeline);
        foreach (var buffer in buffers) WebGpu.BufferRelease(buffer);
        WebGpu.ShaderModuleRelease(shader);
        return new { test.Name, preparationMs, executionAndReadbackMs, outputBytes = output.Length };
    }

    static void Wait(bool gpu)
    {
        var timer = Stopwatch.StartNew();
        while (!done && timer.Elapsed < TimeSpan.FromSeconds(60))
        {
            if (gpu) WebGpu.PollDevice(device, true, TimeSpan.FromSeconds(1));
            WebGpu.InstanceProcessEvents(instance);
            if (uncaptured.Length != 0) throw new Exception(uncaptured);
            if (!done) Thread.Sleep(1);
        }
        if (!done) throw new TimeoutException("WebGPU callback did not complete");
    }
    static void CheckScope()
    {
        done = false;
        WebGpu.DevicePopErrorScope(device, new PopErrorScopeCallbackInfo { Mode = CallbackMode.AllowProcessEvents, Callback = &OnScope });
        Wait(false);
        if (status != (uint)PopErrorScopeStatus.Success || scopedError != ErrorType.NoError) throw new Exception(callbackMessage);
    }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    static void OnDevice(RequestDeviceStatus s, Device d, StringView m, void* a, void* b)
    { device = d; status = (uint)s; callbackMessage = WgpuString.ToManaged(m); done = true; }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    static void OnMap(MapAsyncStatus s, StringView m, void* a, void* b)
    { status = (uint)s; callbackMessage = WgpuString.ToManaged(m); done = true; }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    static void OnScope(PopErrorScopeStatus s, ErrorType e, StringView m, void* a, void* b)
    { status = (uint)s; scopedError = e; callbackMessage = WgpuString.ToManaged(m); done = true; }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    static void OnError(Device* d, ErrorType e, StringView m, void* a, void* b)
    { uncaptured = e + ": " + WgpuString.ToManaged(m); }
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    static void OnLost(Device* d, DeviceLostReason r, StringView m, void* a, void* b)
    { if (r != DeviceLostReason.Destroyed) uncaptured = "Device lost: " + WgpuString.ToManaged(m); }
}
