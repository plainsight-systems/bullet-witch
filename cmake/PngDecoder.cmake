# Pinned portable PNG decoding. Dependencies remain under the ignored build.
include(FetchContent)
function(bw_configure_png_decoder)
  set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
  set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
  set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(bw_zlib
    URL https://github.com/madler/zlib/archive/refs/tags/v1.3.2.tar.gz
    URL_HASH SHA256=b99a0b86c0ba9360ec7e78c4f1e43b1cbdf1e6936c8fa0f6835c0cd694a495a1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(bw_zlib)
  # libpng uses FindZLIB. Supply the pinned target and headers explicitly rather
  # than allowing discovery of a different system library.
  add_library(ZLIB::ZLIB ALIAS zlibstatic)
  set(ZLIB_LIBRARY zlibstatic CACHE STRING "Pinned zlib target" FORCE)
  set(ZLIB_INCLUDE_DIR "${bw_zlib_SOURCE_DIR}" CACHE PATH "Pinned zlib headers" FORCE)
  set(SKIP_INSTALL_ALL ON)
  set(PNG_FRAMEWORK OFF CACHE BOOL "" FORCE)
  set(PNG_SHARED OFF CACHE BOOL "" FORCE)
  set(PNG_STATIC ON CACHE BOOL "" FORCE)
  set(PNG_TESTS OFF CACHE BOOL "" FORCE)
  set(PNG_TOOLS OFF CACHE BOOL "" FORCE)
  set(PNG_EXECUTABLES OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(bw_libpng
    URL https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.59.tar.gz
    URL_HASH SHA256=2540302a1844ad2b2b501977abecfa850f265f97b78f065a712ab4074a89f5b5
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(bw_libpng)
  add_library(bw_images src/formats/image.cpp src/formats/bitmap_font.cpp)
  target_include_directories(bw_images PUBLIC include)
  target_compile_features(bw_images PUBLIC cxx_std_20)
  target_link_libraries(bw_images PRIVATE png_static PUBLIC bw_resources)
  bw_strict(bw_images)
endfunction()
bw_configure_png_decoder()
