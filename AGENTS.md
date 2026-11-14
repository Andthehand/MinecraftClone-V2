# Project style guide

Use this guide for future changes to this repository. Match nearby code when a
detail is not covered here. Keep changes focused; avoid unrelated reformatting.

## Formatting

- Treat the root `.clang-format` as the source of truth for C++ formatting.
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
  Follow existing domain names for other constants, such as `Blocks::Air`.
- Prefer `const` for values that do not change and `constexpr` for compile-time
  values. Initialize fields and aggregates explicitly, commonly with braces.
- Use `[[nodiscard]]` where ignoring a result would lose meaningful information.
- Follow existing ownership patterns: values and standard containers first,
  `std::unique_ptr` for exclusive heap ownership, and references or pointers for
  borrowed access. Match RealRHI resource ownership in rendering code.
- Keep implementation-only helpers in an anonymous namespace in the `.cpp` file.
- Follow existing `std::expected` and `std::optional` patterns for recoverable
  failures and missing values. Include useful context in errors, such as filenames.
- Use comments to explain constraints, units, layout, or reasoning. Prefer clear
  names over comments that repeat the code.

## Project structure

- `src/app`: application lifecycle, input collection, and camera synchronization.
- `src/game`: CPU-only gameplay and EnTT components.
- `src/world`, `src/navigation`, `src/meshing`: CPU world data, pathfinding, and mesh
  generation. Keep these independent of RealRHI and SDL.
- `src/rendering`: camera and GPU rendering; individual passes live in `passes/`.
- `src/shaders`: Slang shaders, using lowercase filenames with underscores.
- `src/config/Settings.h`: shared compile-time settings, grouped under `Options`.
- `src/diagnostics`: debug UI, frame statistics, and profiling wrappers.
- `assets`: data-driven block and texture definitions; follow `docs/blocks.md`
  when changing their format or adding content.
- `external/RealRHI`: a separate dependency. Keep game changes in this project
  unless a change to that dependency is specifically needed.

## Rendering and data contracts

- Keep CPU upload structures and corresponding Slang structures synchronized,
  including field order, matrix convention, offsets, and alignment. Maintain the
  relevant `static_assert` checks in `src/rendering/ShaderData.h`.
- Preserve the existing right-handed camera and Vulkan zero-to-one depth
  conventions when changing projection or visibility code.
- Use the existing `MC_PROFILE_*` wrappers for profiling.

## Build and verification

- Add new compiled sources to the appropriate target in `CMakeLists.txt`.
- Use out-of-source builds and the existing Windows presets: `windows-dev`,
  `windows-release`, and `windows-profile`. Run from a shell with the MSVC build
  environment and required SDKs available.
- Typical development build: `cmake --preset windows-dev`, then
  `cmake --build --preset windows-dev`.
- For relevant CPU behavior changes, enable tests with
  `cmake --preset windows-dev -DMINECRAFT_BUILD_TESTS=ON`, build with
  `cmake --build out/build/windows-dev --target MinecraftBlockTests`, and run
  `ctest --test-dir out/build/windows-dev --output-on-failure`.
- Extend `tests/BlockRegistryTests.cpp` for relevant registry, texture, or meshing
  regressions, following its existing `Require` helpers and fixtures.
- Check rendering changes in the running game when possible. Report which checks
  were actually run and any checks that could not be completed.
- Documentation-only changes do not require compiling the game.
