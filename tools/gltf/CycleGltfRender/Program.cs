// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Threading.Tasks;
using CodeBrix.Graphics3D.Cycles.Bundles;
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
        "Usage: CodeBrix.Graphics3D.Cycles.GltfRender MODEL --output FILM.bin [options]\n" +
        "       CodeBrix.Graphics3D.Cycles.GltfRender MODEL --write-bundle DIR [options]\n" +
        "       CodeBrix.Graphics3D.Cycles.GltfRender --bundle SCENE.json --output FILM.bin [--backend cpu|gpu | --compare]\n\n" +
        "  --backend cpu|gpu      backend (default gpu; cpu = explicit CPU reference)\n" +
        "  --compare              render on both backends; writes FILM.cpu.bin and FILM.gpu.bin and prints\n" +
        "                         max / practical / strict statistics (exit code 3 on a practical failure)\n" +
        "  --width N --height N   film size (default 64 x 48)\n" +
        "  --samples N            samples per pixel (default 16)\n" +
        "  --camera N             perspective camera index in scene-graph order (default: the first)\n" +
        "  --default-camera       ignore the file's cameras and frame the scene bounds\n" +
        "  --material-mapping M   closures (default; factors only) or blender (the Blender glTF importer's node graph,\n" +
        "                         Principled BSDF with image textures; the glTF's images are decoded with CodeBrix.Imaging)\n" +
        "  --textures DIR         blender mapping with pre-decoded raw RGBA8 files (textures.json manifest) instead of decoding\n" +
        "  --write-bundle DIR     write the converted scene as a cbcOracleScene v1 bundle (scene.json + raw geometry,\n" +
        "                         attribute and pixel files) into a new or empty DIR; renders too when --output is given\n" +
        "  --bundle-id ID         the scene id recorded in the bundle (default gltf/<file name>)\n" +
        "  --repeats N            render.repeats recorded in the bundle (default 2)\n" +
        "  --bundle SCENE.json    render a saved bundle through the library's bundle loader (size and samples from it)\n" +
        "  --native-library PATH  the native host (else CODEBRIX_CYCLES_NATIVE, else the in-tree Linux x64 or macOS arm64 build)\n" +
        "  --write-sample PATH    write the procedural sample model (.glb) and exit; add --sample-sun for a directional light,\n" +
        "                         or --sample-textured for the textured-plane control (its decoded textures go to PATH.textures)\n\n" +
        "Output: FILM.bin = raw little-endian float32 RGBA (scene-linear, row 0 at the bottom) and FILM.bin.json\n" +
        "(width, height, samples, backend, sha256). No image encoding is done.\n\n" +
        "Image decoding: the Cycles library takes decoded pixels only. This tool decodes the glTF's images with CodeBrix.Imaging\n" +
        "(the recommended way to decode bitmaps for the image API) and hands RGBA8 rows, bottom row first, to Cycles.\n" +
        "--write-bundle also writes bundle-manifest.json (source hashes, licence-review matches, decoder identity, file hashes).\n\n" +
        GltfSceneConverter.MappingSummary + "\n";

    /// <summary>
    /// Runs the tool.
    /// </summary>
    /// <param name="args">The command-line arguments.</param>
    /// <returns>0 on success, 2 on a usage error, 3 when a comparison fails the practical criterion, 1 on any other failure.</returns>
    public static async Task<int> Main(string[] args)
    {
        string model = null, output = null, backendName = "gpu", nativeLibrary = null, sample = null;
        string textures = null, mapping = null, bundleOut = null, bundleId = null, bundleIn = null;
        uint width = 64, height = 48, samples = 16, repeats = 2;
        int camera = -1;
        bool compare = false, defaultCamera = false, sampleSun = false, sampleTextured = false;
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
                    case "--sample-textured": sampleTextured = true; break;
                    case "--textures": textures = Next(); break;
                    case "--material-mapping": mapping = Next(); break;
                    case "--write-bundle": bundleOut = Next(); break;
                    case "--bundle-id": bundleId = Next(); break;
                    case "--repeats": repeats = uint.Parse(Next(), CultureInfo.InvariantCulture); break;
                    case "--bundle": bundleIn = Next(); break;
                    default:
                        if (args[i].StartsWith("--", StringComparison.Ordinal) || model != null) throw new ArgumentException("Unexpected argument: " + args[i]);
                        model = args[i];
                        break;
                }
            }
            if (sample != null)
            {
                if (sampleTextured)
                {
                    SampleModel.CreateTexturedPlane().SaveGLB(Path.GetFullPath(sample));
                    SampleModel.WriteTexturedPlaneTextures(Path.GetFullPath(sample) + ".textures");
                }
                else SampleModel.Create(sampleSun).SaveGLB(Path.GetFullPath(sample));
                Console.WriteLine("wrote " + sample);
                return 0;
            }
            if (bundleIn != null && (model != null || bundleOut != null || textures != null || mapping != null))
                throw new ArgumentException("--bundle renders a saved bundle; it takes no MODEL, --write-bundle, --textures or --material-mapping.");
            if (bundleIn == null && model == null) throw new ArgumentException("MODEL (or --bundle) is required.");
            if (output == null && bundleOut == null) throw new ArgumentException("--output (or --write-bundle) is required.");
            if (mapping is not (null or "closures" or "blender")) throw new ArgumentException("--material-mapping must be closures or blender.");
            if (textures != null && mapping == "closures") throw new ArgumentException("--textures needs --material-mapping blender.");
            if (backendName is not ("cpu" or "gpu")) throw new ArgumentException("--backend must be cpu or gpu.");
            if (width == 0 || height == 0 || samples == 0 || repeats == 0) throw new ArgumentException("--width, --height, --samples and --repeats must be positive.");
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
            OracleSceneBundle bundle;
            string source;
            if (bundleIn != null)
            {
                bundle = OracleSceneBundle.Load(bundleIn);
                source = Path.GetFullPath(bundleIn);
                Console.WriteLine($"bundle {bundle.Id}: {bundle.Render.Width}x{bundle.Render.Height} @ {bundle.Render.Samples} spp, {bundle.FileNames.Count} files");
            }
            else
            {
                ModelRoot root = ModelRoot.Load(Path.GetFullPath(model));
                GltfConversionOptions options = new()
                {
                    Width = width, Height = height, CameraIndex = camera, ForceDefaultCamera = defaultCamera,
                    MaterialMapping = mapping == "blender" || textures != null ? GltfMaterialMapping.Blender : GltfMaterialMapping.Closures,
                };
                if (options.MaterialMapping == GltfMaterialMapping.Blender)
                    options.Textures = textures != null ? TextureSet.Load(textures) : TextureSet.Decode(root, NativeLibraryLocator.LicenseReview());
                bundle = GltfSceneConverter.ToBundle(root, options, new RenderSettings(width, height, samples), out GltfConversionResult result,
                    bundleId ?? "gltf/" + Path.GetFileNameWithoutExtension(model), "glTF file " + Path.GetFileName(model) + " converted by tools/gltf/CycleGltfRender (" +
                    (options.MaterialMapping == GltfMaterialMapping.Blender ? "Blender-importer material mapping" : "closure material mapping") + ")", repeats);
                source = Path.GetFullPath(model);
                Console.WriteLine($"converted: {result.Meshes} meshes, {result.MeshObjects} objects, {result.Triangles} triangles, {result.Materials} materials, {result.Lights} lights" +
                                  (result.DefaultLightAdded ? " (default sun)" : "") + (result.DefaultCameraUsed ? ", default camera" : "") +
                                  (options.Textures == null ? "" : $", {options.Textures.Images.Count} decoded images"));
                foreach (string note in result.Notes) Console.WriteLine("conversion: " + note);
                foreach (string warning in result.Warnings) Console.WriteLine("warning: " + warning);
                if (bundleOut != null)
                {
                    Console.WriteLine("wrote bundle " + bundle.Save(bundleOut));
                    BundleManifest.Write(bundleOut, bundle, Path.GetFullPath(model), options.Textures);
                }
            }
            if (output == null) return 0;
            RenderSettings settings = bundle.Render;
            if (!compare)
            {
                FilmBuffer film = await RenderAsync(bundle, source, settings, backendName == "cpu" ? CyclesBackend.CpuReference : CyclesBackend.Gpu, output);
                return film == null ? 1 : 0;
            }
            string stem = output.EndsWith(".bin", StringComparison.OrdinalIgnoreCase) ? output[..^4] : output;
            FilmBuffer cpu = await RenderAsync(bundle, source, settings, CyclesBackend.CpuReference, stem + ".cpu.bin");
            FilmBuffer gpu = await RenderAsync(bundle, source, settings, CyclesBackend.Gpu, stem + ".gpu.bin");
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

    private static async Task<FilmBuffer> RenderAsync(OracleSceneBundle bundle, string model, RenderSettings settings, CyclesBackend backend, string output)
    {
        using CyclesScene scene = new(backend);
        bundle.Apply(scene);
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
