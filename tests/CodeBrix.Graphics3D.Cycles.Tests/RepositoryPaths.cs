// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.IO;
using System.Runtime.InteropServices;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Locates files in the repository working tree the tests run from.
internal static class RepositoryPaths
{
    private const string SolutionFileName = "CodeBrix.Graphics3D.Cycles.slnx";

    // The repository root (the folder holding the .slnx), or null when the tests
    // are not running from inside a working tree.
    internal static string Root { get; } = FindRoot();

    internal static string NativeHostHeader => Root == null ? null : Path.Combine(Root, "native", "host", "cycles_host.h");

    // Where native/tools/build_cycles.py places the Linux x64 native host.
    internal static string LinuxNativeHostLibrary => Root == null ? null : Path.Combine(Root, "native", "work", "cycles-build", "lib", "libcodebrix_cycles_host.so");

    // Where tools/build_macos.py places the osx-arm64 (Metal) native host; its Metal kernel
    // sources are staged beside it in codebrix-cycles/source.
    internal static string MacNativeHostLibrary => Root == null ? null : Path.Combine(Root, "native", "work", "macos", "osx-arm64", "host", "build", "lib", "libcodebrix_cycles_host.dylib");

    // The in-tree native host for this process's platform, or null where none is built in-tree.
    internal static string NativeHostLibrary =>
        OperatingSystem.IsLinux() && RuntimeInformation.ProcessArchitecture == Architecture.X64 ? LinuxNativeHostLibrary
        : OperatingSystem.IsMacOS() && RuntimeInformation.ProcessArchitecture == Architecture.Arm64 ? MacNativeHostLibrary
        : null;

    // The command that builds NativeHostLibrary.
    internal static string NativeHostBuildCommand =>
        OperatingSystem.IsMacOS() ? "python3 tools/build_macos.py --jobs 8" : "python3 native/tools/build_cycles.py --jobs 6";

    private static string FindRoot()
    {
        for (DirectoryInfo directory = new(AppContext.BaseDirectory); directory != null; directory = directory.Parent)
        {
            if (File.Exists(Path.Combine(directory.FullName, SolutionFileName))) return directory.FullName;
        }
        return null;
    }
}
