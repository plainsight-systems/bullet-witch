# Render graph: pass declarations, compile, the schedule policy and execute
# against the RHI. See docs/architecture/render-graph.md. The header
# interface names the public headers; the module library holds the
# declaration side and compile (builder.cpp, compile.cpp, sharing the
# private graph_state.hpp), the schedule policies (schedule_policy.cpp) and
# execute (executor.cpp). access.hpp is header-only (every function
# constexpr) and has no source.
bw_header_interface(bw_graph_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_graph_headers INTERFACE bw_rhi_headers)

add_library(bw_graph
  builder.cpp
  compile.cpp
  executor.cpp
  schedule_policy.cpp)
target_link_libraries(bw_graph PUBLIC bw_graph_headers bw_rhi)
