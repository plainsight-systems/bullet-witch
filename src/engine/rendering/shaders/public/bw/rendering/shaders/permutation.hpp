#ifndef BW_RENDERING_SHADERS_PERMUTATION_HPP
#define BW_RENDERING_SHADERS_PERMUTATION_HPP
#include "bw/rendering/shaders/parameter_block.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

// Responsibility: the finite permutation set of a shader and the shader's
// build-time identity (docs/architecture/shaders.md, "Permutations" and
// invariant 6). A shader declares dimensions; the domain is their product; a
// dense id maps to dimension values and back; each dimension sets one
// preprocessor define; a pure predicate excludes permutations; the tool
// compiles every permutation that remains. Consumers: shader_compiler
// (enumeration, defines), the RHI (ids in artifacts, precompilation set).
//
// Contract. Inputs: dimension types with a define name and a value range; a
// ShaderDeclaration type naming source, entry point, stage, domain, blocks
// and predicate. Outputs: PermutationDomain values, Define lists, a
// type-erased ShaderDescription, the enumerated id list. Preconditions:
// from_id requires id < count; get/set require the dimension to be in the
// domain (compile error otherwise); enumerate_permutations requires the
// output span to hold permutation_count ids. Postconditions, each a test:
// id round-trips through from_id for every id < count; count equals the
// product of dimension counts; defines() has one entry per dimension in
// declaration order; enumerate_permutations writes strictly ascending ids
// below count, exactly those the predicate admits. Failure: a description
// with no emitter or a precondition violation is a programming error, not a
// runtime path; the loader rejects an artifact whose id is out of range
// (artifact.hpp). Ownership: everything here is a value or a pointer to a
// static constexpr object; nothing is owned. No synchronization needed.
//
// Algorithm: a mixed-radix number with the first dimension least
// significant. id = i0 + c0 * (i1 + c1 * (i2 + ...)); from_id peels i_k = id
// mod c_k, id = id / c_k. Enumeration walks 0..count-1 and keeps the ids the
// predicate admits. The build-time flow is: enumerate per shader, emit the
// defines of each kept id, compile.
//
// Design and cost: counts and ids are compile-time constants where the
// domain type is known; the type-erased description costs two indirect
// calls per permutation at build time and none at runtime. kMaxPermutations
// bounds the build matrix so a mistaken dimension cannot multiply the shader
// build silently.
//
// Core guidelines read: I.1 and I.9/T.10 (dimensions are concept-checked
// template parameters; nothing ambient), Enum.3 (ShaderStage), Con.5 and
// T.123 (constexpr domain arithmetic), F.6 (pure arithmetic is noexcept),
// R.14 (spans for the emitter and enumeration output). Performance
// guidelines read: EMB.6 (the domain is compile-time data; the enumeration
// runs in the build tool), GDSA.17 (the permutation set is the key the
// precompiled pipeline set is built against; it is complete by
// construction). A search of the performance corpus for build-matrix or
// shader-variant rules returned no applicable rule; recorded here.
namespace bw::shaders {

// A dense permutation id, below the domain's count.
struct PermutationId {
  std::uint32_t value{};
  [[nodiscard]] friend constexpr bool operator==(PermutationId, PermutationId) noexcept = default;
};

// One preprocessor define emitted for one dimension.
struct Define {
  std::string_view name{};
  std::int32_t value{};
  [[nodiscard]] friend constexpr bool operator==(const Define&, const Define&) noexcept = default;
};

// Bounds on the build matrix. A domain beyond either fails to compile.
inline constexpr std::size_t kMaxDimensions = 8;
inline constexpr std::uint32_t kMaxPermutations = 1024;

// A define name as a non-type template argument: BoolDimension<"USE_FOG">.
template <std::size_t N> struct DefineName {
  char text[N]{};
  consteval DefineName(const char (&literal)[N]) noexcept { // NOLINT(google-explicit-constructor)
    for (std::size_t i = 0; i < N; ++i) {
      text[i] = literal[i];
    }
  }
  [[nodiscard]] constexpr std::string_view view() const noexcept { return {text, N - 1}; }
};

// What a dimension provides. count values, indexed 0..count-1; to_index and
// from_index are inverse on that range; define_value is what the define is
// set to for a value.
template <class D>
concept PermutationDimension = requires(typename D::value_type value, std::uint32_t index) {
  { D::define } -> std::convertible_to<std::string_view>;
  { D::count } -> std::convertible_to<std::uint32_t>;
  { D::to_index(value) } -> std::same_as<std::uint32_t>;
  { D::from_index(index) } -> std::same_as<typename D::value_type>;
  { D::define_value(value) } -> std::same_as<std::int32_t>;
};

// Off is index 0 and define value 0; on is index 1 and define value 1.
template <DefineName Name> struct BoolDimension {
  using value_type = bool;
  static constexpr std::string_view define = Name.view();
  static constexpr std::uint32_t count = 2;
  [[nodiscard]] static constexpr std::uint32_t to_index(bool value) noexcept {
    return value ? 1U : 0U;
  }
  // Precondition: index < count.
  [[nodiscard]] static constexpr bool from_index(std::uint32_t index) noexcept {
    return index == 1U;
  }
  [[nodiscard]] static constexpr std::int32_t define_value(bool value) noexcept {
    return value ? 1 : 0;
  }
};

// Count contiguous values from First; the index is value - First and the
// define value is the value itself.
template <DefineName Name, std::int32_t Count, std::int32_t First = 0>
  requires(Count > 0)
struct IntDimension {
  using value_type = std::int32_t;
  static constexpr std::string_view define = Name.view();
  static constexpr std::uint32_t count = static_cast<std::uint32_t>(Count);
  // Precondition: First <= value < First + Count.
  [[nodiscard]] static constexpr std::uint32_t to_index(std::int32_t value) noexcept {
    return static_cast<std::uint32_t>(value - First);
  }
  // Precondition: index < count.
  [[nodiscard]] static constexpr std::int32_t from_index(std::uint32_t index) noexcept {
    return First + static_cast<std::int32_t>(index);
  }
  [[nodiscard]] static constexpr std::int32_t define_value(std::int32_t value) noexcept {
    return value;
  }
};

// Count is given explicitly; the enumerators must be contiguous from 0.
template <DefineName Name, class Enum, std::uint32_t Count>
  requires(std::is_enum_v<Enum> && Count > 0)
struct EnumDimension {
  using value_type = Enum;
  static constexpr std::string_view define = Name.view();
  static constexpr std::uint32_t count = Count;
  // Precondition: the enumerator's value is below Count.
  [[nodiscard]] static constexpr std::uint32_t to_index(Enum value) noexcept {
    return static_cast<std::uint32_t>(static_cast<std::underlying_type_t<Enum>>(value));
  }
  // Precondition: index < count.
  [[nodiscard]] static constexpr Enum from_index(std::uint32_t index) noexcept {
    return static_cast<Enum>(index);
  }
  [[nodiscard]] static constexpr std::int32_t define_value(Enum value) noexcept {
    return static_cast<std::int32_t>(static_cast<std::underlying_type_t<Enum>>(value));
  }
};

namespace detail {
template <class D, class... Dims>
inline constexpr std::size_t dimension_index = [] {
  std::size_t index = 0;
  std::size_t found = sizeof...(Dims);
  ((std::is_same_v<D, Dims> ? (found = index, ++index) : ++index), ...);
  return found;
}();
} // namespace detail

// The count is the product of the dimension counts, and 1 for the empty
// domain. The first dimension is least significant: from an id, the first
// dimension takes id % count and the tail id / count; id() is index + count *
// tail id. defines() sets one define per dimension, name to define value.
template <PermutationDimension... Dims> class PermutationDomain {
public:
  static constexpr std::size_t dimension_count = sizeof...(Dims);
  static constexpr std::uint32_t count = (1U * ... * Dims::count);
  static_assert(dimension_count <= kMaxDimensions, "too many permutation dimensions");
  static_assert(count <= kMaxPermutations, "permutation domain exceeds kMaxPermutations");

  // Every dimension at index 0.
  constexpr PermutationDomain() noexcept = default;

  // Precondition: id.value < count. Inverse of id().
  [[nodiscard]] static constexpr PermutationDomain from_id(PermutationId id) noexcept {
    PermutationDomain domain{};
    std::uint32_t rest = id.value;
    std::size_t k = 0;
    ((domain.indices_[k] = rest % Dims::count, rest /= Dims::count, ++k), ...);
    return domain;
  }

  [[nodiscard]] constexpr PermutationId id() const noexcept {
    std::uint32_t result = 0;
    std::uint32_t radix = 1;
    std::size_t k = 0;
    ((result += indices_[k] * radix, radix *= Dims::count, ++k), ...);
    return PermutationId{result};
  }

  template <PermutationDimension D>
    requires(detail::dimension_index<D, Dims...> < sizeof...(Dims))
  [[nodiscard]] constexpr typename D::value_type get() const noexcept {
    return D::from_index(indices_[detail::dimension_index<D, Dims...>]);
  }

  template <PermutationDimension D>
    requires(detail::dimension_index<D, Dims...> < sizeof...(Dims))
  constexpr void set(typename D::value_type value) noexcept {
    indices_[detail::dimension_index<D, Dims...>] = D::to_index(value);
  }

  // One define per dimension, in declaration order.
  [[nodiscard]] constexpr std::array<Define, dimension_count> defines() const noexcept {
    std::array<Define, dimension_count> result{};
    std::size_t k = 0;
    ((result[k] = Define{Dims::define, Dims::define_value(Dims::from_index(indices_[k]))}, ++k),
     ...);
    return result;
  }

  [[nodiscard]] friend constexpr bool operator==(const PermutationDomain&,
                                                 const PermutationDomain&) noexcept = default;

private:
  std::array<std::uint32_t, dimension_count> indices_{};
};

// Pipeline stages the game builds. Values are this format's own.
// The game's passes are vertex, pixel and compute.
enum class ShaderStage : std::uint8_t {
  Vertex = 0,
  Pixel = 1,
  Compute = 2,
};

// Writes the defines of permutation `id` into `out` and returns the number
// written, equal to the domain's dimension_count. Precondition: out.size() >=
// dimension_count and id below the count.
using DefineEmitter = std::size_t (*)(PermutationId id, std::span<Define> out) noexcept;
// True when the permutation is compiled. Pure: the same id gives the same
// answer on every host.
// One platform floor per target is fixed in docs/architecture/shaders.md, so
// the predicate sees the id alone.
using ExclusionPredicate = bool (*)(PermutationId id) noexcept;

// The type-erased identity the tool and the RHI share. name is unique in the
// registry and is the artifact's identity (artifact.hpp). source_path is
// relative to the shader source root with forward slashes. static_blocks
// are the blocks the shader may bind from static slots and draw_block the
// one per-draw block, or null; the generated include declares exactly these
// and the tool rejects a static block that the root layout does not place.
struct ShaderDescription {
  std::string_view name{};
  std::string_view source_path{};
  std::string_view entry_point{};
  std::span<const BlockLayout* const> static_blocks{};
  const BlockLayout* draw_block{};
  DefineEmitter emit_defines{};
  ExclusionPredicate should_compile{};
  std::uint32_t permutation_count{1};
  std::uint32_t dimension_count{};
  ShaderStage stage{ShaderStage::Vertex};
};

// A shader as declared in C++: a type with these static members.
template <class S>
concept ShaderDeclaration = requires(const typename S::Domain& domain) {
  typename S::Domain;
  { S::name } -> std::convertible_to<std::string_view>;
  { S::source_path } -> std::convertible_to<std::string_view>;
  { S::entry_point } -> std::convertible_to<std::string_view>;
  { S::stage } -> std::convertible_to<ShaderStage>;
  { S::static_blocks } -> std::convertible_to<std::span<const BlockLayout* const>>;
  { S::draw_block } -> std::convertible_to<const BlockLayout*>;
  { S::should_compile(domain) } -> std::same_as<bool>;
};

// The description of a declaration, with the domain erased behind the two
// function pointers.
template <ShaderDeclaration S>
[[nodiscard]] constexpr ShaderDescription describe_shader() noexcept {
  using Domain = typename S::Domain;
  return ShaderDescription{
      .name = S::name,
      .source_path = S::source_path,
      .entry_point = S::entry_point,
      .static_blocks = S::static_blocks,
      .draw_block = S::draw_block,
      .emit_defines = [](PermutationId id, std::span<Define> out) noexcept -> std::size_t {
        const auto defines = Domain::from_id(id).defines();
        for (std::size_t k = 0; k < defines.size() && k < out.size(); ++k) {
          out[k] = defines[k];
        }
        return defines.size();
      },
      .should_compile = [](PermutationId id) noexcept -> bool {
        return S::should_compile(Domain::from_id(id));
      },
      .permutation_count = Domain::count,
      .dimension_count = static_cast<std::uint32_t>(Domain::dimension_count),
      .stage = S::stage,
  };
}

// Writes, in strictly ascending order, every id below permutation_count that
// should_compile admits (all of them when should_compile is null), and
// returns how many. Precondition: out.size() >= permutation_count. The
// result is the complete build set of the shader: finite, and every id in it
// round-trips through the domain.
[[nodiscard]] std::size_t enumerate_permutations(const ShaderDescription& shader,
                                                 std::span<PermutationId> out) noexcept;

// Every shader the engine builds, in registry order; names unique. Defined
// by the shader module as one constexpr table; a renderer slice adds a shader
// by adding an entry there and its HLSL under the shader root.
[[nodiscard]] std::span<const ShaderDescription> shader_registry() noexcept;

// The entry with that name, or null.
[[nodiscard]] const ShaderDescription* find_shader(std::span<const ShaderDescription> registry,
                                                   std::string_view name) noexcept;

} // namespace bw::shaders
#endif
