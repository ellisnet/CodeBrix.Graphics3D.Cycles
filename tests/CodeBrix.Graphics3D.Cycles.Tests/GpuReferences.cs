// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using System.Text.Json.Nodes;
using CodeBrix.Graphics3D.Cycles.GltfRender;
using CodeBrix.Graphics3D.Cycles.Rendering;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// The authority a GPU film is judged against depends on the backend (docs/TESTING-MACOS-ARM64.md,
// "Reference policy"):
//  - WebGPU runs the CodeBrix WGSL device being built in this repository, so the CPU reference film
//    rendered in the same process is the authority.
//  - Metal runs Cycles' own, upstream-validated Metal device (osx-arm64), so a reviewed Metal film
//    recorded on Apple Silicon (References/metal-osx-arm64) is the authority. The CPU film is still
//    compared and reported, but never fails the test: Metal is not required to match the CPU.
// Every judged film must have no practical-criterion failures against its authority.
internal static class GpuReferences
{
    // Set to 1 on an Apple Silicon Mac to (re)write the Metal references from this run's films.
    // The new films and manifest are then reviewed like any other change before they are committed.
    internal const string RecordVariable = "CODEBRIX_CYCLES_RECORD_METAL_REFERENCES";

    private static readonly object Sync = new();

    // Several tests render the same scene; a recording run keeps the first film of each name.
    private static readonly HashSet<string> RecordedThisRun = [];

    internal static string Folder => Path.Combine(RepositoryPaths.Root, "tests", "CodeBrix.Graphics3D.Cycles.Tests", "References", "metal-osx-arm64");

    private static string ManifestPath => Path.Combine(Folder, "manifest.json");

    private static bool Recording => Environment.GetEnvironmentVariable(RecordVariable) == "1";

    // Judges a GPU film against its backend's authority and writes both comparisons to the test output.
    internal static FilmComparison Judge(Backend backend, string name, ReadOnlySpan<float> film, ReadOnlySpan<float> cpu,
        Action<string> log, IReadOnlyDictionary<string, string> recordedFacts = null)
    {
        FilmComparison versusCpu = FilmComparison.Compare(film, cpu);
        if (backend != Backend.Metal)
        {
            log($"{name}: authority CPU reference: {versusCpu}");
            return versusCpu;
        }
        log($"{name}: CPU comparison (reported, not required on Metal): {versusCpu}");
        if (Recording) Record(name, film, versusCpu, recordedFacts);
        float[] reference = Load(name, film.Length);
        FilmComparison versusMetal = FilmComparison.Compare(film, reference);
        log($"{name}: authority recorded Metal reference: {versusMetal}");
        return versusMetal;
    }

    private static float[] Load(string name, int length)
    {
        JsonObject entry = ReadManifest()["references"]?[name] as JsonObject;
        if (entry == null)
            Assert.Fail($"No recorded Metal reference for '{name}'. Record the Metal references on Apple Silicon with "
                + RecordVariable + "=1 and review them (docs/TESTING-MACOS-ARM64.md).");
        string path = Path.Combine(Folder, (string)entry["file"]);
        byte[] bytes = File.ReadAllBytes(path);
        if (Hash(bytes) != (string)entry["sha256"]) Assert.Fail($"Metal reference {path} does not match its recorded SHA-256.");
        if (bytes.Length != length * sizeof(float))
            Assert.Fail($"Metal reference '{name}' holds {bytes.Length / sizeof(float)} values; this film has {length}. "
                + "The scene or its render settings changed; re-record and review the Metal references.");
        return MemoryMarshal.Cast<byte, float>(bytes).ToArray();
    }

    private static void Record(string name, ReadOnlySpan<float> film, FilmComparison versusCpu, IReadOnlyDictionary<string, string> facts)
    {
        byte[] bytes = MemoryMarshal.AsBytes(film).ToArray();
        string file = name + ".rgba.f32";
        lock (Sync)
        {
            if (!RecordedThisRun.Add(name)) return;
            Directory.CreateDirectory(Folder);
            File.WriteAllBytes(Path.Combine(Folder, file), bytes);
            JsonObject manifest = ReadManifest();
            JsonObject entry = new()
            {
                ["file"] = file,
                ["sha256"] = Hash(bytes),
                ["components"] = film.Length,
                ["layout"] = "float32 little-endian RGBA, lower-left origin",
                ["recordedUtc"] = DateTime.UtcNow.ToString("yyyy-MM-ddTHH:mm:ssZ"),
                ["os"] = RuntimeInformation.OSDescription,
                ["cpuComparison"] = new JsonObject
                {
                    ["maximumAbsoluteError"] = versusCpu.MaximumAbsoluteError,
                    ["practicalFailures"] = versusCpu.PracticalFailures,
                    ["strictDifferences"] = versusCpu.StrictDifferences,
                },
            };
            foreach (KeyValuePair<string, string> fact in facts ?? new Dictionary<string, string>()) entry[fact.Key] = fact.Value;
            manifest["references"][name] = entry;
            File.WriteAllText(ManifestPath, manifest.ToJsonString(new JsonSerializerOptions { WriteIndented = true }) + "\n");
        }
    }

    private static JsonObject ReadManifest()
    {
        if (File.Exists(ManifestPath)) return JsonNode.Parse(File.ReadAllText(ManifestPath)).AsObject();
        return new JsonObject
        {
            ["policy"] = "Cycles' Metal device (osx-arm64) is judged against these reviewed Metal films with the practical criterion; "
                + "the CPU comparison recorded with each film is reported only. Re-record (" + RecordVariable + "=1) only when the "
                + "Cycles revision, the toolchain or a scene changes, and review the new films and CPU comparisons before committing.",
            ["references"] = new JsonObject(),
        };
    }

    private static string Hash(byte[] bytes) => Convert.ToHexStringLower(SHA256.HashData(bytes));
}
