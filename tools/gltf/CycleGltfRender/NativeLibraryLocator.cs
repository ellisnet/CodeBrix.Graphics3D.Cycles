// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;

namespace CodeBrix.Graphics3D.Cycles.GltfRender;

/// <summary>
/// Points the library's P/Invokes at a specific native host file. The package carries no runtime
/// assets yet, so the tool accepts an explicit path, the <c>CODEBRIX_CYCLES_NATIVE</c> environment
/// variable, or (when run from a working tree) the in-tree Linux build.
/// </summary>
public static class NativeLibraryLocator
{
    private const string LibraryName = "codebrix_cycles_host";
    private static readonly object Sync = new();
    private static string registeredPath;

    /// <summary>
    /// Finds the native host: <paramref name="explicitPath"/>, then <c>CODEBRIX_CYCLES_NATIVE</c>,
    /// then <c>native/work/cycles-build/lib/libcodebrix_cycles_host.so</c> in an enclosing working
    /// tree. Returns null when none exists (the default loader search then applies).
    /// </summary>
    /// <param name="explicitPath">An explicit library path, or null.</param>
    /// <returns>The full path, or null.</returns>
    public static string Find(string explicitPath = null)
    {
        if (!string.IsNullOrEmpty(explicitPath)) return Path.GetFullPath(explicitPath);
        string fromEnvironment = Environment.GetEnvironmentVariable("CODEBRIX_CYCLES_NATIVE");
        if (!string.IsNullOrEmpty(fromEnvironment)) return Path.GetFullPath(fromEnvironment);
        if (!OperatingSystem.IsLinux()) return null;
        for (DirectoryInfo directory = new(AppContext.BaseDirectory); directory != null; directory = directory.Parent)
        {
            if (!File.Exists(Path.Combine(directory.FullName, "CodeBrix.Graphics3D.Cycles.slnx"))) continue;
            string candidate = Path.Combine(directory.FullName, "native", "work", "cycles-build", "lib", "libcodebrix_cycles_host.so");
            return File.Exists(candidate) ? candidate : null;
        }
        return null;
    }

    /// <summary>
    /// Registers a resolver that loads <paramref name="path"/> for the native host. Only the first
    /// registration in a process takes effect.
    /// </summary>
    /// <param name="path">The native host file.</param>
    /// <exception cref="FileNotFoundException">The file does not exist.</exception>
    public static void Use(string path)
    {
        if (!File.Exists(path)) throw new FileNotFoundException("The native Cycles host was not found.", path);
        lock (Sync)
        {
            if (registeredPath != null) return;
            NativeLibrary.SetDllImportResolver(typeof(CyclesScene).Assembly, Resolve);
            registeredPath = path;
        }
    }

    private static IntPtr Resolve(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
        => libraryName == LibraryName ? NativeLibrary.Load(registeredPath) : IntPtr.Zero;
}
