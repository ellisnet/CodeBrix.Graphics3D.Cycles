// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

public class CyclesExceptionTests
{
    [Fact]
    public void NativeFailure_is_a_cyclesexception_with_the_same_status()
    {
        //Act
        CyclesException failure = new NativeFailure(4, "cancelled");

        //Assert
        failure.NativeStatus.Should().Be(NativeStatus.Cancelled);
        failure.Message.Should().Be("cancelled");
    }

    [Fact]
    public void inner_exception_is_preserved()
    {
        //Arrange
        InvalidOperationException inner = new("inner");

        //Act
        CyclesException failure = new(NativeStatus.Failed, "outer", inner);

        //Assert
        failure.InnerException.Should().BeSameAs(inner);
        failure.NativeStatus.Should().Be(NativeStatus.Failed);
    }
}
