// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

namespace CodeBrix.Graphics3D.Cycles;

/// <summary>
/// The pixel layout of a tightly packed raw image passed to <see cref="CyclesHost.CreateImage"/>.
/// The values equal the <c>CBC_IMAGE_*</c> constants of the native host ABI. Float and half
/// inputs use native-endian storage.
/// </summary>
public enum ImageFormat : uint
{
    /// <summary>One 32-bit float channel per pixel.</summary>
    Float = 1,

    /// <summary>Four 32-bit float channels per pixel.</summary>
    Float4 = 2,

    /// <summary>One 16-bit half-float channel per pixel.</summary>
    Half = 3,

    /// <summary>Four 16-bit half-float channels per pixel.</summary>
    Half4 = 4,

    /// <summary>One 8-bit channel per pixel.</summary>
    Byte = 5,

    /// <summary>Four 8-bit channels per pixel.</summary>
    Byte4 = 6,

    /// <summary>One 16-bit unsigned integer channel per pixel.</summary>
    UShort = 7,

    /// <summary>Four 16-bit unsigned integer channels per pixel.</summary>
    UShort4 = 8
}
