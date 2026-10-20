// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
using System.Diagnostics;
using System.Runtime.InteropServices;
using CodeBrix.Graphics3D.Cycles;
using System.Text;
using System.Text.Json;

static void Require(bool value, string message) { if (!value) throw new Exception(message); }
static void Rejected(Action call, int expected)
{
    try { call(); } catch (NativeFailure error) { Require(error.Status == expected, error.ToString()); return; }
    throw new Exception("Expected native rejection " + expected);
}
static Node Fixture(CyclesHost host)
{
    Node integrator = host.SceneNode("integrator");
    foreach (string setting in new[] { "use_guiding", "use_light_tree", "use_adaptive_sampling", "use_denoise", "caustics_reflective", "caustics_refractive" }) host.SetInts(integrator, setting, [0]);
    host.SetInts(integrator, "min_bounce", [2]); host.SetInts(integrator, "max_bounce", [4]); host.SetInts(integrator, "max_diffuse_bounce", [4]);
    host.SetString(integrator, "sampling_pattern", "tabulated_sobol");
    Node camera = host.SceneNode("camera");
    host.SetFloats(camera, "fov", [1.1f]);
    host.SetFloats(camera, "matrix", [1,0,0,0, 0,1,0,0, 0,0,1,-4]);
    Node material = host.CreateNode("shader"), diffuse = host.ShaderNode(material, "diffuse_bsdf"), color = host.ShaderNode(material, "color");
    host.SetFloats(color, "value", [.7f,.2f,.1f]); host.SetFloats(diffuse, "roughness", [.3f]);
    host.Connect(color, "Color", diffuse, "Color"); host.Connect(diffuse, "BSDF", host.ShaderOutput(material), "Surface");
    host.SetString(material, "emission_sampling_method", "none");
    Node mesh = host.CreateNode("mesh"); host.SetNodes(mesh, "used_shaders", [material]);
    host.Mesh(mesh, [-1.5f,-1,0, 1.5f,-1,0, 0,1.5f,0], [0,1,2]);
    Node instance = host.CreateNode("object");
    host.SetNodes(instance, "geometry", [mesh]);
    host.SetString(instance, "asset_name", "Matériau — 三角形");
    Node world = host.CreateNode("shader"), background = host.ShaderNode(world, "background_shader");
    host.SetFloats(background, "color", [.25f,.4f,.6f]); host.SetFloats(background, "strength", [.8f]);
    host.Connect(background, "Background", host.ShaderOutput(world), "Surface");
    host.SetString(world, "emission_sampling_method", "none");
    host.SetNodes(host.SceneNode("background"), "shader", [world]);
    return color;
}
static async Task WaitActive(CyclesHost host, Task render)
{
    Stopwatch timer = Stopwatch.StartNew();
    while (!host.IsRendering)
    {
        Require(!render.IsCompleted && timer.Elapsed < TimeSpan.FromSeconds(30), "Render did not become active");
        await Task.Delay(1);
    }
}
Require(args.Length == 2, "Usage: Consumer CPU_REFERENCE_DIRECTORY OUTPUT_DIRECTORY");
Directory.CreateDirectory(args[1]);
using (CyclesHost properties = new(Backend.CpuReference))
{
    Node instance = properties.CreateNode("object");
    const string name = "Matériau — 三角形";
    properties.SetString(instance, "asset_name", name);
    Require(!properties.GetUtf8(instance, "asset_name", 0, Span<byte>.Empty, out ulong size), "Expected UTF-8 size query");
    byte[] utf8 = Enumerable.Repeat((byte)'?', checked((int)size)).ToArray();
    Require(!properties.GetUtf8(instance, "asset_name", 0, utf8.AsSpan(0, utf8.Length - 1), out size), "Expected small-buffer result");
    Require(utf8.All(value => value == '?'), "Small text buffer changed");
    Require(properties.GetUtf8(instance, "asset_name", 0, utf8, out size), "UTF-8 copy");
    Require(Encoding.UTF8.GetString(utf8, 0, (int)size-1) == name && utf8[(int)size-1] == 0, "UTF-8 roundtrip");
    properties.SetUInt(instance, "light_set_membership", ulong.MaxValue);
    Require(properties.GetUInt(instance, "light_set_membership") == ulong.MaxValue, "Unsigned 64-bit roundtrip");
    float[] motion = [1,0,0,2, 0,1,0,3, 0,0,1,4, 2,0,0,5, 0,2,0,6, 0,0,2,7];
    properties.SetFloats(instance, "motion", motion);
    PropertyInfo info = properties.Property(instance, "motion");
    Require(info.ValueType == 13 && info.Components == 12 && info.Count == 2 && (info.Flags & 1) == 1, "Transform array shape");
    Require(!properties.GetFloats(instance, "motion", Span<float>.Empty, out size) && size == 24, "Float size query");
    float[] copied = new float[(int)size]; Require(properties.GetFloats(instance, "motion", copied, out size) && motion.SequenceEqual(copied), "Transform array roundtrip");
    properties.SetFloats(instance, "motion", []);
    Require(properties.GetFloats(instance, "motion", Span<float>.Empty, out size) && size == 0, "Empty transform array");
    Node mesh = properties.CreateNode("mesh");
    properties.SetInts(mesh, "smooth", [0,1,0]);
    long[] flags = new long[3]; Require(properties.GetInts(mesh, "smooth", flags, out size) && flags.SequenceEqual(new long[]{0,1,0}), "Boolean array roundtrip");
    properties.SetNodes(instance, "geometry", [mesh]);
    ulong[] nodes = new ulong[1]; Require(properties.GetNodes(instance, "geometry", nodes, out size) && nodes[0] == mesh.Value, "Node handle roundtrip");
}
string registry = CyclesHost.RegistryJson();
using (JsonDocument document = JsonDocument.Parse(registry))
{
    JsonElement.ArrayEnumerator nodes = document.RootElement.GetProperty("nodeTypes").EnumerateArray();
    Require(nodes.Count() == 184, "Complete registry retention");
    Require(nodes.Sum(node => node.GetProperty("inputs").GetArrayLength() + node.GetProperty("outputs").GetArrayLength()) == 1277, "Complete socket registry");
}
File.WriteAllText(Path.Combine(args[1], "registry.json"), registry);
CyclesHost.ConfigureTiers(PipelineTier.Auto, null);
Rejected(() => CyclesHost.ConfigureTiers((PipelineTier)7, null), 1);
Rejected(() => CyclesHost.ConfigureTiers(PipelineTier.Auto, null, (PipelineTierFlags)8), 1);
using CyclesHost host = new(Backend.CpuReference);
using (JsonDocument report = JsonDocument.Parse(host.DeviceReportJson()))
    Require(report.RootElement.GetProperty("backend").GetString() == "cpu" && report.RootElement.GetProperty("pipelines").ValueKind == JsonValueKind.Null, "CPU device report");
Node color = Fixture(host);
using (CyclesHost other = new(Backend.CpuReference))
    Rejected(() => host.SetFloats(other.SceneNode("camera"), "fov", [1]), 1);
Require(!host.Cancel(), "Idle cancellation result");
float[] pixels = new float[32*24*4];
for (int round = 0; round < 3; ++round)
{
    if (round == 1) host.SetFloats(color, "value", [.1f,.65f,.3f]);
    Array.Fill(pixels, -99);
    Rejected(() => host.Render(32, 24, 8, pixels.AsSpan(0, pixels.Length-1)), 1);
    Require(pixels.All(x => x == -99), "Rejected render changed output");
    host.Render(32, 24, 8, pixels);
    byte[] bytes = MemoryMarshal.AsBytes(pixels.AsSpan()).ToArray();
    Require(bytes.SequenceEqual(File.ReadAllBytes(Path.Combine(args[0], $"run.{round}.rgba.bin"))), "Managed film differs from original CPU Session");
    File.WriteAllBytes(Path.Combine(args[1], $"run.{round}.rgba.bin"), bytes);
}
float[] previous = (float[])pixels.Clone();
host.Render(32, 24, 64, pixels); host.Render(32, 24, 8, pixels);
Require(previous.SequenceEqual(pixels), "Reducing sample budget changed repeat");
Array.Fill(pixels, -99);
Task running = Task.Run(() => Rejected(() => host.Render(32,24,1000000,pixels), 4));
await WaitActive(host, running);
Require(host.Cancel(), "Active cancellation result");
await running.WaitAsync(TimeSpan.FromSeconds(30));
Require(pixels.All(x => x == -99), "Cancelled render changed output");
host.Render(32,24,8,pixels); Require(previous.SequenceEqual(pixels), "Render after cancellation changed repeat");
Array.Fill(pixels, -99);
running = Task.Run(() => Rejected(() => host.Render(32,24,1000000,pixels), 4));
await WaitActive(host, running); host.Dispose();
await running.WaitAsync(TimeSpan.FromSeconds(30));
Require(pixels.All(x => x == -99), "Dispose during render changed output");
try { host.SceneNode("camera"); throw new Exception("Disposed host accepted a call"); } catch (ObjectDisposedException) { }
host.Dispose();
Console.WriteLine("{\"passed\":true,\"thinManagedConsumer\":true,\"cpuReferenceBitwiseEqual\":true,\"cancelAndReuse\":true,\"disposeDuringRender\":true,\"utf8Strings\":true,\"foreignHandlesRejected\":true,\"deviceReport\":true,\"tierConfigValidated\":true}");
