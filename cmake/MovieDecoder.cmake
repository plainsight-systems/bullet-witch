# Shared LGPL-only FFmpeg decoder dependency, fetched into the ignored build.
# No Homebrew/system FFmpeg fallback: those builds may enable GPL components.
include(ExternalProject)
include(ProcessorCount)
ProcessorCount(bw_ffmpeg_jobs)
if(bw_ffmpeg_jobs GREATER 8)
  set(bw_ffmpeg_jobs 8)
elseif(bw_ffmpeg_jobs LESS 1)
  set(bw_ffmpeg_jobs 1)
endif()
find_program(BW_FFMPEG_MAKE NAMES gmake make REQUIRED)
set(bw_ffmpeg_prefix "${CMAKE_BINARY_DIR}/vendor/ffmpeg")
set(bw_ffmpeg_install "${bw_ffmpeg_prefix}/install")
file(MAKE_DIRECTORY "${bw_ffmpeg_install}/include")
set(bw_ffmpeg_byproducts)
foreach(component avformat avcodec swscale swresample avutil)
  list(APPEND bw_ffmpeg_byproducts
    "${bw_ffmpeg_install}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}${component}${CMAKE_SHARED_LIBRARY_SUFFIX}")
endforeach()
set(bw_ffmpeg_platform_options)
if(APPLE)
  list(APPEND bw_ffmpeg_platform_options "--install-name-dir=@rpath")
endif()
ExternalProject_Add(bw_ffmpeg_build
  PREFIX "${bw_ffmpeg_prefix}"
  URL https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz
  URL_HASH SHA256=733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  CONFIGURE_COMMAND <SOURCE_DIR>/configure
    "--prefix=${bw_ffmpeg_install}"
    --disable-everything --disable-autodetect
    --disable-gpl --disable-nonfree --disable-version3
    --disable-programs --disable-doc --disable-static --enable-shared
    --disable-avdevice --disable-avfilter --disable-postproc
    --enable-avformat --enable-avcodec --enable-avutil
    --enable-swscale --enable-swresample
    --enable-demuxer=asf --enable-protocol=file
    --enable-decoder=wmv3,vc1,wmapro --enable-parser=vc1 --enable-pthreads
    ${bw_ffmpeg_platform_options}
  BUILD_COMMAND "${BW_FFMPEG_MAKE}" "-j${bw_ffmpeg_jobs}"
  INSTALL_COMMAND "${BW_FFMPEG_MAKE}" install
  BUILD_BYPRODUCTS ${bw_ffmpeg_byproducts}
  LOG_DOWNLOAD ON LOG_CONFIGURE ON LOG_BUILD ON LOG_INSTALL ON)
# Declaring the install name before the external build lets CMake generate
# consumer RPATH entries even on a clean configure (the dylibs do not exist yet).
# These ABI majors are part of the pinned FFmpeg 7.1.1 source version.
set(bw_ffmpeg_avformat_major 61)
set(bw_ffmpeg_avcodec_major 61)
set(bw_ffmpeg_swscale_major 8)
set(bw_ffmpeg_swresample_major 5)
set(bw_ffmpeg_avutil_major 59)
foreach(component avformat avcodec swscale swresample avutil)
  add_library(bw_ffmpeg_${component} SHARED IMPORTED GLOBAL)
  set_target_properties(bw_ffmpeg_${component} PROPERTIES
    IMPORTED_LOCATION "${bw_ffmpeg_install}/lib/${CMAKE_SHARED_LIBRARY_PREFIX}${component}${CMAKE_SHARED_LIBRARY_SUFFIX}"
    INTERFACE_INCLUDE_DIRECTORIES "${bw_ffmpeg_install}/include")
  if(APPLE)
    set_target_properties(bw_ffmpeg_${component} PROPERTIES
      IMPORTED_SONAME "@rpath/lib${component}.${bw_ffmpeg_${component}_major}.dylib")
  elseif(UNIX)
    set_target_properties(bw_ffmpeg_${component} PROPERTIES
      IMPORTED_SONAME "lib${component}.so.${bw_ffmpeg_${component}_major}")
  endif()
  add_dependencies(bw_ffmpeg_${component} bw_ffmpeg_build)
endforeach()
find_package(Threads REQUIRED)
add_library(bw_video_ffmpeg src/video/ffmpeg/decoder.cpp)
target_include_directories(bw_video_ffmpeg PUBLIC include INTERFACE src PRIVATE src)
target_compile_features(bw_video_ffmpeg PUBLIC cxx_std_20)
target_link_libraries(bw_video_ffmpeg PRIVATE bw_video bw_ffmpeg_avformat bw_ffmpeg_avcodec
  bw_ffmpeg_swscale bw_ffmpeg_swresample bw_ffmpeg_avutil Threads::Threads)
bw_strict(bw_video_ffmpeg)
