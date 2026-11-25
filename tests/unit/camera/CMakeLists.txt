# Unit: rigs and the evaluator (bw/camera), one scenario per invariant.
add_executable(camera_tests camera_tests.cpp)
target_link_libraries(camera_tests PRIVATE bw_test_harness bw_camera)
add_test(NAME unit.camera.stack.table COMMAND camera_tests stack_table)
add_test(NAME unit.camera.load_errors COMMAND camera_tests load_errors)
set_tests_properties(unit.camera.stack.table unit.camera.load_errors PROPERTIES TIMEOUT 10)
