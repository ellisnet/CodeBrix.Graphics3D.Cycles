// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.IO;

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

    private static string FindRoot()
    {
        for (DirectoryInfo directory = new(AppContext.BaseDirectory); directory != null; directory = directory.Parent)
        {
            if (File.Exists(Path.Combine(directory.FullName, SolutionFileName))) return directory.FullName;
        }
        return null;
    }
}
