# Integration scenarios of the shader toolchain: the built shader_compiler as
# a child process, the real DXC and converter loaded from the paths below,
# artifacts in a scratch directory under this build directory. Every path is
# an argument; nothing is discovered from the environment.
set(bw_shader_test_scratch "${CMAKE_CURRENT_BINARY_DIR}/scratch")
set(bw_shader_test_targets d3d12,vulkan)
set(bw_shader_test_converter "none")
if(APPLE)
  set(bw_shader_test_targets d3d12,vulkan,metal)
  set(bw_shader_test_converter "$<TARGET_FILE:bw_metal_irconverter>")
endif()
set(bw_shader_test_arguments
  "$<TARGET_FILE:shader_compiler>"
  "$<TARGET_FILE:bw_dxcompiler>"
  "${bw_shader_test_converter}"
  "${PROJECT_SOURCE_DIR}/src/engine/rendering/shaders/hlsl"
  "${bw_shader_test_scratch}"
  "${bw_shader_test_targets}")

add_executable(shader_toolchain_tests shader_toolchain_tests.cpp)
target_link_libraries(shader_toolchain_tests PRIVATE bw_shader_compiler_toolchain)
add_dependencies(shader_toolchain_tests shader_compiler)
foreach(scenario IN ITEMS fixture_on_three_targets depfile_names_every_include)
  add_test(NAME integration.shader_toolchain.${scenario}
    COMMAND shader_toolchain_tests ${scenario} ${bw_shader_test_arguments})
  set_tests_properties(integration.shader_toolchain.${scenario} PROPERTIES TIMEOUT 300)
endforeach()

if(APPLE)
  # Objective-C++: Metal is an Objective-C API. OBJCXX is enabled by
  # cmake/ThirdParty.cmake on Apple platforms.
  add_executable(metal_library_tests metal_library_tests.mm)
  target_link_libraries(metal_library_tests PRIVATE bw_shader_compiler_logic
    "-framework Foundation" "-framework Metal")
  target_compile_options(metal_library_tests PRIVATE -fobjc-arc)
  set_target_properties(metal_library_tests PROPERTIES
    OBJCXX_CLANG_TIDY "${CMAKE_CXX_CLANG_TIDY}")
  add_dependencies(metal_library_tests shader_compiler)
  add_test(NAME integration.shader_toolchain.metal_library_loads
    COMMAND metal_library_tests metal_library_loads ${bw_shader_test_arguments})
  set_tests_properties(integration.shader_toolchain.metal_library_loads PROPERTIES TIMEOUT 120)
endif()
