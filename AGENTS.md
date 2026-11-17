# Project style guide

Use this guide for future changes to this repository. Match nearby code when a
detail is not covered here. Keep changes focused; avoid unrelated reformatting.

## Formatting

- Use four spaces, no tabs, a 120-column limit, and opening braces on the same line.
- Use braces for conditionals and loops, even for a single statement.
- Write pointers and references next to the type: `World*`, `const Camera&`.
- Preserve include order and groups. In `.cpp` files, include the matching header
  first. Use paths relative to `src`, such as `"world/World.h"`.
- Use `#pragma once` in headers. Separate function definitions with blank lines.

## Naming and C++ conventions

- Use C++23, as configured by CMake.
- Use `PascalCase` for types, functions, and C++ filenames: `ChunkStreamer`,
  `GetPosition`, `TerrainGenerator.cpp`.
- Use `camelCase` for parameters, local variables, and public data fields.
- Use `m_PascalCase` for private member variables: `m_ViewProjectionDirty`.
- Use `UPPER_SNAKE_CASE` for configuration constants in `Options` namespaces.
  Follow existing domain names for other constants.
- Prefer `const` for values that do not change and `constexpr` for compile-time
  values. Initialize fields and aggregates explicitly, commonly with braces.
- Use `[[nodiscard]]` where ignoring a result would lose meaningful information.
- Follow existing ownership patterns: values and standard containers first,
  `std::unique_ptr` for exclusive heap ownership, and references or pointers for
  borrowed access. Match RealRHI resource ownership in rendering code.
- Use comments to explain constraints, units, layout, but mostly reasoning. Prefer clear
  names over comments that repeat the code.

## Profiling

- Use the existing `MC_PROFILE_*` wrappers for profiling.

## Build and verification

- Add new compiled sources to the appropriate target in `CMakeLists.txt`.
- Use out-of-source builds and the existing Windows presets: `windows-dev`,
  `windows-release`, and `windows-profile`. Run from a shell with the MSVC build
  environment and required SDKs available.
- Typical development build: `cmake --preset windows-dev`, then
  `cmake --build --preset windows-dev`.
- Check rendering changes in the running game when possible. Report which checks
  were actually run and any checks that could not be completed.
