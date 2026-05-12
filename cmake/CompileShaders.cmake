# Locates glslangValidator and provides broadstroke_add_shaders() to compile
# GLSL .comp files into SPIR-V at build time.

# Prefer the imported target from find_package(Vulkan) when available.
if(TARGET Vulkan::glslangValidator)
  set(_GLSLANG_TARGET Vulkan::glslangValidator)
else()
  find_program(GLSLANG_VALIDATOR_EXE
    NAMES glslangValidator glslangValidator.exe
    HINTS ENV VULKAN_SDK
    PATH_SUFFIXES Bin Bin32 bin
    REQUIRED
  )
endif()

set(BROADSTROKE_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders"
    CACHE INTERNAL "Directory where compiled .spv files are placed")

file(MAKE_DIRECTORY "${BROADSTROKE_SHADER_OUTPUT_DIR}")

# broadstroke_add_shaders(TARGET <name> SOURCES <file1.comp> <file2.comp> ...)
function(broadstroke_add_shaders)
  cmake_parse_arguments(SHD "" "TARGET" "SOURCES" ${ARGN})
  if(NOT SHD_TARGET)
    message(FATAL_ERROR "broadstroke_add_shaders: TARGET is required")
  endif()

  set(spv_files)
  foreach(src ${SHD_SOURCES})
    get_filename_component(name "${src}" NAME_WE)
    set(out "${BROADSTROKE_SHADER_OUTPUT_DIR}/${name}.spv")

    if(_GLSLANG_TARGET)
      set(glslang_cmd ${_GLSLANG_TARGET})
    else()
      set(glslang_cmd "${GLSLANG_VALIDATOR_EXE}")
    endif()

    add_custom_command(
      OUTPUT "${out}"
      COMMAND ${glslang_cmd} -V "${CMAKE_CURRENT_SOURCE_DIR}/${src}" -o "${out}"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${src}"
      COMMENT "GLSL -> SPIR-V: ${src}"
      VERBATIM
    )
    list(APPEND spv_files "${out}")
  endforeach()

  add_custom_target(${SHD_TARGET} ALL DEPENDS ${spv_files})
endfunction()
