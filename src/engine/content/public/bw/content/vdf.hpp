#ifndef BW_CONTENT_VDF_HPP
#define BW_CONTENT_VDF_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: a pure parser for the text format Steam writes its
// libraryfolders.vdf and appmanifest_<id>.acf in ("VDF"), which the
// install search reads (docs/architecture/content.md, invariant 5).
//
// Contract.
//   Tokens: whitespace is skipped; "//" starts a comment to the end of the
//   line, anywhere a token may start; a '"' starts a quoted token that
//   runs to the next '"', with escapes translated only when
//   options.escapes is true (false by default); '{' and '}' are tokens of
//   their own; a bare token runs to whitespace, '"', '{' or '}'; a bare
//   token that starts with '[' is a conditional, and a key such as a[b]
//   stays a key, so nothing vanishes silently from files that carry no
//   conditionals; a token of kVdfMaxTokenBytes bytes or more is
//   TokenTooLong: it is refused, not truncated, because a truncated path
//   is a wrong path.
//   Escapes when enabled: \n \t \v \b \r \f \a \\ \? \' \"; any other
//   '\' pair is InvalidEscape.
//   Structure: a document is a sequence of root pairs; before the first,
//   "#include" and "#base" each followed by a token other than '{' or '}'
//   are recorded as directives in order and not followed, because the
//   parser has no file access; a brace there is DirectiveWithoutArgument.
//   A pair is a key token, an optional conditional, then either a value
//   token or '{' members '}'. Members are pairs. Duplicate keys are kept
//   in order. A conditional is recorded on the node as text and never
//   evaluated: Steam's library and manifest files carry none. Nesting
//   deeper than kVdfMaxDepth is NestingTooDeep.
//   Errors, each named at its 1-based line: UnexpectedEnd (the text ends
//   where a key or value was due), EmptyKey (at the root as well as inside
//   a block), UnexpectedCloseBrace (a '}' where a value was due, or a '}'
//   at the root), MissingOpenBrace (a root key followed by anything but
//   '{'), ConditionalBeforeValue (a conditional between a key and its
//   value), UnterminatedString (the text ends inside a quoted token),
//   TokenTooLong, InvalidEscape, NestingTooDeep, DirectiveWithoutArgument.
//   Lookup: find_child compares keys case-insensitively over ASCII; the
//   first match in document order wins.
//
// Core guidelines read: I.1 and Con.5 (a pure function of the text and
// the options), E.27 (one named code per failure with its line),
// SL.str.1 (nodes own their strings; a document outlives the text it was
// parsed from). Performance guidelines read: none applies to parsing two
// small files at startup; recorded rather than cited.
namespace bw::content {

inline constexpr std::size_t kVdfMaxTokenBytes = 4096;
inline constexpr std::size_t kVdfMaxDepth = 100;

struct VdfNode {
  std::string key{};
  // The value token when has_children is false.
  std::string value{};
  std::vector<VdfNode> children{};
  bool has_children{};
  // The conditional text, brackets included, or empty.
  std::string conditional{};
};

struct VdfDirective {
  // "#include" or "#base", lower-case.
  std::string name{};
  std::string argument{};
};

struct VdfDocument {
  std::vector<VdfNode> roots{};
  std::vector<VdfDirective> directives{};
};

struct VdfOptions {
  bool escapes{false};
};

enum class VdfError : std::uint8_t {
  None,
  UnexpectedEnd,
  EmptyKey,
  UnexpectedCloseBrace,
  MissingOpenBrace,
  ConditionalBeforeValue,
  UnterminatedString,
  TokenTooLong,
  InvalidEscape,
  NestingTooDeep,
  DirectiveWithoutArgument,
};

struct VdfParse {
  VdfDocument document{};
  VdfError error{VdfError::None};
  // 1-based line of the failure; 0 when none.
  std::size_t line{};
  [[nodiscard]] bool ok() const noexcept { return error == VdfError::None; }
};

[[nodiscard]] VdfParse parse_vdf(std::string_view text, VdfOptions options = {});

// The first child whose key equals `key` ignoring ASCII case, or null.
[[nodiscard]] const VdfNode* find_child(const VdfNode& node, std::string_view key) noexcept;
[[nodiscard]] const VdfNode* find_root(const VdfDocument& document, std::string_view key) noexcept;

} // namespace bw::content

#endif
