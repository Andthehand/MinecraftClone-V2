cmake_minimum_required(VERSION 3.25)

project(MinecraftClone VERSION 0.1.0 LANGUAGES CXX)

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

add_subdirectory(external/RealRHI)

include(FetchContent)
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "Build the GLM library")
set(GLM_BUILD_TESTS OFF CACHE BOOL "Build GLM tests")
set(GLM_BUILD_INSTALL OFF CACHE BOOL "Generate GLM install rules")
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 8d1fd52e5ab5590e2c81768ace50c72bae28f2ed # 1.0.3
)
FetchContent_MakeAvailable(glm)

set(FASTNOISE2_TOOLS OFF CACHE BOOL "Build FastNoise2 tools")
set(FASTNOISE2_TESTS OFF CACHE BOOL "Build FastNoise2 tests")
set(FASTNOISE2_STRICT_FP ON CACHE BOOL "Keep terrain consistent across SIMD implementations")
FetchContent_Declare(FastNoise2
    GIT_REPOSITORY https://github.com/Auburn/FastNoise2.git
    GIT_TAG 903c1f2d2f9d53ddce94cd223f32727d9ab3aeaa # v1.1.1
)
FetchContent_MakeAvailable(FastNoise2)

add_library(TerrainGenerator STATIC src/TerrainGenerator.cpp src/Octree.cpp)
target_include_directories(TerrainGenerator PUBLIC src)
target_compile_features(TerrainGenerator PUBLIC cxx_std_23)
target_link_libraries(TerrainGenerator PUBLIC glm::glm FastNoise)

add_executable(MinecraftClone
    src/main.cpp
    src/Camera.h
    src/Camera.cpp
    src/ChunkMesh.h
    src/ChunkMesh.cpp
    src/Octree.h
    src/Options.h
    src/Renderer.h
    src/Renderer.cpp
    src/Sun.h
    src/Sun.cpp
    src/VoxelShadowVolume.h
    src/VoxelShadowVolume.cpp
    src/VoxelTextures.h
    src/World.h
    src/World.cpp
)
target_compile_features(MinecraftClone PRIVATE cxx_std_23)
set_target_properties(MinecraftClone PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(MinecraftClone PRIVATE RealRHI TerrainGenerator)

add_custom_target(MinecraftCloneShaders
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/src/shaders"
            "$<TARGET_FILE_DIR:MinecraftClone>/shaders"
        COMMENT "Copying MinecraftClone shaders"
    )
add_dependencies(MinecraftClone MinecraftCloneShaders)

if(MSVC)
    target_compile_options(MinecraftClone PRIVATE /W4 /permissive-)
else()
    target_compile_options(MinecraftClone PRIVATE -Wall -Wextra -Wpedantic)
endif()
