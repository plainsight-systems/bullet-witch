#include "bw/rendering/shaders/artifact_file.hpp"

#include "bw/rendering/shaders/artifact.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/rendering/shaders/root_layout.hpp"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: the out-of-line part of
// bw/rendering/shaders/artifact_file.hpp.
namespace bw::shaders {

ArtifactFileFailure read_artifact_file(const std::filesystem::path& directory,
                                       std::string_view shader_name, PermutationId permutation,
                                       ArtifactFile& out) {
  out = ArtifactFile{};
  ArtifactFileFailure failure{};
  const ShaderDescription* shader = find_shader(shader_registry(), shader_name);
  if (shader == nullptr) {
    failure.error = ArtifactFileError::ShaderUnknown;
    return failure;
  }
  failure.path = directory / artifact_file_name(shader->name, permutation, shader->stage);
  std::ifstream stream(failure.path, std::ios::binary);
  if (!stream) {
    failure.error = ArtifactFileError::FileUnreadable;
    return failure;
  }
  // The size, then one read of exactly that many bytes: a short read is a
  // failure here, not a truncated artifact handed to the loader.
  stream.seekg(0, std::ios::end);
  const std::streamoff size = stream.tellg();
  stream.seekg(0, std::ios::beg);
  if (!stream || size < 0) {
    failure.error = ArtifactFileError::FileUnreadable;
    return failure;
  }
  std::vector<char> chars(static_cast<std::size_t>(size));
  stream.read(chars.data(), size);
  if (!stream || stream.gcount() != size) {
    failure.error = ArtifactFileError::FileUnreadable;
    return failure;
  }
  out.bytes.resize(chars.size());
  for (std::size_t i = 0; i < chars.size(); ++i) {
    out.bytes[i] = static_cast<std::byte>(chars[i]);
  }
  std::vector<const BlockLayout*> blocks;
  if (shader->draw_block != nullptr) {
    blocks.push_back(shader->draw_block);
  }
  const LoadExpectations expectations{&engine_root_layout(), blocks, shader->permutation_count};
  const ArtifactLoadResult result = load_artifact(out.bytes, expectations);
  if (!result.ok()) {
    failure.error = ArtifactFileError::ArtifactRejected;
    failure.load = result.error();
    // The name views the bytes dropped below; the failure keeps a copy.
    failure.block_name = std::string(failure.load.block_name);
    failure.load.block_name = {};
    out = ArtifactFile{};
    return failure;
  }
  out.artifact = result.artifact();
  return {};
}

std::string artifact_file_failure_text(const ArtifactFileFailure& failure,
                                       std::string_view shader_name) {
  std::ostringstream text;
  switch (failure.error) {
  case ArtifactFileError::None:
    break;
  case ArtifactFileError::ShaderUnknown:
    text << "the shader registry has no shader named " << shader_name;
    break;
  case ArtifactFileError::FileUnreadable:
    text << "the shader artifact " << failure.path.string() << " cannot be read";
    break;
  case ArtifactFileError::ArtifactRejected:
    text << "loading the shader artifact " << failure.path.string() << " failed: error "
         << static_cast<unsigned>(failure.load.code) << ": block '" << failure.block_name
         << "' expected " << failure.load.expected << " actual " << failure.load.actual
         << " at byte " << failure.load.byte_offset;
    break;
  }
  return text.str();
}

} // namespace bw::shaders
