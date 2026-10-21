// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Threading.Tasks;
using CodeBrix.Graphics3D.Cycles.Diagnostics;
using CodeBrix.Graphics3D.Cycles.Rendering;
using CodeBrix.Graphics3D.Gltf2.Schema2;

namespace CodeBrix.Graphics3D.Cycles.GltfRender;

/// <summary>
/// The command-line entry point of the glTF render example.
/// </summary>
public static class Program
{
    private const string Usage =
        "CodeBrix.Graphics3D.Cycles.GltfRender - render a glTF 2.0 file (.gltf/.glb) with Cycles (example client)\n\n" +
        "Usage: CodeBrix.Graphics3D.Cycles.GltfRender MODEL --output FILM.bin [options]\n\n" +
        "  --backend cpu|gpu      backend (default gpu; cpu = explicit CPU reference)\n" +
        "  --compare              render on both backends; writes FILM.cpu.bin and FILM.gpu.bin and prints\n" +
        "                         max / practical / strict statistics (exit code 3 on a practical failure)\n" +
        "  --width N --height N   film size (default 64 x 48)\n" +
        "  --samples N            samples per pixel (default 16)\n" +
        "  --camera N             perspective camera index in scene-graph order (default: the first)\n" +
        "  --default-camera       ignore the file's cameras and frame the scene bounds\n" +
        "  --native-library PATH  the native host (else CODEBRIX_CYCLES_NATIVE, else the in-tree Linux build)\n" +
        "  --write-sample PATH    write the procedural sample model (.glb) and exit; add --sample-sun for a directional light\n\n" +
        "Output: FILM.bin = raw little-endian float32 RGBA (scene-linear, row 0 at the bottom) and FILM.bin.json\n" +
        "(width, height, samples, backend, sha256). No image encoding is done.\n\n" +
        "Textures are not decoded (Gltf2 carries image bytes only), so base-colour factors are used.\n\n" +
        GltfSceneConverter.MappingSummary + "\n";

    /// <summary>
    /// Runs the tool.
    /// </summary>
    /// <param name="args">The command-line arguments.</param>
    /// <returns>0 on success, 2 on a usage error, 3 when a comparison fails the practical criterion, 1 on any other failure.</returns>
    public static async Task<int> Main(string[] args)
    {
        string model = null, output = null, backendName = "gpu", nativeLibrary = null, sample = null;
        uint width = 64, height = 48, samples = 16;
        int camera = -1;
        bool compare = false, defaultCamera = false, sampleSun = false;
        try
        {
            for (int i = 0; i < args.Length; ++i)
            {
                string Next() => i + 1 < args.Length ? args[++i] : throw new ArgumentException("Missing value for " + args[i]);
                switch (args[i])
                {
                    case "--help": case "-h": Console.WriteLine(Usage); return 0;
                    case "--output": output = Next(); break;
                    case "--backend": backendName = Next(); break;
                    case "--compare": compare = true; break;
                    case "--width": width = uint.Parse(Next(), CultureInfo.InvariantCulture); break;
                    case "--height": height = uint.Parse(Next(), CultureInfo.InvariantCulture); break;
                    case "--samples": samples = uint.Parse(Next(), CultureInfo.InvariantCulture); break;
                    case "--camera": camera = int.Parse(Next(), CultureInfo.InvariantCulture); break;
                    case "--default-camera": defaultCamera = true; break;
                    case "--native-library": nativeLibrary = Next(); break;
                    case "--write-sample": sample = Next(); break;
                    case "--sample-sun": sampleSun = true; break;
                    default:
                        if (args[i].StartsWith("--", StringComparison.Ordinal) || model != null) throw new ArgumentException("Unexpected argument: " + args[i]);
                        model = args[i];
                        break;
                }
            }
            if (sample != null)
            {
                SampleModel.Create(sampleSun).SaveGLB(Path.GetFullPath(sample));
                Console.WriteLine("wrote " + sample);
                return 0;
            }
            if (model == null || output == null) throw new ArgumentException("MODEL and --output are required.");
            if (backendName is not ("cpu" or "gpu")) throw new ArgumentException("--backend must be cpu or gpu.");
            if (width == 0 || height == 0 || samples == 0) throw new ArgumentException("--width, --height and --samples must be positive.");
        }
        catch (Exception error) when (error is ArgumentException or FormatException or OverflowException)
        {
            Console.Error.WriteLine(error.Message);
            Console.Error.WriteLine(Usage);
            return 2;
        }

        try
        {
            string library = NativeLibraryLocator.Find(nativeLibrary);
            if (library != null) NativeLibraryLocator.Use(library);
            ModelRoot root = ModelRoot.Load(Path.GetFullPath(model));
            GltfConversionOptions options = new() { Width = width, Height = height, CameraIndex = camera, ForceDefaultCamera = defaultCamera };
            RenderSettings settings = new(width, height, samples);
            if (!compare)
            {
                FilmBuffer film = await RenderAsync(root, model, options, settings, backendName == "cpu" ? CyclesBackend.CpuReference : CyclesBackend.Gpu, output);
                return film == null ? 1 : 0;
            }
            string stem = output.EndsWith(".bin", StringComparison.OrdinalIgnoreCase) ? output[..^4] : output;
            FilmBuffer cpu = await RenderAsync(root, model, options, settings, CyclesBackend.CpuReference, stem + ".cpu.bin");
            FilmBuffer gpu = await RenderAsync(root, model, options, settings, CyclesBackend.Gpu, stem + ".gpu.bin");
            if (cpu == null || gpu == null) return 1;
            FilmComparison comparison = FilmComparison.Compare(gpu.Span, cpu.Span);
            Console.WriteLine("compare gpu vs cpu: " + comparison);
            return comparison.PracticalPass ? 0 : 3;
        }
        catch (Exception error)
        {
            Console.Error.WriteLine("error: " + error.Message);
            return 1;
        }
    }

    private static async Task<FilmBuffer> RenderAsync(ModelRoot root, string model, GltfConversionOptions options, RenderSettings settings, CyclesBackend backend, string output)
    {
        using CyclesScene scene = new(backend);
        GltfConversionResult result = GltfSceneConverter.Convert(root, scene, options);
        Console.WriteLine($"{scene.Backend}: {result.Meshes} meshes, {result.MeshObjects} objects, {result.Triangles} triangles, {result.Materials} materials, {result.Lights} lights" +
                          (result.DefaultLightAdded ? " (default sun)" : "") + (result.DefaultCameraUsed ? ", default camera" : ""));
        foreach (string warning in result.Warnings) Console.WriteLine("warning: " + warning);
        Stopwatch clock = Stopwatch.StartNew();
        FilmBuffer film = await scene.RenderAsync(settings);
        if (scene.IsFaulted)
        {
            Console.Error.WriteLine($"{scene.Backend}: the device faulted during the render.");
            return null;
        }
        ICapabilityReport report = scene.GetCapabilities();
        string sha256 = FilmFile.Write(output, film, scene.Backend, settings.Samples, Path.GetFullPath(model));
        Console.WriteLine($"{scene.Backend}: rendered {settings.Width}x{settings.Height} @ {settings.Samples} spp in {clock.Elapsed.TotalSeconds:F2} s on {report.DeviceDescription}" +
                          (report.PipelineTier == null ? "" : $" (tier {report.PipelineTier})") + $" -> {output} sha256 {sha256}");
        foreach (string warning in report.Warnings) Console.WriteLine($"{scene.Backend} device warning: {warning}");
        return film;
    }
}
