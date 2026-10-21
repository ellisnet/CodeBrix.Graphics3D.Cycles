// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using CodeBrix.Graphics3D.Cycles.Interop;

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// Version information for the managed library and the native host it loads.
/// </summary>
/// <remarks>
/// <see cref="NativeLibraryVersion"/>, <see cref="NativeAbi"/> and <see cref="CyclesRevision"/> come
/// from the native version structure (<c>cbc_host_version</c>), read once on first use; reading them
/// loads the native library.
/// </remarks>
public static class CyclesVersion
{
    private static readonly Lazy<(uint Abi, string Library, string Revision)> NativeInfo = new(ReadNative);

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
    /// Gets the native library's own version, <c>major.minor.yyyyMMdd.build</c> as stamped by the
    /// native build (for example <c>1.0.20261020.0</c>).
    /// </summary>
    /// <exception cref="NativeFailure">The native host did not report its version.</exception>
    public static string NativeLibraryVersion => NativeInfo.Value.Library;

    /// <summary>
    /// Gets the ABI version reported in the native version structure (equal to <see cref="Abi"/>).
    /// </summary>
    /// <exception cref="NativeFailure">The native host did not report its version.</exception>
    public static uint NativeAbi => NativeInfo.Value.Abi;

    /// <summary>
    /// Gets the pinned upstream Cycles revision (a git commit hash) the native host was built from.
    /// </summary>
    /// <exception cref="NativeFailure">The native host did not report its version.</exception>
    public static string CyclesRevision => NativeInfo.Value.Revision;

    /// <summary>
    /// Gets the version of this managed assembly.
    /// </summary>
    public static Version ManagedVersion { get; } = typeof(CyclesVersion).Assembly.GetName().Version;

    private static (uint Abi, string Library, string Revision) ReadNative()
    {
        CyclesHost.EnsureSupportedOperatingSystem();
        NativeStatus status = CyclesAbi.Version(out uint abi, out string library, out string revision);
        if (status != NativeStatus.Ok) throw new NativeFailure((int)status, "The native host did not report its version: " + CyclesAbi.LastError());
        return (abi, library, revision);
    }
}
