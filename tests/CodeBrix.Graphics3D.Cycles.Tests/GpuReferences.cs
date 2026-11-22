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
    // Set on an Apple Silicon Mac to write Metal references from this run's films:
    //  - "1" writes only the names that have no reference yet; a name that has one is left alone and
    //    judged against it, so a visit that adds scenes cannot disturb the reviewed films;
    //  - "all" re-records every name (a new pinned Cycles revision or toolchain).
    // The new films and manifest are then reviewed like any other change.
    internal const string RecordVariable = "CODEBRIX_CYCLES_RECORD_METAL_REFERENCES";
    internal const string RecordMissingValue = "1";
    internal const string RecordAllValue = "all";

    internal enum Recording
    {
        Off,
        MissingOnly,
        All,
    }

    private static readonly object Sync = new();

    // Several tests render the same scene; a recording run keeps the first film of each name (per folder).
    private static readonly HashSet<string> RecordedThisRun = [];

    internal static string Folder => Path.Combine(RepositoryPaths.Root, "tests", "CodeBrix.Graphics3D.Cycles.Tests", "References", "metal-osx-arm64");

    private static string ManifestPath(string folder) => Path.Combine(folder, "manifest.json");

    // The recording mode a value of the recording variable asks for. A value that is neither is an error, never "off":
    // a mistyped setting must not pass for a run that recorded.
    internal static Recording ParseRecording(string value)
    {
        if (string.IsNullOrEmpty(value) || value == "0") return Recording.Off;
        if (value == RecordMissingValue) return Recording.MissingOnly;
        if (value == RecordAllValue) return Recording.All;
        throw new ArgumentException($"{RecordVariable}={value} is not a recording mode: use {RecordMissingValue} (write only the "
            + $"names that have no reference yet) or {RecordAllValue} (re-record every name).", nameof(value));
    }

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
        return JudgeAgainstRecorded(Folder, ParseRecording(Environment.GetEnvironmentVariable(RecordVariable)), name, film, versusCpu, log, recordedFacts);
    }

    // The Metal half of Judge with its folder and recording mode as arguments, so that the recording rules are tested
    // on every platform (the recording itself only ever runs on Apple Silicon).
    internal static FilmComparison JudgeAgainstRecorded(string folder, Recording recording, string name, ReadOnlySpan<float> film,
        FilmComparison versusCpu, Action<string> log, IReadOnlyDictionary<string, string> recordedFacts = null)
    {
        if (recording != Recording.Off && Record(folder, recording, name, film, versusCpu, recordedFacts))
            log($"{name}: Metal reference written ({(recording == Recording.All ? "re-recording every name" : "no reference yet")})");
        float[] reference = Load(folder, name, film.Length);
        FilmComparison versusMetal = FilmComparison.Compare(film, reference);
        log($"{name}: authority recorded Metal reference: {versusMetal}");
        return versusMetal;
    }

    private static float[] Load(string folder, string name, int length)
    {
        JsonObject entry = ReadManifest(folder)["references"]?[name] as JsonObject;
        if (entry == null)
            Assert.Fail($"No recorded Metal reference for '{name}'. Record the Metal references on Apple Silicon with "
                + RecordVariable + "=" + RecordMissingValue + " and review them (docs/TESTING-MACOS-ARM64.md).");
        string path = Path.Combine(folder, (string)entry["file"]);
        byte[] bytes = File.ReadAllBytes(path);
        if (Hash(bytes) != (string)entry["sha256"]) Assert.Fail($"Metal reference {path} does not match its recorded SHA-256.");
        if (bytes.Length != length * sizeof(float))
            Assert.Fail($"Metal reference '{name}' holds {bytes.Length / sizeof(float)} values; this film has {length}. "
                + "The scene or its render settings changed; remove that name's manifest entry and record it again ("
                + RecordVariable + "=" + RecordMissingValue + "), then review the new film.");
        return MemoryMarshal.Cast<byte, float>(bytes).ToArray();
    }

    // Writes the film as the name's reference when the mode says so; returns whether it wrote.
    private static bool Record(string folder, Recording recording, string name, ReadOnlySpan<float> film, FilmComparison versusCpu,
        IReadOnlyDictionary<string, string> facts)
    {
        byte[] bytes = MemoryMarshal.AsBytes(film).ToArray();
        string file = name + ".rgba.f32";
        lock (Sync)
        {
            JsonObject manifest = ReadManifest(folder);
            if (recording == Recording.MissingOnly && manifest["references"]?[name] != null) return false;
            if (!RecordedThisRun.Add(Path.GetFullPath(folder) + "\n" + name)) return false;
            Directory.CreateDirectory(folder);
            File.WriteAllBytes(Path.Combine(folder, file), bytes);
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
            File.WriteAllText(ManifestPath(folder), manifest.ToJsonString(new JsonSerializerOptions { WriteIndented = true }) + "\n");
            return true;
        }
    }

    private static JsonObject ReadManifest(string folder)
    {
        if (File.Exists(ManifestPath(folder))) return JsonNode.Parse(File.ReadAllText(ManifestPath(folder))).AsObject();
        return new JsonObject
        {
            ["policy"] = "Cycles' Metal device (osx-arm64) is judged against these reviewed Metal films with the practical criterion; "
                + "the CPU comparison recorded with each film is reported only. Re-record (" + RecordVariable + "=" + RecordAllValue + ") only when the "
                + "Cycles revision, the toolchain or a scene changes, and review the new films and CPU comparisons before committing.",
            ["references"] = new JsonObject(),
        };
    }

    private static string Hash(byte[] bytes) => Convert.ToHexStringLower(SHA256.HashData(bytes));
}
