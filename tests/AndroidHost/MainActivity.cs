// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
using Android.App;
using Android.OS;
using Android.Widget;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;

[Activity(Name = "com.codebrix.cycles.hostvalidation.MainActivity", Label = "Cycles Host Validation", MainLauncher = true, Exported = true)]
public sealed class MainActivity : Activity
{
    protected override void OnCreate(Bundle? state)
    {
        base.OnCreate(state);
        TextView text = new(this) { Text = "Running native host validation…" };
        SetContentView(text);
        string directory = FilesDir!.AbsolutePath;
        _ = Task.Run(async () =>
        {
            object report;
            List<FilmComparison> comparisons = new();
            try
            {
                using Stream manifestStream = Assets!.Open("payload.json");
                using JsonDocument manifest = JsonDocument.Parse(manifestStream);
                string reference = Path.Combine(directory, "reference"), output = Path.Combine(directory, "output");
                foreach (JsonProperty asset in manifest.RootElement.GetProperty("references").EnumerateObject())
                {
                    string destination = Path.Combine(directory, asset.Name);
                    Directory.CreateDirectory(Path.GetDirectoryName(destination)!);
                    using (Stream input = Assets.Open(asset.Name))
                    using (FileStream file = File.Create(destination)) input.CopyTo(file);
                    if (Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(destination))) != asset.Value.GetString())
                        throw new Exception("Reference asset hash differs: " + asset.Name);
                }
                comparisons.AddRange(await HostChecks.Run([Path.Combine(reference, "cpu"), Path.Combine(output, "lifecycle")]));
                for (int round = 0; round < 6; ++round)
                {
                    string variant = ((char)('a' + round % 3)).ToString();
                    comparisons.AddRange(ImageChecks.Run([Path.Combine(reference, variant, "output"), Path.Combine(output, $"round.{round}")]));
                }
                if (comparisons.Count != 1563) throw new Exception("Incomplete managed film coverage");
                string nativeDirectory = ApplicationInfo!.NativeLibraryDir!;
                string abi = RuntimeInformation.ProcessArchitecture switch { Architecture.Arm64 => "arm64-v8a", Architecture.X64 => "x86_64", _ => throw new Exception("Unexpected ABI") };
                Dictionary<string, string> libraries = new();
                foreach (JsonProperty library in manifest.RootElement.GetProperty("native").GetProperty(abi).EnumerateObject())
                {
                    string digest = Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(Path.Combine(nativeDirectory, library.Name))));
                    if (digest != library.Value.GetString()) throw new Exception("Installed library hash differs: " + library.Name);
                    libraries[library.Name] = digest;
                }
                string maps = File.ReadAllText("/proc/self/maps");
                foreach (string name in new[] { "libcodebrix_cycles_host.so", "libcodebrix_wgpu.so" })
                    if (!maps.Contains(Path.Combine(nativeDirectory, name), StringComparison.Ordinal))
                        throw new Exception("Native library not mapped from app installation: " + name);
                if (maps.Contains("/data/local/tmp/codebrix-", StringComparison.Ordinal))
                    throw new Exception("App loaded shell-test libraries");
                File.WriteAllText(Path.Combine(directory, "native-maps.txt"), maps);
                report = new { passed = true, actualAndroidApp = true, explicitCpuReference = true,
                    architecture = RuntimeInformation.ProcessArchitecture.ToString(), apiLevel = (int)Build.VERSION.SdkInt,
                    nativeDirectory, libraries, comparisons, configurationRounds = 6, imageFilms = 1560,
                    imageFormats = 8, cancellationAndReuse = true, disposeDuringRender = true,
                    registryNodeTypes = 184, registrySockets = 1277, propertyReadbackChecks = true };
            }
            catch (Exception error) { report = new { passed = false, error = error.ToString(), comparisons }; }
            string json = JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true });
            string temporary = Path.Combine(directory, "result.tmp");
            File.WriteAllText(temporary, json);
            File.Move(temporary, Path.Combine(directory, "result.json"), true);
            RunOnUiThread(() => text.Text = json);
        });
    }
}
