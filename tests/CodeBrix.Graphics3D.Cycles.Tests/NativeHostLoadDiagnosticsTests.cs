// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System.Runtime.InteropServices;
using CodeBrix.Graphics3D.Cycles.Internal;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

public class NativeHostLoadDiagnosticsTests
{
    [Theory]
    [InlineData("Linux", Architecture.X64, "LinuxX64")]
    [InlineData("Linux", Architecture.Arm64, "LinuxArm64")]
    [InlineData("Linux", Architecture.RiscV64, "LinuxRiscv64")]
    [InlineData("Windows", Architecture.X64, "WindowsX64")]
    [InlineData("Windows", Architecture.Arm64, "WindowsArm64")]
    [InlineData("MacOS", Architecture.X64, "MacOSX64")]
    [InlineData("MacOS", Architecture.Arm64, "MacOSArm64")]
    [InlineData("Android", Architecture.X64, "Android")]
    [InlineData("Android", Architecture.Arm64, "Android")]
    public void PackageNameFor_names_the_platform_package(string platform, Architecture architecture, string part) =>
        NativeHostLoadDiagnostics.PackageNameFor(platform, architecture).Should()
            .Be("CodeBrix.Graphics3D.Cycles." + part + ".ApacheLicenseForever");

    [Theory]
    [InlineData("Linux", Architecture.X86)]
    [InlineData("Windows", Architecture.X86)]
    [InlineData("Android", Architecture.RiscV64)]
    [InlineData(null, Architecture.X64)]
    public void PackageNameFor_is_null_without_a_platform_package(string platform, Architecture architecture) =>
        NativeHostLoadDiagnostics.PackageNameFor(platform, architecture).Should().BeNull();

    [Fact]
    public void BuildMessage_puts_the_package_before_the_loader_text()
    {
        //Arrange
        const string package = "CodeBrix.Graphics3D.Cycles.LinuxX64.ApacheLicenseForever";

        //Act
        string message = NativeHostLoadDiagnostics.BuildMessage(package, "the loader's text");

        //Assert
        message.Should().Contain(package);
        message.IndexOf(package, System.StringComparison.Ordinal).Should().BeLessThan(message.IndexOf("the loader's text", System.StringComparison.Ordinal));
    }

    [Fact]
    public void BuildMessage_says_when_no_platform_package_exists()
    {
        //Act
        string message = NativeHostLoadDiagnostics.BuildMessage(null, "the loader's text");

        //Assert
        message.Should().Contain("No CodeBrix.Graphics3D.Cycles platform package exists");
        message.Should().Contain("the loader's text");
    }
}
