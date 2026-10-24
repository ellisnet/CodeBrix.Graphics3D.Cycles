// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Points the library's P/Invokes at the native host built in this working tree for
// this platform (Linux x64 or macOS arm64). When it is absent the native tests skip
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
        => libraryName == LibraryName ? NativeLibrary.Load(RepositoryPaths.NativeHostLibrary) : IntPtr.Zero;

    private static string FindSkipReason()
    {
        if (RepositoryPaths.Root == null)
            return "The tests are not running from inside the repository working tree, so the native host cannot be located.";
        if (RepositoryPaths.NativeHostLibrary == null)
            return "The native-host tests run on Linux x64 (WebGPU) and macOS arm64 (Metal), where the native host is built in-tree.";
        if (!File.Exists(RepositoryPaths.NativeHostLibrary))
            return "The native host has not been built: " + RepositoryPaths.NativeHostLibrary
                + " is missing. Build it with `" + RepositoryPaths.NativeHostBuildCommand + "` (see MAINTAINER-README.txt).";
        return null;
    }
}
