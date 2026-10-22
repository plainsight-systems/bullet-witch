include(FetchContent)
FetchContent_Declare(lua502
  URL https://www.lua.org/ftp/lua-5.0.2.tar.gz
  URL_HASH SHA256=a6c85d85f912e1c321723084389d63dee7660b81b8292452b190ea7190dd73bc
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(lua502)
include("${CMAKE_CURRENT_LIST_DIR}/PatchLua502.cmake")
file(GLOB lua502_core CONFIGURE_DEPENDS "${lua502_SOURCE_DIR}/src/*.c")
add_library(bw_lua502 STATIC ${lua502_core} "${lua502_SOURCE_DIR}/src/lib/lauxlib.c")
target_include_directories(bw_lua502 SYSTEM PUBLIC "${lua502_SOURCE_DIR}/include")
set_target_properties(bw_lua502 PROPERTIES C_STANDARD 99)
if(BW_SANITIZE)
  target_compile_options(bw_lua502 PRIVATE -fsanitize=${BW_SANITIZERS} -fno-omit-frame-pointer)
  target_link_options(bw_lua502 PUBLIC -fsanitize=${BW_SANITIZERS})
endif()
