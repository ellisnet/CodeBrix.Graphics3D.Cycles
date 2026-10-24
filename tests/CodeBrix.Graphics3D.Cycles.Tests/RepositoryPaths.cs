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

    // The Windows RID of this process (win-x64 or win-arm64), or null off Windows.
    internal static string WindowsRid =>
        !OperatingSystem.IsWindows() ? null
        : RuntimeInformation.ProcessArchitecture == Architecture.X64 ? "win-x64"
        : RuntimeInformation.ProcessArchitecture == Architecture.Arm64 ? "win-arm64"
        : null;

    // Where native/tools/build_cycles_windows.py places the Windows native host (beside the
    // dependency DLLs, the WebGPU runtime and DXC). On a device that only tests (win-arm64 is
    // cross-built on x64), the reviewed payload in native/binaries/<rid>/runtime is used instead.
    internal static string WindowsNativeHostLibrary
    {
        get
        {
            if (Root == null || WindowsRid == null) return null;
            string built = Path.Combine(Root, "native", "work", "windows", WindowsRid, "host", "build", "bin", "codebrix_cycles_host.dll");
            string staged = Path.Combine(Root, "native", "binaries", WindowsRid, "runtime", "codebrix_cycles_host.dll");
            return File.Exists(built) || !File.Exists(staged) ? built : staged;
        }
    }

    // The staged Windows payload has no WebGPU runtime (it ships in the CodeBrix.WebGpu.Native
    // package); when the tests use that payload, this is the runtime from the NuGet cache (or
    // CODEBRIX_WEBGPU_PACKAGE), loaded before the host so the host's import resolves to it.
    internal static string WindowsWebGpuRuntime
    {
        get
        {
            string host = WindowsNativeHostLibrary;
            if (host == null || !host.Contains(Path.Combine("native", "binaries"))) return null;
            string package = Environment.GetEnvironmentVariable("CODEBRIX_WEBGPU_PACKAGE");
            if (string.IsNullOrEmpty(package))
                package = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.UserProfile), ".nuget", "packages",
                    "codebrix.webgpu.native.mitlicenseforever", "1.0.290.1433");
            return Path.Combine(package, "runtimes", WindowsRid, "native", "codebrix_wgpu.dll");
        }
    }

    // The in-tree native host for this process's platform, or null where none is built in-tree.
    internal static string NativeHostLibrary =>
        OperatingSystem.IsLinux() && RuntimeInformation.ProcessArchitecture == Architecture.X64 ? LinuxNativeHostLibrary
        : OperatingSystem.IsMacOS() && RuntimeInformation.ProcessArchitecture == Architecture.Arm64 ? MacNativeHostLibrary
        : OperatingSystem.IsWindows() ? WindowsNativeHostLibrary
        : null;

    // The command that builds NativeHostLibrary.
    internal static string NativeHostBuildCommand =>
        OperatingSystem.IsMacOS() ? "python3 tools/build_macos.py --jobs 8"
        : OperatingSystem.IsWindows() ? "python -X utf8 native/tools/build_cycles_windows.py --rid " + (WindowsRid ?? "win-x64") + " --jobs 16"
        : "python3 native/tools/build_cycles.py --jobs 6";

    private static string FindRoot()
    {
        for (DirectoryInfo directory = new(AppContext.BaseDirectory); directory != null; directory = directory.Parent)
        {
            if (File.Exists(Path.Combine(directory.FullName, SolutionFileName))) return directory.FullName;
        }
        return null;
    }
}
