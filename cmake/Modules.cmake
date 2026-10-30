# Read SF.11/SF.13/GEN.6: actual self-contained headers, scoped include names,
# compiler-required inline bodies retained. No copied/generated export headers.
# Unreal f45729c0b6f097038e72b52d366a62baeca3275b Core.Build.cs: public/private
# dependency declarations guide this CMake adaptation, not its exact syntax.
function(bw_header_interface target root)
  add_library(${target} INTERFACE)
  target_include_directories(${target} INTERFACE "${root}/public")
  target_link_libraries(${target} INTERFACE bw_contracts)
endfunction()
