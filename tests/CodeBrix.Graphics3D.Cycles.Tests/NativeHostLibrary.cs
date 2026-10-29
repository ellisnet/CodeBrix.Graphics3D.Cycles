// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Points the library's P/Invokes at the native host built in this working tree for
// this platform (Linux x64 or ARM64, macOS arm64 or x64, Windows x64 or ARM64). When it is absent the native tests skip
// with SkipReason instead of failing.
internal static class NativeHostLibrary
{
    private const string LibraryName = "codebrix_cycles_host";
    private static readonly object Sync = new();
    private static bool registered;

    internal static bool IsAvailable => SkipReason == null;

    internal static string SkipReason { get; } = FindSkipReason();

    // Registers the resolver once per process and reports whether native tests can run.
    internal static bool EnsureLoaded()
    {
        if (!IsAvailable) return false;
        lock (Sync)
        {
            if (!registered)
            {
                NativeLibrary.SetDllImportResolver(typeof(CyclesHost).Assembly, Resolve);
                registered = true;
            }
        }
        return true;
    }

    private static IntPtr Resolve(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (libraryName != LibraryName) return IntPtr.Zero;
        // A staged Windows payload carries no WebGPU runtime; load the package's copy first.
        string runtime = RepositoryPaths.WindowsWebGpuRuntime;
        if (runtime != null && File.Exists(runtime)) NativeLibrary.Load(runtime);
        return NativeLibrary.Load(RepositoryPaths.NativeHostLibrary);
    }

    private static string FindSkipReason()
    {
        if (RepositoryPaths.Root == null)
            return "The tests are not running from inside the repository working tree, so the native host cannot be located.";
        if (RepositoryPaths.NativeHostLibrary == null)
            return "The native-host tests run on Linux x64/ARM64, Windows x64/ARM64 and macOS x64 (WebGPU) and macOS arm64 (Metal), where the native host is built in-tree.";
        if (!File.Exists(RepositoryPaths.NativeHostLibrary))
            return "The native host has not been built: " + RepositoryPaths.NativeHostLibrary
                + " is missing. Build it with `" + RepositoryPaths.NativeHostBuildCommand + "` (see MAINTAINER-README.txt).";
        if (RepositoryPaths.WindowsWebGpuRuntime != null && !File.Exists(RepositoryPaths.WindowsWebGpuRuntime))
            return "The staged Windows payload needs the CodeBrix.WebGpu.Native runtime: " + RepositoryPaths.WindowsWebGpuRuntime
                + " is missing. Restore it with `dotnet restore research/GpuProbe/GpuProbe.csproj`.";
        return null;
    }
}
