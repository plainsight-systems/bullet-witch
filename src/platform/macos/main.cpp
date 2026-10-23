// Mac executable entry and backend selection. Called by the process on its
// main thread with argc valid argv elements. Parse before opening a window;
// create file/native services; invoke shared framework; unwind before exit.
// The shared loop borrows these services. No playback/game algorithm lives
// here. Exceptions become stderr diagnostics and nonzero exit; success returns
// the framework result. See docs/architecture/file-mapping.md. No additional
// per-frame work or allocations are introduced. Design MCP: I.27 (#ri-pimpl)
// keeps native details behind common headers; C.129 (#rh-kind) permits explicit
// codec choice. Implementation MCP: E.6 (#re-raii) preserves cleanup on
// failure; performance COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) keeps
// native and decoder resources uniquely owned.
#include <exception>
#include <iostream>
#include <span>

#include "framework/startup_application.hpp"
#include "video/ffmpeg/decoder.hpp"

int main(int argc, char** argv) {
  try {
    const auto configuration = bw::framework::parse_startup_options(
        std::span<char*>(argv, static_cast<std::size_t>(argc)));
    bw::platform::HostFiles files;
    bw::platform::StartupHost host;
    return bw::framework::run_startup(configuration, host, files,
                                      bw::video::ffmpeg::open_decoder);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
