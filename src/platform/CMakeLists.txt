# Shared contracts export only their public directories. Selected target layout
# and inline definitions are compiler-required detail, not a universal src root.
foreach(owner IN ITEMS math memory time tasks diagnostics process)
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

# Where the running executable is (process/public/bw/platform/process/
# executable_directory.hpp), the macOS implementation.
add_library(bw_platform_process os/macos/process/executable_directory.cpp)
target_link_libraries(bw_platform_process PUBLIC bw_platform_process_headers)
# Device input: what an adapter hands upward, the pad contract and the
# scripted device (input/public/bw/platform/input), and the macOS pad
# selector. The window's contract names the device sample type.
bw_header_interface(bw_platform_input_headers "${CMAKE_CURRENT_SOURCE_DIR}/input")
bw_header_interface(bw_macos_input_headers "${CMAKE_CURRENT_SOURCE_DIR}/os/macos/input")
target_link_libraries(bw_macos_input_headers INTERFACE bw_platform_input_headers)
target_link_libraries(bw_platform_window_headers INTERFACE bw_platform_input_headers)
# The window's device-sample buffer (WindowInputBuffer) beside the event
# queue, platform-neutral, so its unit test links no OS framework either.
target_sources(bw_window_events PRIVATE window/window_input_buffer.cpp)
# The scripted device's parse (scripted_device.hpp): the standard library
# only, no OS.
add_library(bw_scripted_device input/scripted_device.cpp)
target_link_libraries(bw_scripted_device PUBLIC bw_platform_input_headers)
# The adapter's capture and raw mouse motion: the cursor association and
# visibility calls (CoreGraphics) and the game controller framework's mouse.
if(APPLE)
  target_link_libraries(bw_window PRIVATE "-framework GameController" "-framework CoreGraphics")
endif()
# The macOS pad (os/macos/input/public/bw/platform/os/macos/pad.hpp): the
# difference of two profile readings as samples (pad_samples), pure C++ in a
# library of its own so its unit test links no framework, and on Apple hosts
# the adapter over the game controller framework in Objective-C++ with ARC,
# under the same clang-tidy command as C++.
add_library(bw_pad_samples os/macos/input/pad_samples.cpp)
target_link_libraries(bw_pad_samples PUBLIC bw_macos_input_headers)
if(APPLE)
  add_library(bw_pad os/macos/input/pad.mm)
  target_compile_options(bw_pad PRIVATE -fobjc-arc)
  set_target_properties(bw_pad PROPERTIES OBJCXX_CLANG_TIDY "${CMAKE_CXX_CLANG_TIDY}")
  target_link_libraries(bw_pad PUBLIC bw_macos_input_headers
    PRIVATE bw_pad_samples "-framework Foundation" "-framework GameController")
endif()
