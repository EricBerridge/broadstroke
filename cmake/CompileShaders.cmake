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

function(broadstroke_compile_shader out_var src output_name)
  set(options)
  set(one_value_args LOCAL_SIZE_X LOCAL_SIZE_Y LOCAL_SIZE_Z)
  set(multi_value_args)
  cmake_parse_arguments(CSH "${options}" "${one_value_args}" "${multi_value_args}" ${ARGN})

  set(out "${BROADSTROKE_SHADER_OUTPUT_DIR}/${output_name}.spv")

  if(_GLSLANG_TARGET)
    set(glslang_cmd ${_GLSLANG_TARGET})
  else()
    set(glslang_cmd "${GLSLANG_VALIDATOR_EXE}")
  endif()

  set(defines)
  if(CSH_LOCAL_SIZE_X)
    list(APPEND defines "-DLOCAL_SIZE_X=${CSH_LOCAL_SIZE_X}")
  endif()
  if(CSH_LOCAL_SIZE_Y)
    list(APPEND defines "-DLOCAL_SIZE_Y=${CSH_LOCAL_SIZE_Y}")
  endif()
  if(CSH_LOCAL_SIZE_Z)
    list(APPEND defines "-DLOCAL_SIZE_Z=${CSH_LOCAL_SIZE_Z}")
  endif()

  add_custom_command(
    OUTPUT "${out}"
    COMMAND ${glslang_cmd} -V ${defines} "${CMAKE_CURRENT_SOURCE_DIR}/${src}" -o "${out}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/${src}"
    COMMENT "GLSL -> SPIR-V: ${output_name}.spv"
    VERBATIM
  )

  set(${out_var} "${out}" PARENT_SCOPE)
endfunction()

# broadstroke_add_shaders(TARGET <name> SOURCES <file1.comp> <file2.comp> ...)
function(broadstroke_add_shaders)
  cmake_parse_arguments(SHD "" "TARGET" "SOURCES" ${ARGN})
  if(NOT SHD_TARGET)
    message(FATAL_ERROR "broadstroke_add_shaders: TARGET is required")
  endif()

  set(spv_files)
  foreach(src ${SHD_SOURCES})
    get_filename_component(name "${src}" NAME_WE)
    broadstroke_compile_shader(out "${src}" "${name}")
    list(APPEND spv_files "${out}")
  endforeach()

  add_custom_target(${SHD_TARGET} ALL DEPENDS ${spv_files})
endfunction()

# broadstroke_add_shader_variants(TARGET <name> LOCAL_SIZES_2D 8x8 16x16 SOURCES ...)
function(broadstroke_add_shader_variants)
  cmake_parse_arguments(SHV "" "TARGET" "SOURCES;LOCAL_SIZES_2D" ${ARGN})
  if(NOT SHV_TARGET)
    message(FATAL_ERROR "broadstroke_add_shader_variants: TARGET is required")
  endif()

  set(spv_files)
  foreach(src ${SHV_SOURCES})
    get_filename_component(name "${src}" NAME_WE)
    foreach(size ${SHV_LOCAL_SIZES_2D})
      string(REPLACE "x" ";" parts "${size}")
      list(LENGTH parts part_count)
      if(NOT part_count EQUAL 2)
        message(FATAL_ERROR "local size '${size}' must be WIDTHxHEIGHT")
      endif()
      list(GET parts 0 sx)
      list(GET parts 1 sy)
      broadstroke_compile_shader(out "${src}" "${name}_ls${sx}x${sy}"
        LOCAL_SIZE_X "${sx}" LOCAL_SIZE_Y "${sy}")
      list(APPEND spv_files "${out}")
    endforeach()
  endforeach()

  add_custom_target(${SHV_TARGET} ALL DEPENDS ${spv_files})
endfunction()
