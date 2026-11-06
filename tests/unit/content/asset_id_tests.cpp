// Unit test: public contracts of bw/content/asset_id.hpp; each scenario names
// the declarations it asserts.
#include "bw/content/asset_id.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace {

using bw::content::AssetId;
using bw::content::AssetIdError;
using bw::content::AssetIdResult;
using bw::content::SplitKind;
using bw::testing::require;
using E = AssetIdError;

AssetId id_of(std::string_view text) {
  const AssetIdResult result = bw::content::make_asset_id(text);
  require(result.ok(), std::string("fixture id validates: ") + std::string(text));
  return result.id;
}

// (asset_id.hpp, validate_asset_id, make_asset_id, AssetIdError): each rule
// broken alone names its error, through the text overload, the byte
// overload and make_asset_id alike; the bounds themselves pass.
// make_asset_id folds nothing, so an upper-case byte is InvalidCharacter.
void validate_table() {
  struct Row {
    std::string text;
    E expected;
    std::string_view note;
  };
  const std::array rows{
      Row{"model.pw0.mesh", E::None, "a qualified name"},
      Row{"a", E::None, "one byte, one segment"},
      Row{"a-b_c.0-9", E::None, "every class of the alphabet"},
      Row{std::string(255, 'a') + "." + std::string(255, 'a'), E::None,
          "two segments at kMaxAssetIdSegmentBytes"},
      Row{std::string(255, 'a') + "." + std::string(254, 'b') + ".c", E::None,
          "exactly kMaxAssetIdBytes"},
      Row{"", E::Empty, "empty"},
      Row{std::string(255, 'a') + "." + std::string(255, 'b') + ".cd", E::TooLong,
          "kMaxAssetIdBytes + 1"},
      Row{".a", E::EmptySegment, "leading separator"},
      Row{"a." + std::string(256, 'b'), E::SegmentTooLong, "a 256-byte segment"},
      Row{"model.PW0.mesh", E::InvalidCharacter, "upper case is not folded"},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const Row& row = rows[i];
    const std::span<const std::byte> bytes(reinterpret_cast<const std::byte*>(row.text.data()),
                                           row.text.size());
    require(bw::content::validate_asset_id(row.text) == row.expected, i, row.note);
    require(bw::content::validate_asset_id(bytes) == row.expected, i,
            "bytes: " + std::string(row.note));
    const AssetIdResult made = bw::content::make_asset_id(row.text);
    require(made.error == row.expected, i, "make_asset_id: " + std::string(row.note));
    require(made.ok() ? made.id.view() == row.text : made.id.empty(), i,
            "an id holds its text exactly, or nothing on failure");
  }
}

// (asset_id.hpp, asset_id_from_qualified_name, asset_id_from_container_record,
// split_asset_id, is_remaster_id): the derivation rules as the contract
// states them, including the header's own example.
void derivation_table() {
  struct Row {
    AssetIdResult result;
    std::string_view expected;
    E error;
    std::string_view note;
  };
  const AssetId mesh = id_of("model.pw0.mesh");
  const AssetId long_parent = id_of(std::string(255, 'a') + "." + std::string(245, 'b'));
  const std::array rows{
      Row{bw::content::asset_id_from_qualified_name("model.pw0.mesh.bonelist"),
          "model.pw0.mesh.bonelist", E::None, "an already lower-case name is unchanged"},
      Row{bw::content::asset_id_from_qualified_name("model.PW0.texture.dds.PW0_body_m1_game_psd"),
          "model.pw0.texture.dds.pw0_body_m1_game_psd", E::None, "a CALL argument folds"},
      Row{bw::content::asset_id_from_qualified_name("model.pw0 mesh"), "", E::InvalidCharacter,
          "a byte outside the alphabet after folding"},
      Row{bw::content::asset_id_from_container_record("data/data/stable/sprite/title/title.cpr",
                                                      "title_3.png"),
          "data.data.stable.sprite.title.title.title_3", E::None, "the header's example"},
      Row{bw::content::asset_id_from_container_record("Data\\Sprite\\Title.CPR", "Title_3.PNG"),
          "data.sprite.title.title_3", E::None,
          "backslashes, upper-case path, extension and short name fold"},
      Row{bw::content::asset_id_from_container_record("data/effect/fx", "spark"),
          "data.effect.fx.spark", E::None, "no .cpr to drop; a short name without '.' is whole"},
      Row{bw::content::asset_id_from_container_record("data/effect/fx.cpr", "a.b.png"),
          "data.effect.fx.a.b", E::None, "only the text after the last '.' is the extension"},
      Row{bw::content::asset_id_from_container_record("data/my dir/fx.cpr", "a.png"), "",
          E::InvalidCharacter, "a byte outside the alphabet is not replaced"},
      Row{bw::content::split_asset_id(mesh, SplitKind::Material, 3), "model.pw0.mesh.material.3",
          E::None, "material suffix"},
      Row{bw::content::split_asset_id(mesh, SplitKind::Skeleton, 7), "model.pw0.mesh.skeleton",
          E::None, "skeleton ignores the index"},
      Row{bw::content::split_asset_id(mesh, SplitKind::Shapes, 0), "model.pw0.mesh.shapes.0",
          E::None, "shapes suffix, index unpadded"},
      Row{bw::content::split_asset_id(mesh, SplitKind::Material, 4294967295U),
          "model.pw0.mesh.material.4294967295", E::None, "decimal index"},
      Row{bw::content::split_asset_id(long_parent, SplitKind::Material, 10), "", E::TooLong,
          "a result over kMaxAssetIdBytes"},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const Row& row = rows[i];
    require(row.result.error == row.error, i, row.note);
    require(row.result.id.view() == row.expected, i, std::string(row.note) + ": text");
  }
  require(bw::content::is_remaster_id(id_of("remaster.alicia.hair")), "remaster. prefix");
  require(!bw::content::is_remaster_id(id_of("remaster")), "remaster without its separator");
  require(!bw::content::is_remaster_id(id_of("remasters.a")), "a longer first segment");
  require(!bw::content::is_remaster_id(id_of("model.remaster.a")), "remaster not first");
}

// (asset_id.hpp, asset_relative_path): segments joined by '/' plus the cooked
// extension, so an asset and the assets split from it coexist in one layer
// directory; the empty id has no path.
void relative_path() {
  require(bw::content::asset_relative_path(id_of("model.pw0.mesh")) == "model/pw0/mesh.bwc",
          "a mesh is a file");
  require(bw::content::asset_relative_path(id_of("model.pw0.mesh.material.3")) ==
              "model/pw0/mesh/material/3.bwc",
          "its split asset lives under a directory of the same stem");
  require(bw::content::asset_relative_path(id_of("a")) == "a.bwc", "one segment");
  require(bw::content::asset_relative_path(AssetId{}).empty(), "the empty id");
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"validate_table", &validate_table},
    Scenario{"derivation_table", &derivation_table},
    Scenario{"relative_path", &relative_path},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
