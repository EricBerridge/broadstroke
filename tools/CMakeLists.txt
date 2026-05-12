# Tools + media. The "tool" axis (pencil, brush, pastel) is independent
# from the "medium" axis (graphite is bound to pencil; pigments are loaded
# into brushes). Keep one library so consumers don't need a parallel set
# of target_link_libraries lines for what is conceptually one subsystem.

add_library(broadstroke_tools STATIC
  src/pencil.cpp
  src/brush.cpp
  src/pigment.cpp
  src/pastel.cpp
  src/stroke.cpp
)

target_include_directories(broadstroke_tools
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_BINARY_DIR}/generated"
)

target_link_libraries(broadstroke_tools
  PUBLIC
    nlohmann_json::nlohmann_json
)

add_library(broadstroke::tools ALIAS broadstroke_tools)
