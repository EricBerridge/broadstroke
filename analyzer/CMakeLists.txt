# Image analysis (Stage 5). CPU-only by design — produces the typed
# field arrays the planner consumes; no dependency on compute/.

add_library(broadstroke_analyzer STATIC
  src/load.cpp
  src/luminance.cpp
  src/edges.cpp
  src/flow.cpp
  src/analyze.cpp
)

target_include_directories(broadstroke_analyzer
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(broadstroke_analyzer
  PRIVATE
    stb_image
)

add_library(broadstroke::analyzer ALIAS broadstroke_analyzer)
