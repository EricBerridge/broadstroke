cmake_minimum_required(VERSION 3.24)

project(broadstroke
  VERSION 2.0.0
  DESCRIPTION "Photo to artwork by physically simulating the artist's strokes"
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4 /permissive- /Zc:__cplusplus /Zc:preprocessor /MP /arch:AVX2 /utf-8)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -mavx2 -mfma)
endif()

# stb_image / stb_image_write (public domain), vendored so the build needs no
# network access. Compiled once, with warnings off.
add_library(stb STATIC third_party/stb/stb_impl.cpp)
target_include_directories(stb SYSTEM PUBLIC third_party/stb)
if(MSVC)
  target_compile_options(stb PRIVATE /w)
else()
  target_compile_options(stb PRIVATE -w)
endif()

add_library(broadstroke_engine STATIC
  src/core/parallel.cpp
  src/core/image.cpp
  src/core/json.cpp
  src/optics/pigment.cpp
  src/optics/mixer.cpp
  src/surface/surface.cpp
  src/canvas/canvas.cpp
  src/motion/hand.cpp
  src/media/bristle_brush.cpp
  src/media/dry_tool.cpp
  src/media/watercolor.cpp
  src/vision/analysis.cpp
  src/vision/scene.cpp
  src/artist/paths.cpp
  src/artist/target.cpp
  src/artist/paint_oil.cpp
  src/artist/paint_dry.cpp
  src/artist/paint_watercolor.cpp
  src/artist/artist.cpp
)
target_include_directories(broadstroke_engine PUBLIC src)
target_link_libraries(broadstroke_engine PUBLIC stb)
find_package(Threads REQUIRED)
target_link_libraries(broadstroke_engine PUBLIC Threads::Threads)

add_executable(broadstroke src/app/main.cpp src/app/studies.cpp)
target_link_libraries(broadstroke PRIVATE broadstroke_engine)
if(WIN32)
  target_sources(broadstroke PRIVATE src/app/gui_win32.cpp)
  target_link_libraries(broadstroke PRIVATE comctl32 comdlg32 gdi32 shell32 user32)
else()
  target_sources(broadstroke PRIVATE src/app/gui_stub.cpp)
endif()

enable_testing()
add_executable(broadstroke_tests
  tests/test_main.cpp
  tests/test_optics.cpp
  tests/test_surface.cpp
  tests/test_motion.cpp
  tests/test_brush.cpp
  tests/test_dry.cpp
  tests/test_watercolor.cpp
  tests/test_artist.cpp
  tests/test_scene.cpp
)
target_link_libraries(broadstroke_tests PRIVATE broadstroke_engine)
add_test(NAME broadstroke_tests COMMAND broadstroke_tests)

# Sampling profiler (Windows only): runs a program and reports where its time
# goes. See the Profiling section of README.md.
if(WIN32)
  add_executable(broadstroke_profiler tools/profiler/sampler.cpp)
  target_compile_definitions(broadstroke_profiler PRIVATE UNICODE _UNICODE)
  target_link_libraries(broadstroke_profiler PRIVATE dbghelp winmm)
endif()
