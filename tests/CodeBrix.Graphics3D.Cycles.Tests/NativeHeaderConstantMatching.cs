// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text.RegularExpressions;
using CodeBrix.Graphics3D.Cycles.Rendering;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// Every managed enum value must equal the CBC_* constant of the same meaning in
// native/host/cycles_host.h, because the values cross the C ABI unchanged.
public class NativeHeaderConstantMatching
{
    private static readonly Lazy<Dictionary<string, long>> HeaderConstants = new(ParseHeader);

    public static TheoryData<string, long> ManagedValues => new()
    {
        { "CBC_HOST_WEBGPU", (long)Backend.WebGpu },
        { "CBC_HOST_CPU_REFERENCE", (long)Backend.CpuReference },
        { "CBC_HOST_METAL", (long)Backend.Metal },
        { "CBC_HOST_GPU", (long)Backend.Gpu },
        { "CBC_EVENT_STATUS", (long)RenderEventKind.Status },
        { "CBC_EVENT_PROGRESS", (long)RenderEventKind.Progress },
        { "CBC_EVENT_ERROR", (long)RenderEventKind.Error },
        { "CBC_VALUE_FLOAT", (long)AttributeValue.Float },
        { "CBC_VALUE_COLOR", (long)AttributeValue.Color },
        { "CBC_VALUE_VECTOR", (long)AttributeValue.Vector },
        { "CBC_VALUE_POINT", (long)AttributeValue.Point },
        { "CBC_VALUE_NORMAL", (long)AttributeValue.Normal },
        { "CBC_VALUE_POINT2", (long)AttributeValue.Point2 },
        { "CBC_VALUE_TRANSFORM", (long)AttributeValue.Transform },
        { "CBC_VALUE_FLOAT4", (long)AttributeValue.Float4 },
        { "CBC_VALUE_BYTE4", (long)AttributeValue.Byte4 },
        { "CBC_ATTR_MESH", (long)AttributeDomain.Mesh },
        { "CBC_ATTR_FACE", (long)AttributeDomain.Face },
        { "CBC_ATTR_VERTEX", (long)AttributeDomain.Vertex },
        { "CBC_ATTR_CORNER", (long)AttributeDomain.Corner },
        { "CBC_ATTR_CURVE", (long)AttributeDomain.Curve },
        { "CBC_ATTR_CURVE_KEY", (long)AttributeDomain.CurveKey },
        { "CBC_ATTR_VERTEX_NORMAL", (long)AttributeDomain.VertexNormal },
        { "CBC_ATTR_CORNER_NORMAL", (long)AttributeDomain.CornerNormal },
        { "CBC_ATTR_CORNER_BYTE", (long)AttributeDomain.CornerByte },
        { "CBC_ATTR_CURVE_KEY_NORMAL", (long)AttributeDomain.CurveKeyNormal },
        { "CBC_ATTR_STANDARD", (long)AttributeFlags.Standard },
        { "CBC_ATTR_SUBDIVISION", (long)AttributeFlags.Subdivision },
        { "CBC_IMAGE_FLOAT", (long)ImageFormat.Float },
        { "CBC_IMAGE_FLOAT4", (long)ImageFormat.Float4 },
        { "CBC_IMAGE_HALF", (long)ImageFormat.Half },
        { "CBC_IMAGE_HALF4", (long)ImageFormat.Half4 },
        { "CBC_IMAGE_BYTE", (long)ImageFormat.Byte },
        { "CBC_IMAGE_BYTE4", (long)ImageFormat.Byte4 },
        { "CBC_IMAGE_USHORT", (long)ImageFormat.UShort },
        { "CBC_IMAGE_USHORT4", (long)ImageFormat.UShort4 },
        { "CBC_TIER_AUTO", (long)PipelineTier.Auto },
        { "CBC_TIER_FULL", (long)PipelineTier.Full },
        { "CBC_TIER_CONSERVATIVE", (long)PipelineTier.Conservative },
        { "CBC_TIER_RECORD_OFF", (long)PipelineTierFlags.RecordOff },
        { "CBC_TIER_RECORD_READ_ONLY", (long)PipelineTierFlags.RecordReadOnly },
        { "CBC_TIER_CANARY", (long)PipelineTierFlags.Canary },
        { "CBC_OK", (long)NativeStatus.Ok },
        { "CBC_INVALID", (long)NativeStatus.Invalid },
        { "CBC_UNSUPPORTED", (long)NativeStatus.Unsupported },
        { "CBC_FAILED", (long)NativeStatus.Failed },
        { "CBC_CANCELLED", (long)NativeStatus.Cancelled },
        { "CBC_NOT_READY", (long)NativeStatus.NotReady },
        { "CBC_SMALL_BUFFER", (long)NativeStatus.SmallBuffer },
        { "CBC_VALUE_UNSUPPORTED", (long)ValueKind.Unsupported },
        { "CBC_VALUE_BOOLEAN", (long)ValueKind.Boolean },
        { "CBC_VALUE_INT", (long)ValueKind.Int },
        { "CBC_VALUE_UINT", (long)ValueKind.UInt },
        { "CBC_VALUE_UINT64", (long)ValueKind.UInt64 },
        { "CBC_VALUE_FLOAT", (long)ValueKind.Float },
        { "CBC_VALUE_COLOR", (long)ValueKind.Color },
        { "CBC_VALUE_VECTOR", (long)ValueKind.Vector },
        { "CBC_VALUE_POINT", (long)ValueKind.Point },
        { "CBC_VALUE_NORMAL", (long)ValueKind.Normal },
        { "CBC_VALUE_POINT2", (long)ValueKind.Point2 },
        { "CBC_VALUE_STRING", (long)ValueKind.String },
        { "CBC_VALUE_ENUM", (long)ValueKind.Enum },
        { "CBC_VALUE_TRANSFORM", (long)ValueKind.Transform },
        { "CBC_VALUE_NODE", (long)ValueKind.Node },
        { "CBC_VALUE_CLOSURE", (long)ValueKind.Closure },
        { "CBC_VALUE_FLOAT4", (long)ValueKind.Float4 },
        { "CBC_VALUE_BYTE4", (long)ValueKind.Byte4 },
        { "CBC_PROPERTY_ARRAY", (long)PropertyFlags.Array },
        { "CBC_PROPERTY_LINKABLE", (long)PropertyFlags.Linkable },
        { "CBC_PROPERTY_INTERNAL", (long)PropertyFlags.Internal },
        { "CBC_PROPERTY_READABLE", (long)PropertyFlags.Readable },
        { "CBC_PROPERTY_WRITABLE", (long)PropertyFlags.Writable },
        { "CBC_HOST_ABI", (long)CyclesVersion.ExpectedAbi },
    };

    [Theory]
    [MemberData(nameof(ManagedValues))]
    public void managed_enum_value_matches_native_header_constant(string constant, long managedValue)
    {
        //Arrange
        Dictionary<string, long> header = HeaderConstants.Value;

        //Assert
        header.Should().ContainKey(constant);
        header[constant].Should().Be(managedValue);
    }

    [Fact]
    public void every_enum_member_is_mapped_to_a_header_constant()
    {
        //Arrange
        Type[] enums = [typeof(Backend), typeof(AttributeValue), typeof(AttributeDomain), typeof(AttributeFlags),
                        typeof(ImageFormat), typeof(PipelineTier), typeof(PipelineTierFlags), typeof(NativeStatus),
                        typeof(ValueKind), typeof(PropertyFlags), typeof(RenderEventKind)];
        int flagNoneMembers = 3; // AttributeFlags.None, PipelineTierFlags.None and PropertyFlags.None have no header constant.
        int nonEnumConstants = 1; // CBC_HOST_ABI is checked against CyclesVersion.ExpectedAbi.

        //Act
        int members = enums.Sum(type => Enum.GetValues(type).Length);

        //Assert
        (members - flagNoneMembers).Should().Be(ManagedValues.Count - nonEnumConstants);
    }

    [Fact]
    public void flag_none_members_are_zero()
    {
        //Assert
        ((uint)AttributeFlags.None).Should().Be(0u);
        ((uint)PipelineTierFlags.None).Should().Be(0u);
        ((uint)PropertyFlags.None).Should().Be(0u);
    }

    private static Dictionary<string, long> ParseHeader()
    {
        Assert.SkipWhen(RepositoryPaths.NativeHostHeader == null, "The tests are not running from inside the repository working tree.");
        string text = File.ReadAllText(RepositoryPaths.NativeHostHeader);
        Dictionary<string, long> constants = new();
        foreach (Match match in Regex.Matches(text, @"\b(CBC_[A-Z0-9_]+)\s*=\s*([0-9]+)"))
        {
            constants[match.Groups[1].Value] = long.Parse(match.Groups[2].Value);
        }
        return constants;
    }
}
