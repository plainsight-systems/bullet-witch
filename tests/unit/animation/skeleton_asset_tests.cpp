// Unit test: public contracts of bw/animation/skeleton_asset.hpp; each
// scenario names the declaration it asserts. Payloads are built here by
// copying the header's records to the 16-aligned offsets it states.
#include "bw/animation/skeleton_asset.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using bw::animation::BindBlock;
using bw::animation::BoneRecord;
using bw::animation::SkeletonPayloadHeader;
using bw::testing::require;
using E = bw::animation::SkeletonAssetError;

using Bytes = std::vector<std::byte>;

void pad16(Bytes& bytes) { bytes.resize((bytes.size() + 15U) & ~std::size_t{15}); }

template <class T> T read_at(const Bytes& bytes, std::size_t offset) {
  T value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}
template <class T> void write_at(Bytes& bytes, std::size_t offset, const T& value) {
  std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

// A chain of `count` bones (bone k's parent is k - 1), bind block B, every
// field distinct per bone so a read-back of the wrong record fails; bone 2
// carries a name of exactly kMaxBoneNameBytes bytes.
struct Skeleton {
  std::vector<BoneRecord> bones;
  std::vector<std::string> names;
};

Skeleton make_skeleton(std::uint32_t count) {
  Skeleton s;
  for (std::uint32_t k = 0; k < count; ++k) {
    std::string name =
        k == 2U ? std::string(bw::animation::kMaxBoneNameBytes, 'n') : "bone_" + std::to_string(k);
    BoneRecord bone{};
    bone.id = 100U + k;
    bone.parent = static_cast<std::int32_t>(k) - 1;
    bone.field_12 = k % 4U;
    bone.field_20 = -1 - static_cast<std::int32_t>(k);
    const auto f = static_cast<float>(k);
    bone.a = {{0.0F, 0.0F, 0.0F, 1.0F}, {f, 2.0F * f, 3.0F * f}, {1.0F, 1.0F, 1.0F}};
    bone.b = {{0.5F, 0.5F, 0.5F, 0.5F}, {-f, -2.0F * f, -3.0F * f}, {2.0F, 2.0F, 2.0F}};
    s.bones.push_back(bone);
    s.names.push_back(std::move(name));
  }
  return s;
}

bool same_transform(const bw::animation::BoneTransformRecord& x,
                    const bw::animation::BoneTransformRecord& y) {
  return x.rotation == y.rotation && x.translation == y.translation && x.scale == y.scale;
}

// Header at 0, bone records at the next 16-byte boundary, then the string
// table; the header's counts and offsets are filled from the layout.
Bytes lay_out(Skeleton s, BindBlock bind) {
  SkeletonPayloadHeader header{};
  header.bone_count = static_cast<std::uint32_t>(s.bones.size());
  header.bind_block = static_cast<std::uint32_t>(bind);
  std::string strings;
  for (std::size_t k = 0; k < s.bones.size(); ++k) {
    s.bones[k].name_offset = static_cast<std::uint32_t>(strings.size());
    s.bones[k].name_length = static_cast<std::uint32_t>(s.names[k].size());
    strings += s.names[k];
  }
  header.string_bytes = static_cast<std::uint32_t>(strings.size());
  Bytes bytes(sizeof(SkeletonPayloadHeader));
  pad16(bytes);
  header.bones_offset = static_cast<std::uint32_t>(bytes.size());
  for (const BoneRecord& bone : s.bones) {
    bytes.resize(bytes.size() + sizeof(BoneRecord));
    write_at(bytes, bytes.size() - sizeof(BoneRecord), bone);
  }
  pad16(bytes);
  header.strings_offset = static_cast<std::uint32_t>(bytes.size());
  for (const char c : strings) {
    bytes.push_back(static_cast<std::byte>(c));
  }
  pad16(bytes);
  write_at(bytes, 0, header);
  return bytes;
}

void patch_header(Bytes& bytes, const std::function<void(SkeletonPayloadHeader&)>& change) {
  auto header = read_at<SkeletonPayloadHeader>(bytes, 0);
  change(header);
  write_at(bytes, 0, header);
}
void patch_bone(Bytes& bytes, std::uint32_t k, const std::function<void(BoneRecord&)>& change) {
  const auto header = read_at<SkeletonPayloadHeader>(bytes, 0);
  const std::size_t offset = header.bones_offset + (std::size_t{k} * sizeof(BoneRecord));
  auto bone = read_at<BoneRecord>(bytes, offset);
  change(bone);
  write_at(bytes, offset, bone);
}

// (skeleton_asset.hpp, decode_skeleton_asset, SkeletonAssetError order,
// SkeletonAssetView): a valid payload reads back every bone record, name and
// the bind block as stored, including the kMaxBones and kMaxBoneNameBytes
// bounds and BindBlock::Unknown (an enumerator, read back for the consumer
// to refuse); each row then breaks one rule, or two where the order decides,
// and names the first failure in the enumeration's order with its index.
void rejections_named() {
  const Skeleton skeleton = make_skeleton(3);
  const Bytes valid = lay_out(skeleton, BindBlock::B);
  {
    bw::animation::SkeletonAssetView view;
    const auto failure = bw::animation::decode_skeleton_asset(valid, view);
    require(failure.error == E::None, "the valid payload decodes");
    require(view.bone_count() == 3U && view.bind_block() == BindBlock::B,
            "bone count and bind block read back");
    for (std::uint32_t k = 0; k < 3U; ++k) {
      const BoneRecord& expected = skeleton.bones[k];
      const BoneRecord actual = view.bone(k);
      require(actual.id == expected.id && actual.parent == expected.parent &&
                  actual.field_12 == expected.field_12 && actual.field_20 == expected.field_20 &&
                  same_transform(actual.a, expected.a) && same_transform(actual.b, expected.b),
              k, "id, parent, both fields and both transform blocks read back as stored");
      require(view.name(k) == skeleton.names[k], k, "name reads back");
    }
  }
  for (const auto& [bind, count] :
       {std::pair{BindBlock::Unknown, 3U}, std::pair{BindBlock::A, bw::animation::kMaxBones}}) {
    bw::animation::SkeletonAssetView view;
    const Bytes bytes = lay_out(make_skeleton(count), bind);
    require(bw::animation::decode_skeleton_asset(bytes, view).error == E::None &&
                view.bind_block() == bind && view.bone_count() == count,
            "Unknown and A bind blocks, and kMaxBones bones, decode and read back");
  }

  struct Row {
    std::string_view note;
    std::function<void(Bytes&)> change;
    E expected;
    std::uint32_t index;
  };
  const std::array rows{
      Row{"header one byte short", [](Bytes& b) { b.resize(sizeof(SkeletonPayloadHeader) - 1U); },
          E::HeaderTruncated, 0},
      Row{"strings offset off 16",
          [](Bytes& b) {
            patch_header(b, [](SkeletonPayloadHeader& h) { h.strings_offset += 8U; });
          },
          E::SectionMisaligned, 0},
      Row{"string table past the end",
          [](Bytes& b) {
            patch_header(b, [](SkeletonPayloadHeader& h) { h.string_bytes = 0x10000U; });
          },
          E::SectionOutOfBounds, 0},
      Row{"no bones",
          [](Bytes& b) { patch_header(b, [](SkeletonPayloadHeader& h) { h.bone_count = 0; }); },
          E::BoneCountInvalid, 0},
      Row{"two roots", [](Bytes& b) { patch_bone(b, 2, [](BoneRecord& r) { r.parent = -1; }); },
          E::RootCountInvalid, 0},
      Row{"a negative parent other than kNoParent",
          [](Bytes& b) { patch_bone(b, 2, [](BoneRecord& r) { r.parent = -2; }); },
          E::ParentOrderInvalid, 2},
      Row{"a name one byte over kMaxBoneNameBytes",
          [](Bytes& b) {
            patch_bone(b, 2, [](BoneRecord& r) {
              r.name_offset = 0;
              r.name_length = 32;
            });
          },
          E::NameInvalid, 2},
      Row{"a bind block that is no enumerator",
          [](Bytes& b) { patch_header(b, [](SkeletonPayloadHeader& h) { h.bind_block = 3; }); },
          E::BindBlockUnknown, 0},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    Bytes bytes = valid;
    rows[i].change(bytes);
    bw::animation::SkeletonAssetView view;
    const auto failure = bw::animation::decode_skeleton_asset(bytes, view);
    require(failure.error == rows[i].expected && failure.index == rows[i].index, i, rows[i].note);
  }
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"rejections_named", &rejections_named},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
