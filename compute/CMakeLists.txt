add_subdirectory(kernels)

add_library(broadstroke_compute STATIC
  bindings/src/context.cpp
)

target_include_directories(broadstroke_compute
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/bindings/include"
    "${CMAKE_BINARY_DIR}/generated"
)

target_link_libraries(broadstroke_compute
  PUBLIC
    kompute
  PRIVATE
    Vulkan::Vulkan
)

# Kompute v0.9.0 uses vk::DispatchLoaderDynamic only in its debug-layer path,
# which requires VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL in SDK 1.3.283+. Mirror
# the KOMPUTE_OPT_DISABLE_VK_DEBUG_LAYERS flag so context.cpp sees the same
# define that Kompute's own translation units see.
target_compile_definitions(broadstroke_compute PRIVATE KOMPUTE_DISABLE_VK_DEBUG_LAYERS=1)

# Ensure SPIR-V is built before any consumer tries to run.
add_dependencies(broadstroke_compute broadstroke_shaders)

add_library(broadstroke::compute ALIAS broadstroke_compute)
