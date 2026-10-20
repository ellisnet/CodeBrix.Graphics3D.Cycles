// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Points the library's P/Invokes at the native host built in this working tree
// (the same library the host-gate fixtures load through LD_LIBRARY_PATH). When it
// is absent the native tests skip with SkipReason instead of failing.
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
        => libraryName == LibraryName ? NativeLibrary.Load(RepositoryPaths.LinuxNativeHostLibrary) : IntPtr.Zero;

    private static string FindSkipReason()
    {
        if (!OperatingSystem.IsLinux() || RuntimeInformation.ProcessArchitecture != Architecture.X64)
            return "The native-host tests run on Linux x64 only (the native host is built in-tree for that RID).";
        if (RepositoryPaths.Root == null)
            return "The tests are not running from inside the repository working tree, so the native host cannot be located.";
        if (!File.Exists(RepositoryPaths.LinuxNativeHostLibrary))
            return "The native host has not been built: " + RepositoryPaths.LinuxNativeHostLibrary
                + " is missing. Build it with `python3 native/tools/build_cycles.py --jobs 6` (see MAINTAINER-README.txt).";
        return null;
    }
}
