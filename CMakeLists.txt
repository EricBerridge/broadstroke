cmake_minimum_required(VERSION 3.24)

project(broadstroke
  VERSION 0.1.0
  DESCRIPTION "Physical simulation painting"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /Zc:__cplusplus /Zc:preprocessor)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Vulkan provides the runtime and (usually) glslangValidator.
find_package(Vulkan REQUIRED)

include(CompileShaders)

# Fetch Kompute (Vulkan compute wrapper).
# Pin to a release tag; bump explicitly once verified against a newer SDK.
include(FetchContent)
set(KOMPUTE_OPT_INSTALL OFF CACHE BOOL "" FORCE)
set(KOMPUTE_OPT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(KOMPUTE_OPT_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(KOMPUTE_OPT_BUILD_SHADERS OFF CACHE BOOL "" FORCE)
set(KOMPUTE_OPT_USE_SPDLOG OFF CACHE BOOL "" FORCE)
set(KOMPUTE_OPT_LOG_LEVEL Warn CACHE STRING "" FORCE)
# vk::DispatchLoaderDynamic is behind VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL in
# SDK 1.3.283+. Kompute v0.9.0 uses it only for debug report callbacks; disable
# the whole debug-layer path rather than enabling the dynamic loader globally.
set(KOMPUTE_OPT_DISABLE_VK_DEBUG_LAYERS ON CACHE BOOL "" FORCE)

FetchContent_Declare(
  kompute
  GIT_REPOSITORY https://github.com/KomputeProject/kompute.git
  GIT_TAG v0.9.0
  GIT_SHALLOW TRUE
  SYSTEM  # marks Kompute headers as system headers, suppressing warnings from them
)
FetchContent_MakeAvailable(kompute)

# nlohmann/json — header-only, used for preset loading.
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install   OFF CACHE INTERNAL "")
FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_SHALLOW TRUE
  SYSTEM
)
FetchContent_MakeAvailable(json)

# stb (single-file header library set). Used by the analyzer to decode JPG/
# PNG/BMP source photographs into a float RGBA buffer. The repo has no
# CMake build of its own, so we just expose the headers via an INTERFACE
# target that depending modules link.
FetchContent_Declare(
  stb
  GIT_REPOSITORY https://github.com/nothings/stb.git
  GIT_TAG f0569113c93ad095470c54bf34a17b36646bbbb5  # 2024-01-29 master snapshot
  GIT_SHALLOW FALSE
  SYSTEM
)
FetchContent_MakeAvailable(stb)
add_library(stb_image INTERFACE)
target_include_directories(stb_image SYSTEM INTERFACE "${stb_SOURCE_DIR}")

# Generated config header so host code can locate shaders at runtime.
configure_file(
  "${CMAKE_SOURCE_DIR}/cmake/build_config.hpp.in"
  "${CMAKE_BINARY_DIR}/generated/broadstroke/build_config.hpp"
  @ONLY
)

enable_testing()

add_subdirectory(compute)
add_subdirectory(reference)
add_subdirectory(support)
add_subdirectory(tools)
add_subdirectory(analyzer)
add_subdirectory(planner)
add_subdirectory(host)
add_subdirectory(tests)
