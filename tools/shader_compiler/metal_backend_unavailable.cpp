#include "backends.hpp"

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Responsibility: the converter session of backends.hpp on hosts without the
// Metal Shader Converter, which Apple ships for macOS and Windows only and
// cmake/ThirdParty.cmake declares on Apple platforms only. Every operation
// fails by name; nothing here produces a Metal library, and
// cmake/ShaderArtifacts.cmake declares no Metal artifact on these hosts.
//
// Retrieved rules: E.27 (the failure is the named ToolchainUnavailable).
namespace bw::shader_compiler::detail {

struct MetalConverterSession::State {
  std::string identity{};
};

MetalConverterSession::MetalConverterSession(std::unique_ptr<State> state) noexcept
    : state_(std::move(state)) {}
MetalConverterSession::MetalConverterSession(MetalConverterSession&& other) noexcept = default;
MetalConverterSession&
MetalConverterSession::operator=(MetalConverterSession&& other) noexcept = default;
MetalConverterSession::~MetalConverterSession() = default;

BackendStatus MetalConverterSession::open(std::string_view library_path,
                                          std::unique_ptr<MetalConverterSession>& session) {
  session.reset();
  return BackendStatus{"the Metal Shader Converter is not available on this host (asked to load '" +
                           std::string{library_path} + "')",
                       shaders::ExitCode::ToolchainUnavailable};
}

const std::string& MetalConverterSession::identity() const noexcept { return state_->identity; }

BackendStatus MetalConverterSession::convert(std::span<const std::byte> /*dxil*/,
                                             std::string_view /*entry_point*/,
                                             shaders::ShaderStage /*stage*/,
                                             std::vector<std::byte>& out) const {
  out.clear();
  return BackendStatus{"the Metal Shader Converter is not available on this host",
                       shaders::ExitCode::ToolchainUnavailable};
}

} // namespace bw::shader_compiler::detail
