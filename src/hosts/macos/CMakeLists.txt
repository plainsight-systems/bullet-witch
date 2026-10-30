add_executable(engine_host engine_host.cpp)
# Global operators remain executable-local, never a transitive library choice.
target_sources(engine_host PRIVATE ../../engine/core/memory/global_allocation.cpp)
target_link_libraries(engine_host PRIVATE bw_runtime bw_allocator)
