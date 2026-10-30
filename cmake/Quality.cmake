# A fresh tool contract: neither formatter nor analyzer silently changes version.
set(BW_LLVM_TOOLS_VERSION "21.1.8")
foreach(tool IN ITEMS clang-format clang-tidy)
  string(REPLACE "-" "_" tool_key "${tool}")
  find_program(BW_${tool_key} NAMES ${tool}
    HINTS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin REQUIRED)
  execute_process(COMMAND "${BW_${tool_key}}" --version
    OUTPUT_VARIABLE tool_version RESULT_VARIABLE version_result)
  if(NOT version_result EQUAL 0 OR
     NOT tool_version MATCHES "version ${BW_LLVM_TOOLS_VERSION}([^0-9.]|$)")
    message(FATAL_ERROR "${tool} must be ${BW_LLVM_TOOLS_VERSION}; got: ${tool_version}")
  endif()
endforeach()

set(CMAKE_CXX_CLANG_TIDY "${BW_clang_tidy};--warnings-as-errors=*")
file(GLOB_RECURSE BW_FORMAT_FILES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/*.hpp" "${PROJECT_SOURCE_DIR}/src/*.cpp"
  "${PROJECT_SOURCE_DIR}/src/*.inl"
  "${PROJECT_SOURCE_DIR}/tests/*.hpp" "${PROJECT_SOURCE_DIR}/tests/*.cpp"
  "${PROJECT_SOURCE_DIR}/tools/*.cpp")
add_custom_target(format-check
  COMMAND "${BW_clang_format}" --dry-run --Werror ${BW_FORMAT_FILES}
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" VERBATIM)
add_custom_target(format
  COMMAND "${BW_clang_format}" -i ${BW_FORMAT_FILES}
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" VERBATIM)
