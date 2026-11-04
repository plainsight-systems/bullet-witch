# Shared contracts export only their public directories. Selected target layout
# and inline definitions are compiler-required detail, not a universal src root.
foreach(owner IN ITEMS math memory time tasks diagnostics)
  bw_header_interface(bw_platform_${owner}_headers "${CMAKE_CURRENT_SOURCE_DIR}/${owner}")
endforeach()
bw_header_interface(bw_macos_memory_headers "${CMAKE_CURRENT_SOURCE_DIR}/os/macos/memory")
bw_header_interface(bw_macos_time_headers "${CMAKE_CURRENT_SOURCE_DIR}/os/macos/time")
target_link_libraries(bw_macos_memory_headers INTERFACE bw_platform_memory_headers)
target_link_libraries(bw_macos_time_headers INTERFACE bw_platform_time_headers)
# The window contract (window/public/bw/platform/window/window.hpp) and its
# macOS selection header.
bw_header_interface(bw_platform_window_headers "${CMAKE_CURRENT_SOURCE_DIR}/window")
bw_header_interface(bw_macos_window_headers "${CMAKE_CURRENT_SOURCE_DIR}/os/macos/window")
target_link_libraries(bw_macos_window_headers INTERFACE bw_platform_window_headers)
# The platform-neutral event queue (WindowEventQueue) on its own, so its unit
# test links no OS framework.
add_library(bw_window_events window/window_event_queue.cpp)
target_link_libraries(bw_window_events PUBLIC bw_platform_window_headers)
# The window library: the event queue and, on Apple hosts, the AppKit adapter
# (macos/window.hpp) in Objective-C++ with ARC, under the same clang-tidy
# command as C++. No other host has an adapter yet.
if(APPLE)
  enable_language(OBJCXX)
  add_library(bw_window os/macos/window/window.mm)
  target_compile_options(bw_window PRIVATE -fobjc-arc)
  set_target_properties(bw_window PROPERTIES OBJCXX_CLANG_TIDY "${CMAKE_CXX_CLANG_TIDY}")
  target_link_libraries(bw_window
    PUBLIC bw_window_events bw_platform_window_headers bw_macos_window_headers
    PRIVATE "-framework AppKit" "-framework QuartzCore")
else()
  add_library(bw_window INTERFACE)
  target_link_libraries(bw_window INTERFACE bw_window_events)
endif()
# The selected memory contract names native block/synchronization layouts.
# Export their compiler-required definitions without exposing allocator internals.
target_include_directories(bw_platform_memory_headers INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/os/macos/memory/public")
target_link_libraries(bw_platform_memory_headers INTERFACE bw_memory_headers)

add_library(bw_platform_time os/macos/time/platform_time.cpp)
target_link_libraries(bw_platform_time PUBLIC bw_platform_time_headers bw_macos_time_headers
  PRIVATE Threads::Threads)
