# Unit tests of the shader_compiler tool's pure library (logic.hpp,
# enumeration.hpp): no process, no file, no toolchain. Links the pure library
# and the engine shader module only.
set(shader_compiler_logic_scenarios
  constant_buffer_classification
  descriptor_array_round_trip
  semantic_split
  vertex_input_predicate
  assemble_reflection_by_slot
  input_hash_items
  generated_include_text
  include_path_normalization
  depfile_text
  file_names
  mach_o_uuid_walk
  enumeration_order)
add_executable(shader_compiler_logic_tests shader_compiler_logic_tests.cpp)
target_link_libraries(shader_compiler_logic_tests PRIVATE bw_test_harness bw_shader_compiler_logic bw_shaders)
foreach(scenario IN LISTS shader_compiler_logic_scenarios)
  add_test(NAME unit.shader_compiler.${scenario} COMMAND shader_compiler_logic_tests ${scenario})
  set_tests_properties(unit.shader_compiler.${scenario} PROPERTIES TIMEOUT 10)
endforeach()
