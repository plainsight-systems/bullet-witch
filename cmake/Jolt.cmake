# CPU rigid-body provider, built from immutable reviewed source. Public engine
# contracts contain no Jolt headers/types/defines. Platform SIMD stays upstream;
# GPU simulation, tools, profiling/debug rendering and implicit worker pools do not.
function(bw_configure_physics_backend)
  include(FetchContent)
  set(JPH_BUILD_SHARED_LIBS OFF)
  set(INTERPROCEDURAL_OPTIMIZATION OFF)
  set(OVERRIDE_CXX_FLAGS OFF)
  set(CPP_EXCEPTIONS_ENABLED ON)
  set(CPP_RTTI_ENABLED ON)
  set(ENABLE_ALL_WARNINGS OFF)
  set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE OFF)
  set(DEBUG_RENDERER_IN_DISTRIBUTION OFF)
  set(PROFILER_IN_DEBUG_AND_RELEASE OFF)
  set(PROFILER_IN_DISTRIBUTION OFF)
  set(ENABLE_OBJECT_STREAM OFF)
  set(JPH_USE_DX12 OFF)
  set(JPH_USE_VK OFF)
  set(JPH_USE_MTL OFF)
  set(JPH_USE_CPU_COMPUTE OFF)
  set(ENABLE_INSTALL OFF)
  FetchContent_Declare(bw_jolt
    URL https://codeload.github.com/jrouwe/JoltPhysics/tar.gz/5d5275c38074c44b14f38b85bc9c73573078c808
    URL_HASH SHA256=fbb4911723a4fe49c5b51281ef4ba10cffd71952d29afd4d8f0ffa9b3bd51f29
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR Build)
  FetchContent_MakeAvailable(bw_jolt)
  set_property(TARGET Jolt APPEND PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
    "${bw_jolt_SOURCE_DIR}")
  add_library(bw_physics
    src/engine/physics/jolt/runtime.cpp
    src/engine/physics/jolt/world.cpp)
  target_include_directories(bw_physics PUBLIC include)
  target_compile_features(bw_physics PUBLIC cxx_std_20)
  target_link_libraries(bw_physics PUBLIC bw_memory bw_math bw_timing PRIVATE Jolt)
  bw_strict(bw_physics)
endfunction()
bw_configure_physics_backend()
