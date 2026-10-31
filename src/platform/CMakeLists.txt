# Shared contracts export only their public directories. Selected target layout
# and inline definitions are compiler-required detail, not a universal src root.
foreach(owner IN ITEMS math memory time tasks diagnostics compiler)
  bw_header_interface(bw_platform_${owner}_headers "${CMAKE_CURRENT_SOURCE_DIR}/${owner}")
endforeach()
bw_header_interface(bw_macos_memory_headers "${CMAKE_CURRENT_SOURCE_DIR}/os/macos/memory")
bw_header_interface(bw_macos_time_headers "${CMAKE_CURRENT_SOURCE_DIR}/os/macos/time")
target_link_libraries(bw_macos_memory_headers INTERFACE bw_platform_memory_headers)
target_link_libraries(bw_macos_time_headers INTERFACE bw_platform_time_headers)
# The selected memory contract names native block/synchronization layouts.
# Export their compiler-required definitions without exposing allocator internals.
target_include_directories(bw_platform_memory_headers INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/os/macos/memory/public")
target_link_libraries(bw_platform_memory_headers INTERFACE bw_memory_headers)

add_library(bw_platform_time os/macos/time/platform_time.cpp)
target_link_libraries(bw_platform_time PUBLIC bw_platform_time_headers bw_macos_time_headers
  PRIVATE Threads::Threads)

# Native debugger observation is owned by platform diagnostics, not math or tests.
add_library(bw_platform_debugger os/macos/diagnostics/debugger.cpp)
target_link_libraries(bw_platform_debugger PUBLIC bw_platform_diagnostics_headers)
