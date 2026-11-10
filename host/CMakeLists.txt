add_executable(broadstroke
  main.cpp
  canvas.cpp
  apply_plan.cpp
  ui_win32.cpp
  io/bmp_writer.cpp
  io/image_writer.cpp
)

target_include_directories(broadstroke
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_link_libraries(broadstroke
  PRIVATE
    broadstroke::compute
    broadstroke::reference
    broadstroke::support
    broadstroke::tools
    broadstroke::analyzer
    broadstroke::planner
    stb_image
)

set_target_properties(broadstroke PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
