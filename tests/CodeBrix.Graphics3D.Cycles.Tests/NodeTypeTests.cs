// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Reflection;
using System.Text.Json.Nodes;
using CodeBrix.Graphics3D.Cycles.Generated;
using CodeBrix.Graphics3D.Cycles.Generated.Scene;
using CodeBrix.Graphics3D.Cycles.Nodes;
using SilverAssertions;
using Xunit;

namespace CodeBrix.Graphics3D.Cycles.Tests;

// The generated catalogue and typed model against the checked-in registry snapshot, the live native
// registry, and live round trips of every generated property.
public class NodeTypeTests
{
    private static string RegistryPath => RepositoryPaths.Root == null ? null
        : Path.Combine(RepositoryPaths.Root, "src", "CodeBrix.Graphics3D.Cycles", "Generated", "registry.json");

    private static JsonNode LoadSnapshot()
    {
        Assert.SkipWhen(RegistryPath == null, "The tests are not running from inside the repository working tree.");
        return JsonNode.Parse(File.ReadAllText(RegistryPath));
    }

    [Fact]
    public void All_covers_every_node_type_socket_and_enum_entry_of_the_snapshot()
    {
        //Arrange
        JsonArray types = LoadSnapshot()["nodeTypes"].AsArray();

        //Act
        int sockets = NodeType.All.Sum(info => info.Inputs.Count + info.Outputs.Count);
        int enumEntries = NodeType.All.Sum(info => info.Inputs.Concat(info.Outputs).Sum(socket => socket.EnumValues.Count));

        //Assert
        NodeType.All.Count.Should().Be(types.Count);
        sockets.Should().Be(types.Sum(type => type["inputs"].AsArray().Count + type["outputs"].AsArray().Count));
        enumEntries.Should().Be(types.Sum(type => type["inputs"].AsArray().Concat(type["outputs"].AsArray()).Sum(socket => socket["enumValues"].AsArray().Count)));
        NodeType.All.Select(info => info.Name).Should().Equal(types.Select(type => (string)type["name"]));
    }

    [Fact]
    public void All_matches_the_live_native_registry()
    {
        //Arrange
        TestScenes.RequireNativeHost();
        JsonArray live = JsonNode.Parse(CyclesHost.RegistryJson())["nodeTypes"].AsArray();

        //Act
        List<string> differences = [];
        foreach (JsonNode type in live)
        {
            NodeTypeInfo info = NodeType.Find((string)type["name"]);
            if (info == null) { differences.Add("missing " + type["name"]); continue; }
            string[] liveInputs = type["inputs"].AsArray().Select(socket => $"{socket["name"]}:{socket["type"]}:{socket["flags"]}").ToArray();
            string[] catalogued = info.Inputs.Select(socket => $"{socket.Name}:{socket.TypeName}:{(uint)socket.Flags}").ToArray();
            if (!liveInputs.SequenceEqual(catalogued)) differences.Add("inputs of " + info.Name);
            if (type["outputs"].AsArray().Count != info.Outputs.Count) differences.Add("outputs of " + info.Name);
        }

        //Assert
        differences.Should().BeEmpty();
        live.Count.Should().Be(NodeType.All.Count);
    }

    [Fact]
    public void every_generated_class_maps_to_exactly_one_node_type()
    {
        //Act
        Type[] generated = typeof(CyclesNode).Assembly.GetTypes()
            .Where(type => type.IsSubclassOf(typeof(CyclesNode)) && type.Namespace.StartsWith("CodeBrix.Graphics3D.Cycles.Generated", StringComparison.Ordinal))
            .ToArray();

        //Assert
        generated.Length.Should().Be(NodeType.All.Count);
        generated.All(type => NodeType.Of(type) != null && NodeType.Of(type).ClrType == type).Should().BeTrue();
        generated.All(type => (string)type.GetField("NativeTypeName").GetValue(null) == NodeType.Of(type).Name).Should().BeTrue();
    }

    [Fact]
    public void generator_output_is_up_to_date()
    {
        //Arrange
        Assert.SkipWhen(RepositoryPaths.Root == null, "The tests are not running from inside the repository working tree.");
        ProcessStartInfo start = new("python3", "tools/managed/generate_model.py --check")
        {
            WorkingDirectory = RepositoryPaths.Root,
            RedirectStandardOutput = true,
            RedirectStandardError = true
        };
        Process process;
        try
        {
            process = Process.Start(start);
        }
        catch (System.ComponentModel.Win32Exception)
        {
            Assert.Skip("python3 is not available to run the generator check.");
            return;
        }

        //Act
        string output = process.StandardOutput.ReadToEnd() + process.StandardError.ReadToEnd();
        process.WaitForExit();

        //Assert
        process.ExitCode.Should().Be(0, output);
    }

    [Fact]
    public void every_generated_property_round_trips_through_the_native_host()
    {
        //Arrange
        using CyclesScene scene = TestScenes.CreateCpuScene();
        ShaderNode graph = scene.CreateShader();
        HashSet<string> coveredTypes = [];
        List<string> failures = [];
        int properties = 0;

        //Act
        foreach (NodeTypeInfo info in NodeType.All)
        {
            CyclesNode node = Instantiate(scene, graph, info);
            if (node == null) continue;
            foreach (System.Reflection.PropertyInfo property in info.ClrType.GetProperties(BindingFlags.Public | BindingFlags.Instance))
            {
                if (!property.CanWrite || property.DeclaringType.Namespace?.StartsWith("CodeBrix.Graphics3D.Cycles.Generated", StringComparison.Ordinal) != true) continue;
                SocketInfo socket = info.Inputs.FirstOrDefault(candidate => Pascal(candidate.Name) == property.Name || Pascal(candidate.Name) + "Value" == property.Name);
                if (socket == null) failures.Add($"{info.Name}.{property.Name}: no matching socket");
                try
                {
                    object before = property.GetValue(node);
                    property.SetValue(node, before);
                    object after = property.GetValue(node);
                    if (!SameValue(before, after)) failures.Add($"{info.Name}.{property.Name}: value changed");
                    if (socket != null) coveredTypes.Add(socket.TypeName);
                    ++properties;
                }
                catch (TargetInvocationException error)
                {
                    failures.Add($"{info.Name}.{property.Name}: {error.InnerException.GetType().Name}: {error.InnerException.Message}");
                }
            }
        }

        //Assert
        failures.Should().BeEmpty();
        properties.Should().BeGreaterThan(900);
        HashSet<string> typedKinds = NodeType.All.SelectMany(info => info.Inputs)
            .Where(socket => socket.Flags.HasFlag(PropertyFlags.Readable) && !socket.Flags.HasFlag(PropertyFlags.Internal) && socket.Kind != ValueKind.Closure)
            .Select(socket => socket.TypeName).ToHashSet();
        coveredTypes.Should().BeEquivalentTo(typedKinds);
    }

    private static CyclesNode Instantiate(CyclesScene scene, ShaderNode graph, NodeTypeInfo info) => info.Creation switch
    {
        NodeCreation.Singleton => scene.Wrap(scene.Host.SceneNode(info.Name)),
        NodeCreation.Scene => scene.Wrap(scene.Host.CreateNode(info.Name)),
        NodeCreation.GraphOutput => graph.Output,
        NodeCreation.ShaderGraph => (CyclesNode)typeof(ShaderNode).GetMethod(nameof(ShaderNode.Add)).MakeGenericMethod(info.ClrType).Invoke(graph, null),
        _ => null
    };

    // The generator's rule: split on separators, capitalise each part, lower-case all-capital parts.
    private static string Pascal(string name) => string.Concat(name.Split('_', '.').Where(part => part.Length > 0)
        .Select(part => part.Length > 1 && part.All(char.IsUpper) ? part[0] + part[1..].ToLowerInvariant() : char.ToUpperInvariant(part[0]) + part[1..]));

    private static bool SameValue(object before, object after)
    {
        if (before is IEnumerable first && after is IEnumerable second && before is not string)
            return first.Cast<object>().SequenceEqual(second.Cast<object>(), EqualityComparer<object>.Default);
        return Equals(before, after);
    }
}
