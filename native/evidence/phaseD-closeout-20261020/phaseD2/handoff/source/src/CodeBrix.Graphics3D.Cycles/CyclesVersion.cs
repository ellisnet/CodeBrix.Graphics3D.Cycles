// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using CodeBrix.Graphics3D.Cycles.Interop;

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// Version information for the managed library and the native host it loads.
/// </summary>
/// <remarks>
/// The native host currently reports only its ABI integer. <see cref="NativeLibraryVersion"/> and
/// <see cref="CyclesRevision"/> are <see langword="null"/> until the native host exports its version
/// structure; they are filled from it without any change to this API.
/// </remarks>
public static class CyclesVersion
{
    /// <summary>
    /// The native ABI version this managed library was built against (<c>CBC_HOST_ABI</c>).
    /// </summary>
    public const uint ExpectedAbi = CyclesAbi.ExpectedAbiVersion;

    /// <summary>
    /// Gets the ABI version the loaded native host implements. Reading it loads the native library.
    /// </summary>
    public static uint Abi
    {
        get
        {
            CyclesHost.EnsureSupportedOperatingSystem();
            return Native.AbiVersion();
        }
    }

    /// <summary>
    /// Gets a value indicating whether the loaded native host implements <see cref="ExpectedAbi"/>.
    /// </summary>
    public static bool IsAbiCompatible => Abi == ExpectedAbi;

    /// <summary>
    /// Gets the native library's own version, or <see langword="null"/> while the native host does
    /// not report it.
    /// </summary>
    public static string NativeLibraryVersion => null;

    /// <summary>
    /// Gets the pinned upstream Cycles revision the native host was built from, or
    /// <see langword="null"/> while the native host does not report it.
    /// </summary>
    public static string CyclesRevision => null;

    /// <summary>
    /// Gets the version of this managed assembly.
    /// </summary>
    public static Version ManagedVersion { get; } = typeof(CyclesVersion).Assembly.GetName().Version;
}
