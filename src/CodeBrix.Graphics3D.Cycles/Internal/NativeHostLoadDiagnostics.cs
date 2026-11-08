// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Runtime.InteropServices;
using CodeBrix.Graphics3D.Cycles.Interop;

namespace CodeBrix.Graphics3D.Cycles.Internal;

/// <summary>
/// Turns a failure to load the native scene host into a message that names the platform package to
/// install. The .Core package carries no native binaries, so referencing it without a platform
/// package is an expected mistake, and the runtime's own message only lists the paths it tried.
/// </summary>
internal static class NativeHostLoadDiagnostics
{
    private const string PackagePrefix = "CodeBrix.Graphics3D.Cycles.";
    private const string PackageSuffix = ".ApacheLicenseForever";

    private static volatile bool loaded;

    /// <summary>Loads the native host once; throws with the guidance below when it cannot be loaded.</summary>
    /// <exception cref="DllNotFoundException">The native host, or a library it depends on, could not be loaded.</exception>
    internal static void EnsureLoaded()
    {
        if (loaded) return;
        try
        {
            Native.AbiVersion();
            loaded = true;
        }
        catch (DllNotFoundException exception)
        {
            throw new DllNotFoundException(BuildMessage(CurrentPackageName(), exception.Message), exception);
        }
    }

    /// <summary>The platform package for this process, or null when the family has none for it.</summary>
    internal static string CurrentPackageName() =>
        PackageNameFor(CurrentPlatformName(), RuntimeInformation.ProcessArchitecture);

    /// <summary>The platform package for an operating system name and process architecture, or null.</summary>
    internal static string PackageNameFor(string platform, Architecture architecture)
    {
        string part = (platform, architecture) switch
        {
            ("Android", Architecture.X64 or Architecture.Arm64) => "Android",
            ("Linux", Architecture.X64) => "LinuxX64",
            ("Linux", Architecture.Arm64) => "LinuxArm64",
            ("Linux", Architecture.RiscV64) => "LinuxRiscv64",
            ("MacOS", Architecture.X64) => "MacOSX64",
            ("MacOS", Architecture.Arm64) => "MacOSArm64",
            ("Windows", Architecture.X64) => "WindowsX64",
            ("Windows", Architecture.Arm64) => "WindowsArm64",
            _ => null,
        };
        return part == null ? null : PackagePrefix + part + PackageSuffix;
    }

    /// <summary>The message for a load failure: the remedy first, the loader's own text after it.</summary>
    internal static string BuildMessage(string packageName, string loaderMessage)
    {
        string remedy = packageName == null
            ? "No CodeBrix.Graphics3D.Cycles platform package exists for this operating system and process architecture."
            : $"Add a reference to the platform package {packageName}. If it is already referenced, a library the native host depends on could not be loaded; see the loader's message below.";
        return $"The native Cycles scene host ({Native.Library}) could not be loaded. {remedy}{Environment.NewLine}Loader: {loaderMessage}";
    }

    private static string CurrentPlatformName()
    {
        if (OperatingSystem.IsAndroid()) return "Android";
        if (OperatingSystem.IsLinux()) return "Linux";
        if (OperatingSystem.IsMacOS()) return "MacOS";
        return OperatingSystem.IsWindows() ? "Windows" : null;
    }
}
