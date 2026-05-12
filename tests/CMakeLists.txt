function(broadstroke_add_physics_test name)
  add_executable(${name} physics/${name}.cpp)
  target_link_libraries(${name}
    PRIVATE
      broadstroke::compute
      broadstroke::reference
      broadstroke::support
      broadstroke::tools
      broadstroke::analyzer
      broadstroke::planner
  )
  set_target_properties(${name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
  )
  add_test(NAME physics.${name} COMMAND ${name})
endfunction()

broadstroke_add_physics_test(test_hello_double)
broadstroke_add_physics_test(test_support_visualize)
broadstroke_add_physics_test(test_compose)
broadstroke_add_physics_test(test_pencil_stroke)
broadstroke_add_physics_test(test_watercolor_stroke)
broadstroke_add_physics_test(test_wetness_diffuse)
broadstroke_add_physics_test(test_pastel_stroke)
broadstroke_add_physics_test(test_pastel_smear)
broadstroke_add_physics_test(test_analyzer)
broadstroke_add_physics_test(test_planner)
broadstroke_add_physics_test(test_planner_styles)
