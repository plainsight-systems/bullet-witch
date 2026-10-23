# Native shaders remain independent source assets. Compile and link them at
# build time; the Mac host loads movie.metallib beside its executable.
find_program(BW_XCRUN xcrun REQUIRED)
execute_process(COMMAND "${BW_XCRUN}" --find metal
  RESULT_VARIABLE bw_metal_status OUTPUT_QUIET ERROR_QUIET)
if(NOT bw_metal_status EQUAL 0)
  message(FATAL_ERROR "Metal compiler unavailable. Install the Metal toolchain with: xcodebuild -downloadComponent MetalToolchain")
endif()
set(bw_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders/$<CONFIG>")
set(bw_movie_air "${bw_shader_dir}/movie.air")
set(bw_movie_library "${bw_shader_dir}/movie.metallib")
add_custom_command(OUTPUT "${bw_movie_air}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${bw_shader_dir}"
  COMMAND "${BW_XCRUN}" -sdk macosx metal -Wall -Wextra -Werror
    -c "${PROJECT_SOURCE_DIR}/src/platform/macos/shaders/movie.metal" -o "${bw_movie_air}"
  DEPENDS "${PROJECT_SOURCE_DIR}/src/platform/macos/shaders/movie.metal"
  COMMENT "Compiling native movie shader" VERBATIM)
add_custom_command(OUTPUT "${bw_movie_library}"
  COMMAND "${BW_XCRUN}" -sdk macosx metallib "${bw_movie_air}" -o "${bw_movie_library}"
  DEPENDS "${bw_movie_air}"
  COMMENT "Linking native movie shader library" VERBATIM)
add_custom_target(bw_movie_shaders
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${bw_movie_library}" "$<TARGET_FILE_DIR:bw-startup>/movie.metallib"
  DEPENDS "${bw_movie_library}" VERBATIM)
add_dependencies(bw-startup bw_movie_shaders)
