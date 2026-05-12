# Stroke planner (Stage 6). Pure data: takes an analyzer::Analysis and a
# pigment palette, returns a RenderPlan (a list of LayerSpecs and Steps)
# the host can replay onto a Canvas. No GPU dependency, no Canvas
# dependency — apply_plan() lives host-side.

add_library(broadstroke_planner STATIC
  src/plan.cpp
)

target_include_directories(broadstroke_planner
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(broadstroke_planner
  PUBLIC
    broadstroke::analyzer
    broadstroke::tools
)

add_library(broadstroke::planner ALIAS broadstroke_planner)
