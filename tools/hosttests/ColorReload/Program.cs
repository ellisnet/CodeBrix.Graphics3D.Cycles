// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
using System.Reflection;
using System.Text.Json;
if (args.Length != 3) throw new Exception("Usage: Reload CONSUMER_DLL REFERENCES OUTPUT");
MethodInfo entry = Assembly.LoadFrom(Path.GetFullPath(args[0])).EntryPoint ?? throw new Exception("Missing consumer entry point");
for (int round = 0; round < 6; ++round)
{
    string variant = ((char)('a' + round % 3)).ToString();
    string[] inputs = [Path.Combine(args[1], variant, "output"), Path.Combine(args[2], $"round.{round}")];
    try { entry.Invoke(null, [inputs]); }
    catch (TargetInvocationException error) { throw error.InnerException ?? error; }
}
// 260 films per round, or 256 where the native host reads no image files (osx-arm64).
int films = Directory.GetFiles(Path.Combine(args[2], "round.0"), "case.*.rgba.bin").Length * 6;
Console.WriteLine(JsonSerializer.Serialize(new { passed = true, sameManagedProcess = true, configurationRounds = 6, films }));
