# CPU reference implementations of every GPU kernel — the source of truth
# for kernel validation (tests/physics) and a fallback when the GPU path
# is unavailable or being debugged.

add_library(broadstroke_reference STATIC
  src/hello_double.cpp
  src/support_visualize.cpp
  src/pencil_stroke.cpp
  src/watercolor_stroke.cpp
  src/wetness_diffuse.cpp
  src/pastel_stroke.cpp
  src/pastel_smear.cpp
  src/compose.cpp
)

target_include_directories(broadstroke_reference PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(broadstroke::reference ALIAS broadstroke_reference)
