// Unit test: public contracts of bw/rendering/shaders/permutation.hpp, one
// scenario per CTest entry. Each scenario names the header declaration it asserts.
#include "bw/rendering/shaders/permutation.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace {
using bw::shaders::BlockLayout;
using bw::shaders::BoolDimension;
using bw::shaders::Define;
using bw::shaders::EnumDimension;
using bw::shaders::IntDimension;
using bw::shaders::PermutationDomain;
using bw::shaders::PermutationId;
using bw::shaders::ShaderDescription;
using bw::shaders::ShaderStage;

using bw::testing::require;

enum class Quality : std::uint8_t { Low, Medium, High, Ultra };

using Fog = BoolDimension<"TEST_FOG">;
using Taps = IntDimension<"TEST_TAPS", 3, -1>; // values -1, 0, 1
using Tier = EnumDimension<"TEST_QUALITY", Quality, 4>;
using TestDomain = PermutationDomain<Fog, Taps, Tier>;

// Excludes every permutation with fog on at Ultra quality.
struct TestShader {
  using Domain = TestDomain;
  static constexpr std::string_view name = "test_shader";
  static constexpr std::string_view source_path = "test/test_shader.hlsl";
  static constexpr std::string_view entry_point = "main";
  static constexpr ShaderStage stage = ShaderStage::Pixel;
  static constexpr std::span<const BlockLayout* const> static_blocks{};
  static constexpr const BlockLayout* draw_block = nullptr;
  [[nodiscard]] static constexpr bool should_compile(const Domain& domain) noexcept {
    return !(domain.get<Fog>() && domain.get<Tier>() == Quality::Ultra);
  }
};

// (permutation.hpp, PermutationDomain::count) and
// (permutation.hpp, PermutationDomain::from_id): count is the product of
// dimension counts and every id below it round-trips through from_id; get/set
// round-trip values.
void id_round_trip_and_count() {
  require(TestDomain::count == 2U * 3U * 4U, "count is the product of dimension counts");
  require(PermutationDomain<>::count == 1U, "the empty domain has one permutation");
  for (std::uint32_t id = 0; id < TestDomain::count; ++id) {
    require(TestDomain::from_id(PermutationId{id}).id() == PermutationId{id}, "id round trip");
  }
  TestDomain domain{};
  require(domain.id() == PermutationId{0}, "default domain is permutation 0");
  domain.set<Taps>(1);
  domain.set<Tier>(Quality::High);
  domain.set<Fog>(true);
  require(domain.get<Taps>() == 1 && domain.get<Tier>() == Quality::High && domain.get<Fog>(),
          "set then get returns the value");
  require(TestDomain::from_id(domain.id()) == domain, "set values round-trip through the id");
}

// (permutation.hpp, PermutationDomain::defines): one define per dimension, in declaration order,
// set to the dimension's define value.
void defines_one_per_dimension_in_order() {
  TestDomain domain{};
  domain.set<Fog>(true);
  domain.set<Taps>(0);
  domain.set<Tier>(Quality::Ultra);
  const std::array<Define, 3> defines = domain.defines();
  require(defines[0] == Define{"TEST_FOG", 1}, "first dimension first");
  require(defines[1] == Define{"TEST_TAPS", 0}, "integer dimension defines its value");
  require(defines[2] == Define{"TEST_QUALITY", 3}, "enum dimension defines its index");
  // The type-erased emitter writes the same list.
  const ShaderDescription description = bw::shaders::describe_shader<TestShader>();
  std::array<Define, 3> emitted{};
  require(description.emit_defines(domain.id(), emitted) == 3U, "emitter writes dimension_count");
  require(emitted == defines, "emitter equals defines()");
  require(description.dimension_count == 3U, "description carries dimension_count");
}

// (permutation.hpp, enumerate_permutations): strictly ascending ids below count,
// exactly those the predicate admits; all ids when the predicate is null.
void enumeration_ascending_excluded_absent() {
  const ShaderDescription description = bw::shaders::describe_shader<TestShader>();
  require(description.permutation_count == TestDomain::count, "description carries the count");
  std::array<PermutationId, TestDomain::count> ids{};
  const std::size_t written = bw::shaders::enumerate_permutations(description, ids);
  require(written == TestDomain::count - 3U, "fog at ultra excluded for each of three tap values");
  for (std::size_t i = 0; i < written; ++i) {
    require(ids[i].value < TestDomain::count, "ids below count");
    require(i == 0 || ids[i - 1].value < ids[i].value, "strictly ascending");
    const TestDomain domain = TestDomain::from_id(ids[i]);
    require(!(domain.get<Fog>() && domain.get<Tier>() == Quality::Ultra), "excluded id absent");
  }
  ShaderDescription unfiltered = description;
  unfiltered.should_compile = nullptr;
  require(bw::shaders::enumerate_permutations(unfiltered, ids) == TestDomain::count,
          "a null predicate admits every id");
  for (std::uint32_t i = 0; i < TestDomain::count; ++i) {
    require(ids[i] == PermutationId{i}, "every id in order");
  }
}

// (permutation.hpp, shader_registry) and (permutation.hpp, find_shader): over
// whatever the registry holds, names are unique,
// find_shader returns each entry by its name, and null for a name the
// registry does not hold. Nothing is assumed about which shaders it holds.
void registry_names_unique() {
  const std::span<const ShaderDescription> registry = bw::shaders::shader_registry();
  for (std::size_t i = 0; i < registry.size(); ++i) {
    for (std::size_t j = i + 1; j < registry.size(); ++j) {
      require(registry[i].name != registry[j].name, "names unique");
    }
    require(bw::shaders::find_shader(registry, registry[i].name) == &registry[i],
            "find_shader returns the entry with that name");
  }
  // A probe name chosen to differ from every registered name.
  std::string absent = "bw_test_absent_shader";
  const auto held = [&registry](std::string_view name) {
    for (const ShaderDescription& shader : registry) {
      if (shader.name == name) {
        return true;
      }
    }
    return false;
  };
  while (held(absent)) {
    absent += '_';
  }
  require(bw::shaders::find_shader(registry, absent) == nullptr,
          "find_shader returns null for a name not in the registry");
  // The same contract on a registry the test builds, so lookup is exercised
  // whatever the engine's registry holds.
  const std::array local{bw::shaders::describe_shader<TestShader>()};
  require(bw::shaders::find_shader(local, "test_shader") == local.data(),
          "find_shader finds a local entry by name");
  require(bw::shaders::find_shader(local, "other") == nullptr,
          "find_shader returns null on a local registry");
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array scenarios{
    Scenario{"id_round_trip_and_count", id_round_trip_and_count},
    Scenario{"defines_one_per_dimension_in_order", defines_one_per_dimension_in_order},
    Scenario{"enumeration_ascending_excluded_absent", enumeration_ascending_excluded_absent},
    Scenario{"registry_names_unique", registry_names_unique},
};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
