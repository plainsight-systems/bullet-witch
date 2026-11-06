#ifndef BW_CONTENT_LOADER_HPP
#define BW_CONTENT_LOADER_HPP
#include "bw/content/asset_id.hpp"
#include "bw/content/cooked_asset.hpp"
#include "bw/content/layers.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Responsibility: opening an asset by id (docs/architecture/content.md,
// "Loading an asset" and invariants 3, 10 and 11: resolve across the layers
// in order, open the cooked asset, check version and hash, hand the caller a
// runtime object; a missing asset is a named error carrying the id and the
// layers searched; nothing above the loader can tell which layer answered).
// The loader parses the frame and nothing of the payload.
//
// Contract.
//   load(id, kind): for each present layer in Layer order, the file
//   <layer>/asset_relative_path(id); the first that exists is read whole
//   into the handle's own bytes and parsed with parse_cooked_header; then
//   check_expectations(header, id, kind); then, when
//   options.verify_payload_hash, verify_payload_hash. The first failure is
//   the result, as a LoadError with the same meaning as the CookedError it
//   came from (load_error_from). `searched` holds every present layer up
//   to and including the one that answered, or all present layers for
//   Missing. ReadFailed carries the OS message in detail. A file that
//   exists but fails any check is a failure of that id: no later layer is
//   tried, because a replacement in an earlier layer is the answer by
//   invariant 3 and a broken one must be visible, not shadowed.
//   AssetHandle owns the bytes; payload() views them and stays valid while
//   the handle lives, through moves; id() is the id the caller asked for,
//   which check_expectations proved equal to the file's. It does not say
//   which layer answered.
//   Whole-file read: the plan's loader memory-maps; this slice reads the
//   file through the standard library instead, because a mapping is a
//   platform contract and adapter the ownership list does not hold
//   (nothing under src/platform maps files today). The handle's interface
//   is the same under either; the departure is recorded in the plan.
//   options.verify_payload_hash defaults to true; a caller that turns it
//   off is naming the choice, and the loader never turns it off itself.
//
// Core guidelines read: R.1 and C.35 (the handle owns its bytes with no
// manual release), I.11 (nothing is handed out by raw pointer), E.27 (one
// code per failure), CP.1 (a Loader is immutable after
// construction, so concurrent loads need no lock). Performance guidelines
// read: CDSA.32 (the payload is handed over in the consumer's layout,
// unparsed), COPY.5 (the handle is move-only).
namespace bw::content {

enum class LoadError : std::uint8_t {
  None,
  // The id does not validate; no layer was searched.
  InvalidId,
  // No present layer holds the file.
  Missing,
  // The file exists and the OS refused to read it whole.
  ReadFailed,
  // The CookedError of the same name (cooked_asset.hpp).
  TooShort,
  BadMagic,
  VersionMismatch,
  UnknownKind,
  FlagsUnknown,
  IdInvalid,
  PayloadMisaligned,
  PayloadOutOfBounds,
  PayloadHashMismatch,
  IdMismatch,
  KindMismatch,
};

[[nodiscard]] constexpr LoadError load_error_from(CookedError error) noexcept {
  switch (error) {
  case CookedError::None:
    return LoadError::None;
  case CookedError::TooShort:
    return LoadError::TooShort;
  case CookedError::BadMagic:
    return LoadError::BadMagic;
  case CookedError::VersionMismatch:
    return LoadError::VersionMismatch;
  case CookedError::UnknownKind:
    return LoadError::UnknownKind;
  case CookedError::FlagsUnknown:
    return LoadError::FlagsUnknown;
  case CookedError::IdInvalid:
    return LoadError::IdInvalid;
  case CookedError::PayloadMisaligned:
    return LoadError::PayloadMisaligned;
  case CookedError::PayloadOutOfBounds:
    return LoadError::PayloadOutOfBounds;
  case CookedError::PayloadHashMismatch:
    return LoadError::PayloadHashMismatch;
  case CookedError::IdMismatch:
    return LoadError::IdMismatch;
  case CookedError::KindMismatch:
    return LoadError::KindMismatch;
  }
  return LoadError::None;
}

struct LoadFailure {
  LoadError error{LoadError::None};
  AssetId id{};
  LayerMask searched{};
  std::string detail{};
};

// The enumerator's name as written above, for a failure line.
[[nodiscard]] std::string_view load_error_name(LoadError error) noexcept;

// One line for a person: the error's name, the id, the layers searched
// (layer_mask_text) and the detail when there is one. The wording is not
// part of the contract.
[[nodiscard]] std::string load_failure_text(const LoadFailure& failure);

class AssetHandle {
public:
  AssetHandle() = default;
  AssetHandle(AssetHandle&&) noexcept = default;
  AssetHandle& operator=(AssetHandle&&) noexcept = default;
  AssetHandle(const AssetHandle&) = delete;
  AssetHandle& operator=(const AssetHandle&) = delete;
  ~AssetHandle() = default;

  [[nodiscard]] const AssetId& id() const noexcept { return id_; }
  [[nodiscard]] const CookedHeader& header() const noexcept { return header_; }
  [[nodiscard]] std::span<const std::byte> payload() const noexcept {
    return cooked_payload(bytes_, header_);
  }
  // The whole file, for a caller that re-checks the frame.
  [[nodiscard]] std::span<const std::byte> file() const noexcept { return bytes_; }

private:
  friend class Loader;
  AssetId id_{};
  std::vector<std::byte> bytes_{};
  CookedHeader header_{};
};

struct LoadResult {
  AssetHandle handle{};
  LoadFailure failure{};
  [[nodiscard]] bool ok() const noexcept { return failure.error == LoadError::None; }
};

struct LoaderOptions {
  bool verify_payload_hash{true};
};

class Loader {
public:
  explicit Loader(LayerDirectories layers, LoaderOptions options = {}) noexcept;
  [[nodiscard]] const LayerDirectories& layers() const noexcept { return layers_; }
  [[nodiscard]] const LoaderOptions& options() const noexcept { return options_; }
  [[nodiscard]] LoadResult load(const AssetId& id, AssetKind kind) const;

private:
  LayerDirectories layers_;
  LoaderOptions options_;
};

} // namespace bw::content

#endif
