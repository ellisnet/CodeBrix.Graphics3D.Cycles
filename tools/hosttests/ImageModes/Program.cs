// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
using System.Runtime.InteropServices;
using CodeBrix.Graphics3D.Cycles;
using System.Text.Json;

static void Require(bool value, string message) { if (!value) throw new Exception(message); }
static void Rejected(Action call, int expected)
{
    try { call(); } catch (NativeFailure error) { Require(error.Status == expected, error.ToString()); return; }
    throw new Exception("Expected native rejection " + expected);
}
static (Node Mesh, Node Material, Node Diffuse) Fixture(CyclesHost host)
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
    return (mesh, material, diffuse);
}
Require(args.Length == 2, "Usage: Consumer REFERENCE_DIRECTORY OUTPUT_DIRECTORY");
string configPath = Path.Combine(args[0], "config.ocio");
CyclesHost.ConfigureColor(configPath);
Rejected(() => CyclesHost.ConfigureColor(Path.Combine(args[0], "missing-config.ocio")), 3);
Directory.CreateDirectory(args[1]);
using CyclesHost host = new(Backend.CpuReference);
Rejected(() => CyclesHost.ConfigureColor(configPath), 5);
var (mesh, material, diffuse) = Fixture(host);
Node texture = host.ShaderNode(material, "image_texture"), uv = host.ShaderNode(material, "uvmap");
host.Connect(uv, "UV", texture, "Vector"); host.Connect(texture, "Color", diffuse, "Color");
host.SetString(texture, "interpolation", "linear"); host.SetString(texture, "extension", "periodic");
host.SetAttribute(mesh, "uv", AttributeValue.Point2, AttributeDomain.Corner, AttributeFlags.Standard, [-.3f,-.2f,1.7f,.1f,.1f,1.4f]);
int completed = 0;
float[] pixels = new float[32*24*4];
void Bind(Node node, int format)
{
    byte[] bytes = File.ReadAllBytes(Path.Combine(args[0], $"input.{format}.bin"));
    Image image = host.CreateImage(7, 5, (ImageFormat)(format+1), bytes);
    Array.Clear(bytes); host.BindImage(node, image); host.ReleaseImage(image);
    Rejected(() => host.BindImage(node, image), 1);
}
void RenderCase()
{
    host.Render(32, 24, 8, pixels);
    byte[] bytes = MemoryMarshal.AsBytes(pixels.AsSpan()).ToArray();
    string name = $"case.{completed}.rgba.bin";
    Require(bytes.SequenceEqual(File.ReadAllBytes(Path.Combine(args[0], name))), "Managed image mode differs from native reference: " + completed);
    File.WriteAllBytes(Path.Combine(args[1], name), bytes); ++completed;
}
string[] spaces = ["data", "scene_linear", "scene_linear_srgb", "__builtin_srgb", "", "cbc_scaled"];
string[] alphas = ["unassociated", "associated", "channel_packed", "ignore", "auto"];
for (int format = 0; format < 8; ++format)
{
    Bind(texture, format);
    foreach (string space in spaces) foreach (string alpha in alphas)
    {
        host.SetString(texture, "colorspace", space); host.SetString(texture, "alpha_type", alpha);
        if (space.Length == 0)
        {
            byte[] value = [99];
            Require(host.GetUtf8(texture, "colorspace", 0, value, out ulong required) && required == 1 && value[0] == 0, "Empty colorspace readback");
        }
        RenderCase();
    }
}
Node constant = host.ShaderNode(material, "color"); host.SetFloats(constant, "value", [.7f,.2f,.1f]);
host.Connect(constant, "Color", diffuse, "Color");
ulong[] worlds = new ulong[1]; Require(host.GetNodes(host.SceneNode("background"), "shader", worlds, out ulong count) && count == 1, "World shader");
Node world = new(worlds[0]), background = host.ShaderNode(world, "background_shader"), environment = host.ShaderNode(world, "environment_texture");
host.SetFloats(background, "strength", [.8f]);
host.Connect(environment, "Color", background, "Color"); host.Connect(background, "Background", host.ShaderOutput(world), "Surface");
host.SetString(environment, "colorspace", "cbc_scaled"); host.SetString(environment, "alpha_type", "channel_packed"); host.SetString(environment, "interpolation", "linear");
for (int format = 0; format < 8; ++format)
{
    Bind(environment, format);
    foreach (string projection in new[] { "equirectangular", "mirror_ball" })
    {
        host.SetString(environment, "projection", projection); RenderCase();
    }
}
Node worldColor = host.ShaderNode(world, "color"); host.SetFloats(worldColor, "value", [.25f,.4f,.6f]);
host.Connect(worldColor, "Color", background, "Color"); host.Connect(texture, "Color", diffuse, "Color");
host.SetString(texture, "colorspace", "cbc_scaled"); host.SetString(texture, "alpha_type", "channel_packed");
// Builds without image file readers (osx-arm64) reject texture filenames; the four
// file-and-rebind cases apply only where the native host reads image files. The
// host reports fileReading as false (no image file readers) or as the string
// "development-only" (files are read; native/host/host_api.inc).
JsonElement fileReading = JsonDocument.Parse(CyclesScene.GetBinaryCapabilities().Json).RootElement
    .GetProperty("imageInput").GetProperty("fileReading");
bool readsFiles = fileReading.ValueKind switch
{
    JsonValueKind.True => true,
    JsonValueKind.False => false,
    JsonValueKind.String when fileReading.GetString() == "development-only" => true,
    _ => throw new Exception("Unknown imageInput.fileReading value: " + fileReading.GetRawText())
};
if (readsFiles)
{
    foreach (int format in new[] { 5, 7 })
    {
        host.SetString(texture, "filename", Path.Combine(args[0], $"input.{format}.png")); host.BindImage(texture, default); RenderCase();
        Bind(texture, format); RenderCase();
    }
}
else Rejected(() => host.SetString(texture, "filename", Path.Combine(args[0], "input.5.png")), 2);
int fileCases = readsFiles ? 4 : 0;
Require(completed == 256 + fileCases, "Incomplete cases");
host.Dispose(); CyclesHost.ConfigureColor(configPath);
Console.WriteLine(JsonSerializer.Serialize(new { passed = true, thinManagedConsumer = true, cases = completed, imageFormats = 8,
    colorspaces = 6, alphaModes = 5, environmentCases = 16, fileAndRebindCases = fileCases, cpuReferenceBitwiseEqual = true,
    emptyStringRoundtrip = true, copiedImagesAndReleasedBindings = true }));
