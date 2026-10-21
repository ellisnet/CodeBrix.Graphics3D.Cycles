// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0

using System;
using System.IO;
using System.Runtime.InteropServices;
using CodeBrix.Graphics3D.Cycles;

NativeLibrary.SetDllImportResolver(typeof(CyclesHost).Assembly, (name, assembly, path) =>
    name == "codebrix_cycles_host" ? NativeLibrary.Load(args[0]) : IntPtr.Zero);
File.WriteAllText(args[1], CyclesHost.RegistryJson());
Console.WriteLine("ok");
