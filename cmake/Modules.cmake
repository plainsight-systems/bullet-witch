# Read SF.11/SF.13/GEN.6: actual self-contained headers, scoped include names,
# compiler-required inline bodies retained. No copied/generated export headers.
function(bw_header_interface target root)
  add_library(${target} INTERFACE)
  target_include_directories(${target} INTERFACE "${root}/public")
  target_link_libraries(${target} INTERFACE bw_contracts)
endfunction()
