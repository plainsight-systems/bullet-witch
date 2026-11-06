#ifndef BW_CONTENT_CONVERTER_SKELETON_READER_HPP
#define BW_CONTENT_CONVERTER_SKELETON_READER_HPP
#include "bw/content_converter/asset_data.hpp"
#include "bw/content_converter/cpr_container.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

// Responsibility: reading a model container's `motion.bil` and
// `motion._bl` records into skeleton data (the converter's skeleton
// reader).
//
// Framing. motion.bil: "MBDB", u32 bone count, then 40-byte entries of
// u32 id, 32-byte NUL-padded name, u32 index. motion._bl: "_BLB", u32
// bone count, then 104-byte entries: u32 0, i32 parent (-1 root), u32
// index, u32 field (0..3), u32 0, i32 field (-1 mostly), block A rotation
// xyzw, translation, scale, block B rotation xyzw, translation, scale, all
// f32.
//
// Contract.
//   read_skeleton fills SkeletonAssetData from the two records when both
//   exist; the counts must agree and each entry's index must equal its
//   position; parent must be -1 or below the index, with exactly one root.
//   The bind block is left Unknown by this reader and set by the caller
//   from the established fact (conversion.hpp). NoSkeleton is returned
//   when the container has neither record (230 of 857 model files), and
//   is not a failure of the mesh.
//
// Core guidelines read: SL.con.3, C.183, E.27 (as cpr_container.hpp).
namespace bw::content_converter {

inline constexpr std::size_t kBilEntryBytes = 40;
inline constexpr std::size_t kBlEntryBytes = 104;

enum class SkeletonReadError : std::uint8_t {
  None,
  NoSkeleton,
  // One of the two records exists without the other.
  RecordPairIncomplete,
  BadMagic,
  SizeMismatch,
  CountMismatch,
  IndexMismatch,
  ParentInvalid,
  RootCountInvalid,
  NameInvalid,
};

struct SkeletonReadFailure {
  SkeletonReadError error{SkeletonReadError::None};
  std::uint32_t index{};
  [[nodiscard]] bool ok() const noexcept { return error == SkeletonReadError::None; }
};

[[nodiscard]] SkeletonReadFailure read_skeleton(const CprContainer& container,
                                                SkeletonAssetData& out);

} // namespace bw::content_converter

#endif
