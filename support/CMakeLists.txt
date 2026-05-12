# Support model: data structures, procedural generators, and JSON preset
# loader for the painting substrate (cold-press watercolor paper, canvas,
# smooth bristol, etc.). Pure CPU code — the GPU only sees the field
# uploaded as Buffers in the compute layer.

add_library(broadstroke_support STATIC
  src/preset.cpp
  src/generate.cpp
)

target_include_directories(broadstroke_support
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_BINARY_DIR}/generated"
)

target_link_libraries(broadstroke_support
  PUBLIC
    nlohmann_json::nlohmann_json
)

add_library(broadstroke::support ALIAS broadstroke_support)
