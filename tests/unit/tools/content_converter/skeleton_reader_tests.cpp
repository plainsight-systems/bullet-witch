// Unit test: public contracts of bw/content_converter/skeleton_reader.hpp;
// the scenario names the declaration it asserts. The two records are built
// here from the header's stated framing.
#include "bw/animation/skeleton_asset.hpp"
#include "bw/content_converter/asset_data.hpp"
#include "bw/content_converter/cpr_container.hpp"
#include "bw/content_converter/skeleton_reader.hpp"
#include "bw/testing/cpr_fixture.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace cc = bw::content_converter;
namespace fx = bw::testing::cpr;
using bw::testing::require;
using fx::Bytes;

struct BoneSpec {
  std::uint32_t id;
  std::string name;
  std::int32_t parent;
  std::uint32_t field_12;
  std::int32_t field_20;
};

struct Spec {
  std::vector<BoneSpec> bones{{0x9461, "N_ALL", -1, 0, -1},
                              {0x97AF, "Waist_direction", 0, 1, -1},
                              {0xE69C, "HipsBone", 1, 2, 3}};
  bool with_bil = true;
  bool with_bl = true;
  std::string bil_magic = "MBDB";
  std::size_t bil_extra = 0;
  std::uint32_t bl_count_word = 3;
  std::uint32_t bl_index_of_1 = 1;
};

float value(std::size_t bone, std::size_t field) {
  return static_cast<float>(bone) + (0.125F * static_cast<float>(field));
}

// motion.bil: "MBDB", count, 40-byte entries of id, 32-byte name, index.
// motion._bl: "_BLB", count, 104-byte entries of 0, parent, index, field,
// 0, field, then twenty floats (block A then block B: rotation xyzw,
// translation, scale).
std::vector<fx::Record> records(const Spec& s) {
  Bytes bil = fx::text_bytes(s.bil_magic);
  fx::append_u32(bil, static_cast<std::uint32_t>(s.bones.size()));
  Bytes bl = fx::text_bytes("_BLB");
  fx::append_u32(bl, s.bl_count_word);
  for (std::size_t k = 0; k < s.bones.size(); ++k) {
    const BoneSpec& b = s.bones[k];
    fx::append_u32(bil, b.id);
    Bytes name(32);
    for (std::size_t i = 0; i < b.name.size() && i < name.size(); ++i) {
      name[i] = static_cast<std::byte>(b.name[i]);
    }
    bil.insert(bil.end(), name.begin(), name.end());
    fx::append_u32(bil, static_cast<std::uint32_t>(k));
    fx::append_u32(bl, 0);
    fx::append_u32(bl, static_cast<std::uint32_t>(b.parent));
    fx::append_u32(bl, k == 1U ? s.bl_index_of_1 : static_cast<std::uint32_t>(k));
    fx::append_u32(bl, b.field_12);
    fx::append_u32(bl, 0);
    fx::append_u32(bl, static_cast<std::uint32_t>(b.field_20));
    for (std::size_t f = 0; f < 20; ++f) {
      fx::append_f32(bl, value(k, f));
    }
  }
  bil.resize(bil.size() + s.bil_extra);
  std::vector<fx::Record> r;
  r.push_back({"USER", "header.bin", "", Bytes(8), Bytes(16), {}, {}});
  if (s.with_bil) {
    r.push_back({"USER", "motion.bil", "", bil, Bytes(16), {}, {}});
  }
  if (s.with_bl) {
    r.push_back({"USER", "motion._bl", "model.pw0.mesh.bonelist", bl, Bytes(16), {}, {}});
  }
  return r;
}

cc::SkeletonReadFailure read(const Spec& s, cc::SkeletonAssetData& out) {
  const Bytes file = fx::build(records(s));
  cc::CprContainer container;
  require(cc::parse_cpr(file, container) == cc::CprError::None, "the container parses");
  return cc::read_skeleton(container, out);
}

struct ErrorRow {
  std::string_view name;
  std::function<void(Spec&)> change;
  cc::SkeletonReadError expected;
};

// (skeleton_reader.hpp, read_skeleton, SkeletonReadError order)
void synthetic_pair_and_errors() {
  cc::SkeletonAssetData out;
  require(read(Spec{}, out).ok(), "the pair reads");
  require(out.bind_block == bw::animation::BindBlock::Unknown,
          "the reader leaves the bind block for the caller");
  require(out.bones.size() == 3U, "one bone per entry");
  const Spec spec;
  for (std::size_t k = 0; k < out.bones.size(); ++k) {
    const cc::BoneData& b = out.bones[k];
    const BoneSpec& e = spec.bones[k];
    require(b.name == e.name && b.id == e.id && b.parent == e.parent && b.field_12 == e.field_12 &&
                b.field_20 == e.field_20,
            k, "name, id, parent and the two fields as stored");
    require(b.a.rotation[0] == value(k, 0) && b.a.rotation[3] == value(k, 3) &&
                b.a.translation[0] == value(k, 4) && b.a.scale[2] == value(k, 9),
            k, "block A");
    require(b.b.rotation[0] == value(k, 10) && b.b.translation[2] == value(k, 16) &&
                b.b.scale[0] == value(k, 17) && b.b.scale[2] == value(k, 19),
            k, "block B");
  }

  using E = cc::SkeletonReadError;
  const std::array<ErrorRow, 8> rows{{
      {"no records",
       [](Spec& s) {
         s.with_bil = false;
         s.with_bl = false;
       },
       E::NoSkeleton},
      {"one record", [](Spec& s) { s.with_bl = false; }, E::RecordPairIncomplete},
      {"magic", [](Spec& s) { s.bil_magic = "MBDX"; }, E::BadMagic},
      {"size", [](Spec& s) { s.bil_extra = 4; }, E::SizeMismatch},
      {"index", [](Spec& s) { s.bl_index_of_1 = 7; }, E::IndexMismatch},
      {"parent after child", [](Spec& s) { s.bones[1].parent = 2; }, E::ParentInvalid},
      {"two roots", [](Spec& s) { s.bones[2].parent = -1; }, E::RootCountInvalid},
      {"empty name", [](Spec& s) { s.bones[2].name.clear(); }, E::NameInvalid},
  }};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    Spec s;
    rows[i].change(s);
    cc::SkeletonAssetData data;
    require(read(s, data).error == rows[i].expected, i, rows[i].name);
  }
  // Counts that each match their own record but differ between them: a
  // two-bone motion.bil beside the three-bone motion._bl.
  Spec two;
  two.bones.pop_back();
  two.bl_count_word = 2;
  std::vector<fx::Record> mixed = records(two);
  mixed[2] = records(Spec{})[2];
  const Bytes file = fx::build(mixed);
  cc::CprContainer container;
  require(cc::parse_cpr(file, container) == cc::CprError::None, "the container parses");
  cc::SkeletonAssetData data;
  require(cc::read_skeleton(container, data).error == E::CountMismatch,
          "two well-formed records of different counts");
}

constexpr std::array<bw::testing::Scenario<void>, 1> kScenarios{{
    {"synthetic_pair_and_errors", synthetic_pair_and_errors},
}};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
