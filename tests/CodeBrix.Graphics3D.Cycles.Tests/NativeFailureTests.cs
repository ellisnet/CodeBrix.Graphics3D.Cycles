// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

public class NativeFailureTests
{
    [Fact]
    public void Status_carries_the_native_status_code()
        => new NativeFailure(6, "buffer too small").Status.Should().Be(6);

    [Fact]
    public void Message_carries_the_native_error_text()
        => new NativeFailure(1, "Unknown socket").Message.Should().Be("Unknown socket");
}
