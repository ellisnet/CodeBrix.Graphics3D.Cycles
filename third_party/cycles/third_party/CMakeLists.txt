# Copyright (c) 2026 Jeremy Ellis and contributors
# SPDX-License-Identifier: Apache-2.0
add_subdirectory(sky)
if(UNIX AND NOT APPLE)
  add_subdirectory(libc_compat)
endif()
