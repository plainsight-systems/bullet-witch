#ifndef BW_RENDERING_SHADERS_ARTIFACT_FILE_HPP
#define BW_RENDERING_SHADERS_ARTIFACT_FILE_HPP

#include "bw/rendering/shaders/artifact.hpp"
#include "bw/rendering/shaders/permutation.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: one registered shader's artifact read from an artifact
// directory and checked, ready to hand to a pipeline description.
//
// Contract.
//   1. read_artifact_file finds `shader_name` in shader_registry()
//      (ShaderUnknown otherwise), reads the whole file directory /
//      artifact_file_name(name, permutation, stage) (FileUnreadable when it
//      cannot be opened or read), and loads it with load_artifact against
//      the engine root layout, the shader's draw block when it has one, and
//      its permutation count (ArtifactRejected, with load_artifact's error).
//   2. On success `out.artifact` views `out.bytes`. An ArtifactFile is
//      movable and not copyable: a move keeps the views valid, a copy would
//      not. The file must outlive every pipeline description that points
//      at its artifact.
//   3. The failure carries the path tried (empty for ShaderUnknown). On a
//      failure `out` holds no bytes, so the failure owns the one text
//      load_artifact's error views in them: block_name here, with
//      load.block_name left empty.
//   4. artifact_file_failure_text is the failure as one line for a person,
//      with no trailing newline and no prefix, so every caller reports a
//      shader that did not load the same way: ShaderUnknown names
//      `shader_name`; FileUnreadable names the path; ArtifactRejected names
//      the path, the load error's number, the block and the expected and
//      actual values and byte offset load_artifact reported. It is empty
//      for a failure that is ok(). The wording is not part of the contract.
//
// Read guidance: R.1 (the file owns the bytes its artifact views), E.27.
namespace bw::shaders {

struct ArtifactFile {
  std::vector<std::byte> bytes{};
  Artifact artifact{};

  ArtifactFile() = default;
  ArtifactFile(const ArtifactFile&) = delete;
  ArtifactFile& operator=(const ArtifactFile&) = delete;
  ArtifactFile(ArtifactFile&&) noexcept = default;
  ArtifactFile& operator=(ArtifactFile&&) noexcept = default;
  ~ArtifactFile() = default;
};

enum class ArtifactFileError : std::uint8_t {
  None,
  ShaderUnknown,
  FileUnreadable,
  ArtifactRejected,
};

struct ArtifactFileFailure {
  ArtifactFileError error{ArtifactFileError::None};
  std::filesystem::path path{};
  // Set for ArtifactRejected; load.block_name is empty and the block's name
  // is block_name.
  LoadError load{};
  std::string block_name{};
  [[nodiscard]] bool ok() const noexcept { return error == ArtifactFileError::None; }
};

[[nodiscard]] ArtifactFileFailure read_artifact_file(const std::filesystem::path& directory,
                                                     std::string_view shader_name,
                                                     PermutationId permutation, ArtifactFile& out);

// `shader_name` is the name read_artifact_file was asked for.
[[nodiscard]] std::string artifact_file_failure_text(const ArtifactFileFailure& failure,
                                                     std::string_view shader_name);

} // namespace bw::shaders

#endif
