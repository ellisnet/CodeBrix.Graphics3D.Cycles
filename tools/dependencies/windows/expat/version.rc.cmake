// Copyright (c) 2026 Jeremy Ellis and contributors
// SPDX-License-Identifier: Apache-2.0
//
// Independent replacement for expat's excluded win32/version.rc.cmake (outside
// the reviewed source profile). expat's CMake configures that template into a
// resource script for its DLL; upstream it only carries version information. The
// Windows dependency recipe places this file at that path in its work copy of the
// snapshot (the snapshot is unchanged). It defines no resources: the DLL's
// behaviour does not depend on them.
