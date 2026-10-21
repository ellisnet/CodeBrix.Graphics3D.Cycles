// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System.Text.RegularExpressions;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// The native version structure (cbc_host_version) through the typed facade.
public class CyclesVersionTests
{
    [Fact]
    public void native_version_fields_are_reported()
    {
        //Arrange
        TestScenes.RequireNativeHost();

        //Act
        string library = CyclesVersion.NativeLibraryVersion;
        string revision = CyclesVersion.CyclesRevision;

        //Assert
        Regex.IsMatch(library, @"^1\.0\.\d{8}\.\d+$").Should().BeTrue();
        Regex.IsMatch(revision, "^[0-9a-f]{40}$").Should().BeTrue();
        CyclesVersion.NativeAbi.Should().Be(CyclesVersion.Abi);
        CyclesVersion.NativeAbi.Should().Be(CyclesVersion.ExpectedAbi);
    }
}
