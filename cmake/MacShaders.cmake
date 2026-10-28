# Native shaders remain independent source assets. Compile and link them at
# build time; the Mac host loads bullet_witch.metallib beside its executable.
find_program(BW_XCRUN xcrun REQUIRED)
execute_process(COMMAND "${BW_XCRUN}" --find metal
  RESULT_VARIABLE bw_metal_status OUTPUT_QUIET ERROR_QUIET)
if(NOT bw_metal_status EQUAL 0)
  message(FATAL_ERROR "Metal compiler unavailable. Install the Metal toolchain with: xcodebuild -downloadComponent MetalToolchain")
endif()
set(bw_shader_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders/$<CONFIG>")
set(bw_movie_air "${bw_shader_dir}/movie.air")
set(bw_sprite_air "${bw_shader_dir}/sprites.air")
set(bw_world_depth_air "${bw_shader_dir}/world_depth.air")
set(bw_world_color_air "${bw_shader_dir}/world_color.air")
set(bw_world_output_air "${bw_shader_dir}/world_output.air")
set(bw_world_effects_air "${bw_shader_dir}/world_effects.air")
set(bw_metal_library "${bw_shader_dir}/bullet_witch.metallib")
add_custom_command(OUTPUT "${bw_movie_air}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${bw_shader_dir}"
  COMMAND "${BW_XCRUN}" -sdk macosx metal -Wall -Wextra -Werror
    -c "${PROJECT_SOURCE_DIR}/src/platform/macos/render/shaders/movie.metal" -o "${bw_movie_air}"
  DEPENDS "${PROJECT_SOURCE_DIR}/src/platform/macos/render/shaders/movie.metal"
  COMMENT "Compiling native movie shader" VERBATIM)
add_custom_command(OUTPUT "${bw_sprite_air}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${bw_shader_dir}"
  COMMAND "${BW_XCRUN}" -sdk macosx metal -Wall -Wextra -Werror
    -c "${PROJECT_SOURCE_DIR}/src/platform/macos/render/shaders/sprites.metal" -o "${bw_sprite_air}"
  DEPENDS "${PROJECT_SOURCE_DIR}/src/platform/macos/render/shaders/sprites.metal"
  COMMENT "Compiling native sprite shader" VERBATIM)
add_custom_command(OUTPUT "${bw_world_depth_air}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${bw_shader_dir}"
  COMMAND "${BW_XCRUN}" -sdk macosx metal -Wall -Wextra -Werror
    -c "${PROJECT_SOURCE_DIR}/src/platform/macos/render/shaders/world_depth.metal" -o "${bw_world_depth_air}"
  DEPENDS "${PROJECT_SOURCE_DIR}/src/platform/macos/render/shaders/world_depth.metal"
  COMMENT "Compiling original static depth shader equations" VERBATIM)
add_custom_command(OUTPUT "${bw_world_color_air}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${bw_shader_dir}"
  COMMAND "${BW_XCRUN}" -sdk macosx metal -Wall -Wextra -Werror
    -c "${PROJECT_SOURCE_DIR}/src/platform/macos/render/shaders/world_color.metal" -o "${bw_world_color_air}"
  DEPENDS "${PROJECT_SOURCE_DIR}/src/platform/macos/render/shaders/world_color.metal"
  COMMENT "Compiling original static surface-family shader equations" VERBATIM)
add_custom_command(OUTPUT "${bw_world_output_air}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${bw_shader_dir}"
  COMMAND "${BW_XCRUN}" -sdk macosx metal -Wall -Wextra -Werror
    -c "${PROJECT_SOURCE_DIR}/src/platform/macos/render/shaders/world_output.metal" -o "${bw_world_output_air}"
  DEPENDS "${PROJECT_SOURCE_DIR}/src/platform/macos/render/shaders/world_output.metal"
  COMMENT "Compiling original world display-output equations" VERBATIM)
add_custom_command(OUTPUT "${bw_world_effects_air}"
  COMMAND "${CMAKE_COMMAND}" -E make_directory "${bw_shader_dir}"
  COMMAND "${BW_XCRUN}" -sdk macosx metal -Wall -Wextra -Werror
    -c "${PROJECT_SOURCE_DIR}/src/platform/macos/render/shaders/world_effects.metal" -o "${bw_world_effects_air}"
  DEPENDS "${PROJECT_SOURCE_DIR}/src/platform/macos/render/shaders/world_effects.metal"
  COMMENT "Compiling original effect sprite shader equations" VERBATIM)
add_custom_command(OUTPUT "${bw_metal_library}"
  COMMAND "${BW_XCRUN}" -sdk macosx metallib "${bw_movie_air}" "${bw_sprite_air}" "${bw_world_depth_air}" "${bw_world_color_air}" "${bw_world_output_air}" "${bw_world_effects_air}" -o "${bw_metal_library}"
  DEPENDS "${bw_movie_air}" "${bw_sprite_air}" "${bw_world_depth_air}" "${bw_world_color_air}" "${bw_world_output_air}" "${bw_world_effects_air}"
  COMMENT "Linking native Metal shader library" VERBATIM)
add_custom_target(bw_metal_shaders
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
    "${bw_metal_library}" "$<TARGET_FILE_DIR:bullet-witch>/bullet_witch.metallib"
  DEPENDS "${bw_metal_library}" VERBATIM)
add_dependencies(bullet-witch bw_metal_shaders)
