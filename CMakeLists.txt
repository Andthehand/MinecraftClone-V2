cmake_minimum_required(VERSION 3.25)

project(MinecraftClone VERSION 0.1.0 LANGUAGES CXX)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use a separate build directory (for example, a CMake preset).")
endif()
include(FetchContent)

option(MINECRAFT_ENABLE_TRACY "Profile MinecraftClone and RealRHI with Tracy" OFF)
option(MINECRAFT_ENABLE_IMGUI "Include the ImGui debug interface" ON)

# Create one client before RealRHI so game and RHI zones share a capture.
if(MINECRAFT_ENABLE_TRACY)
    set(TRACY_ENABLE ON CACHE BOOL "Enable Tracy instrumentation" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "Collect only while a profiler is connected")

    FetchContent_Declare(tracy
        GIT_REPOSITORY https://github.com/wolfpld/tracy.git
        GIT_TAG v0.15.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(tracy)
endif()
set(REALRHI_ENABLE_TRACY ${MINECRAFT_ENABLE_TRACY})

add_library(MinecraftProfiling INTERFACE)
if(MINECRAFT_ENABLE_TRACY)
    target_compile_definitions(MinecraftProfiling INTERFACE MINECRAFT_ENABLE_TRACY=1)
    target_link_libraries(MinecraftProfiling INTERFACE Tracy::TracyClient)
endif()

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/RealRHI/CMakeLists.txt")
    message(FATAL_ERROR "RealRHI is missing. Run: git submodule update --init --recursive")
endif()

# Build only the library when embedding RealRHI in the game.
set(REALRHI_BUILD_VULKAN ON CACHE BOOL "Build the RealRHI Vulkan backend")
set(BUILD_EXAMPLES OFF CACHE BOOL "Build RealRHI examples")
set(REALRHI_BUILD_TESTS OFF CACHE BOOL "Build RealRHI tests")
set(REALRHI_BUILD_GPU_TESTS OFF CACHE BOOL "Build RealRHI GPU tests")
set(REALRHI_BUILD_BENCHMARKS OFF CACHE BOOL "Build RealRHI benchmarks")

if(NOT REALRHI_BUILD_VULKAN)
    message(FATAL_ERROR "MinecraftClone requires REALRHI_BUILD_VULKAN=ON.")
endif()

set(REALRHI_BUILD_IMGUI ${MINECRAFT_ENABLE_IMGUI})
add_subdirectory(external/RealRHI)

set(GLM_BUILD_LIBRARY OFF CACHE BOOL "Build the GLM library")
set(GLM_BUILD_TESTS OFF CACHE BOOL "Build GLM tests")
set(GLM_BUILD_INSTALL OFF CACHE BOOL "Generate GLM install rules")
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 8d1fd52e5ab5590e2c81768ace50c72bae28f2ed # 1.0.3
)
FetchContent_MakeAvailable(glm)

FetchContent_Declare(EnTT
    GIT_REPOSITORY https://github.com/skypjack/entt.git
    GIT_TAG d4014c74dc3793aba95ae354d6e23a026c2796db # v3.15.0
)
FetchContent_MakeAvailable(EnTT)

set(FASTNOISE2_TOOLS OFF CACHE BOOL "Build FastNoise2 tools")
set(FASTNOISE2_TESTS OFF CACHE BOOL "Build FastNoise2 tests")
set(FASTNOISE2_STRICT_FP ON CACHE BOOL "Keep terrain consistent across SIMD implementations")
FetchContent_Declare(FastNoise2
    GIT_REPOSITORY https://github.com/Auburn/FastNoise2.git
    GIT_TAG 903c1f2d2f9d53ddce94cd223f32727d9ab3aeaa # v1.1.1
)
FetchContent_MakeAvailable(FastNoise2)

set(JSON_BuildTests OFF CACHE INTERNAL "Disable nlohmann/json tests")
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.12.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(stb
    GIT_REPOSITORY https://github.com/nothings/stb.git
    GIT_TAG f0569113c93ad095470c54bf34a17b36646bbbb5
)
FetchContent_MakeAvailable(stb)

add_library(MinecraftBlocks STATIC src/world/BlockRegistry.cpp)
target_include_directories(MinecraftBlocks PUBLIC src)
target_compile_features(MinecraftBlocks PUBLIC cxx_std_23)
target_link_libraries(MinecraftBlocks PRIVATE nlohmann_json::nlohmann_json)
target_compile_definitions(MinecraftBlocks PRIVATE JSON_NOEXCEPTION=1)

add_library(MinecraftVoxelTextures STATIC src/rendering/VoxelTextures.cpp)
target_include_directories(MinecraftVoxelTextures SYSTEM PRIVATE ${stb_SOURCE_DIR})
target_link_libraries(MinecraftVoxelTextures PUBLIC MinecraftBlocks)
set_target_properties(MinecraftBlocks MinecraftVoxelTextures PROPERTIES CXX_EXTENSIONS OFF)

# CPU world and meshing code has no RealRHI or SDL dependency.
add_library(MinecraftWorld STATIC
    src/world/Octree.cpp
    src/world/TerrainGenerator.cpp
    src/world/World.cpp
    src/world/ChunkStreamer.cpp
    src/navigation/Pathfinder.cpp
    src/navigation/PathfindingTypes.cpp
    src/navigation/MovementRules.cpp
    src/navigation/HierarchicalPathfinder.cpp
    src/navigation/RegionGraph.cpp
    src/navigation/NavigationCache.cpp
    src/meshing/ChunkMesher.cpp
)
target_include_directories(MinecraftWorld PUBLIC src)
target_compile_features(MinecraftWorld PUBLIC cxx_std_23)
set_target_properties(MinecraftWorld PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(MinecraftWorld PUBLIC glm::glm FastNoise MinecraftBlocks PRIVATE MinecraftProfiling)

# Gameplay is CPU-only; input collection and camera synchronization live in the app.
add_library(MinecraftGame STATIC
    src/game/GameScene.cpp
)
target_include_directories(MinecraftGame PUBLIC src)
target_compile_features(MinecraftGame PUBLIC cxx_std_23)
set_target_properties(MinecraftGame PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(MinecraftGame PUBLIC MinecraftWorld EnTT::EnTT PRIVATE MinecraftProfiling)

add_executable(MinecraftClone
    src/main.cpp
    src/app/Application.cpp
    src/diagnostics/DebugUI.cpp
    src/diagnostics/FrameCounter.cpp
    src/rendering/Camera.cpp
    src/rendering/ChunkRenderCache.cpp
    src/rendering/Renderer.cpp
    src/rendering/TextRenderer.cpp
    src/rendering/TextBatch.cpp
    src/rendering/Sun.cpp
    src/rendering/Visibility.cpp
    src/rendering/passes/LightingPass.cpp
    src/rendering/passes/ShadowPass.cpp
    src/rendering/passes/VoxelPass.cpp
)
target_compile_features(MinecraftClone PRIVATE cxx_std_23)
set_target_properties(MinecraftClone PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(MinecraftClone PRIVATE RealRHI MinecraftGame MinecraftProfiling MinecraftVoxelTextures)
target_compile_definitions(MinecraftClone PRIVATE MINECRAFT_ENABLE_IMGUI=$<BOOL:${MINECRAFT_ENABLE_IMGUI}>)
if(MINECRAFT_ENABLE_IMGUI)
    target_link_libraries(MinecraftClone PRIVATE RealRHIImGui)
    realrhi_copy_imgui_shader(MinecraftClone)
endif()

add_custom_target(MinecraftCloneShaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders"
        "$<TARGET_FILE_DIR:MinecraftClone>/shaders"
    COMMENT "Copying MinecraftClone shaders"
)
add_dependencies(MinecraftClone MinecraftCloneShaders)

# Run on every build so edits and removed block files reach the executable without relinking.
add_custom_target(MinecraftCloneAssets
    COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_FILE_DIR:MinecraftClone>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/assets"
        "$<TARGET_FILE_DIR:MinecraftClone>/assets"
    COMMENT "Copying MinecraftClone block assets"
)
add_dependencies(MinecraftClone MinecraftCloneAssets)

option(MINECRAFT_BUILD_TESTS "Build CPU block registry and meshing tests" OFF)
if(MINECRAFT_BUILD_TESTS)
    enable_testing()
    add_executable(MinecraftBlockTests tests/BlockRegistryTests.cpp)
    target_link_libraries(MinecraftBlockTests PRIVATE MinecraftWorld MinecraftVoxelTextures nlohmann_json::nlohmann_json)
    target_compile_definitions(MinecraftBlockTests PRIVATE JSON_NOEXCEPTION=1)
    add_test(NAME MinecraftBlockTests COMMAND MinecraftBlockTests "${CMAKE_CURRENT_SOURCE_DIR}/assets")
endif()
if(NOT MINECRAFT_ENABLE_IMGUI)
    # Remove the owned shader if this build directory previously enabled ImGui.
    add_custom_command(TARGET MinecraftCloneShaders POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E rm -f "$<TARGET_FILE_DIR:MinecraftClone>/shaders/realrhi_imgui.slang")
endif()

foreach(target MinecraftBlocks MinecraftWorld MinecraftClone)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endforeach()
