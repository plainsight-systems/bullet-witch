# Shared compressed-wave preparation. The same pinned LGPL FFmpeg build serves
# movies and game audio; game decoding has its own finite sample contract.
add_library(bw_audio_decode src/audio/ffmpeg/wave.cpp src/audio/ffmpeg/music.cpp)
target_link_libraries(bw_audio_decode PUBLIC bw_resources PRIVATE
  bw_ffmpeg_avcodec bw_ffmpeg_avutil bw_ffmpeg_swresample)
bw_strict(bw_audio_decode)

# Pinned XACT-compatible mixer and native SDL CoreAudio device support.
# No system package fallback; exact source/license retained in dependency docs.
if(APPLE)
  function(bw_configure_game_audio_backend)
  include(FetchContent)
  set(SDL_SHARED OFF)
  set(SDL_STATIC ON)
  set(SDL_TESTS OFF)
  set(SDL_TEST_LIBRARY OFF)
  set(SDL_EXAMPLES OFF)
  set(SDL_INSTALL OFF)
  set(SDL_VIDEO OFF)
  set(SDL_RENDER OFF)
  set(SDL_GPU OFF)
  set(SDL_CAMERA OFF)
  set(SDL_JOYSTICK OFF)
  set(SDL_HAPTIC OFF)
  set(SDL_SENSOR OFF)
  set(SDL_HIDAPI OFF)
  FetchContent_Declare(bw_audio_sdl
    URL https://codeload.github.com/libsdl-org/SDL/tar.gz/829a65d769d935c4852f8159e964312c0957260a
    URL_HASH SHA256=48e15cf1a6928453d77afa955d1fdfbbca9169e2bc3d76d111acb9b8bb076d66
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(bw_audio_sdl)
  set(BUILD_SHARED_LIBS OFF)
  set(BUILD_SDL3 ON)
  set(XNASONG OFF)
  set(FAUDIO_INSTALL OFF)
  FetchContent_Declare(bw_audio_fact
    URL https://codeload.github.com/FNA-XNA/FAudio/tar.gz/6839b88e304a046ae1a609ff14a085371e02a4e0
    URL_HASH SHA256=987f06301b0aec73fb52472ecbbf58fa07a1737900ffccfe3e67f6746641810a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(bw_audio_fact)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchFact.cmake")
  # Replace the Win32 codec translation unit. The separate, checked patch above
  # adds the original request-owned mix gain to the pinned cue/wave boundary.
  get_target_property(bw_fact_sources FAudio-static SOURCES)
  list(FILTER bw_fact_sources EXCLUDE REGEX "FAudio_platform_win32_wmadec.c$")
  set_property(TARGET FAudio-static PROPERTY SOURCES "${bw_fact_sources}")
  target_compile_definitions(FAudio-static PRIVATE HAVE_WMADEC=1)
  # Vendor public headers use C extensions; keep authored C++ warnings strict.
  set_property(TARGET FAudio-static APPEND PROPERTY INTERFACE_SYSTEM_INCLUDE_DIRECTORIES
    "${bw_audio_fact_SOURCE_DIR}/include" "${bw_audio_fact_SOURCE_DIR}/src")
  add_library(bw_audio_fact src/audio/fact/output.cpp src/audio/fact/wma_bridge.c)
  target_include_directories(bw_audio_fact PUBLIC include INTERFACE src PRIVATE src)
  target_compile_definitions(bw_audio_fact PRIVATE HAVE_WMADEC=1)
  target_link_libraries(bw_audio_fact PUBLIC bw_audio_decode PRIVATE FAudio-static)
  bw_strict(bw_audio_fact)
  endfunction()
  bw_configure_game_audio_backend()
endif()
