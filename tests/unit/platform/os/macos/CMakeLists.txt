# Unit: the macOS pad's difference of two profile readings
# (bw/platform/os/macos/pad.hpp, contract 3), one table-driven scenario.
# Links the pure library alone, so no OS framework.
add_executable(pad_samples_tests pad_samples_tests.cpp)
target_link_libraries(pad_samples_tests PRIVATE bw_test_harness bw_pad_samples)
add_test(NAME unit.platform.macos.pad_samples COMMAND pad_samples_tests pad_samples)
set_tests_properties(unit.platform.macos.pad_samples PROPERTIES TIMEOUT 10)
