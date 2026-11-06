// Unit test: public contracts of bw/content/vdf.hpp; each scenario names the
// declarations it asserts.
#include "bw/content/vdf.hpp"
#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace {

using bw::content::VdfDocument;
using bw::content::VdfError;
using bw::content::VdfNode;
using bw::content::VdfOptions;
using bw::content::VdfParse;
using bw::testing::require;
using E = VdfError;

// A document as one line of text: directives as "#name:argument;", then
// root pairs joined by ';'. A pair is its key, "@<conditional>" when it has
// one, then "=<value>" or "{<members joined by ','>}".
std::string render(const VdfNode& node) {
  std::string text = node.key;
  if (!node.conditional.empty()) {
    text += "@" + node.conditional;
  }
  if (!node.has_children) {
    return text + "=" + node.value;
  }
  text += "{";
  for (std::size_t i = 0; i < node.children.size(); ++i) {
    text += (i == 0 ? "" : ",") + render(node.children[i]);
  }
  return text + "}";
}

std::string render(const VdfDocument& document) {
  std::string text;
  for (const auto& directive : document.directives) {
    text += directive.name + ":" + directive.argument + ";";
  }
  for (std::size_t i = 0; i < document.roots.size(); ++i) {
    text += (i == 0 ? "" : ";") + render(document.roots[i]);
  }
  return text;
}

// "r {" then `blocks - 1` nested "k {" lines, closed again: `blocks` blocks
// deep counting the root's.
std::string nested(std::size_t blocks) {
  std::string text = "r {\n";
  for (std::size_t i = 1; i < blocks; ++i) {
    text += "k {\n";
  }
  for (std::size_t i = 0; i < blocks; ++i) {
    text += "}\n";
  }
  return text;
}

// (vdf.hpp, parse_vdf): comments to end of line where a token may start;
// quoted and bare tokens and their delimiters; braces; nesting; duplicate
// keys kept in order; a conditional between a key and its value recorded and
// not evaluated; directives before the first pair recorded in order, names
// lower-cased; escapes left as written by default and translated when on;
// tokens shorter than kVdfMaxTokenBytes; blocks up to kVdfMaxDepth deep; the
// text ends at a NUL byte.
void grammar_table() {
  const VdfOptions escapes{.escapes = true};
  struct Row {
    std::string text;
    VdfOptions options;
    std::string expected;
    std::string_view note;
  };
  const std::string long_token(bw::content::kVdfMaxTokenBytes - 1U, 'x');
  const std::array rows{
      Row{R"("a" { "k" "v" })", {}, "a{k=v}", "quoted tokens"},
      Row{"a { k v }", {}, "a{k=v}", "bare tokens"},
      Row{"a{k\"v\"}", {}, "a{k=v}", "a bare token ends at '\"', '{' and '}'"},
      Row{"a\t{\r\n\vk\fv\n}", {}, "a{k=v}", "every whitespace byte"},
      Row{"// head\n\"a\" // one\n{ // two\n k v // three\n}// end", {}, "a{k=v}", "comments"},
      Row{"a { k a//b }", {}, "a{k=a//b}", "'//' inside a bare token is text"},
      Row{"a { k \"a // b\" }", {}, "a{k=a // b}", "'//' inside a quoted token is text"},
      Row{R"(a { "{" "}" k "" })", {}, "a{{=},k=}", "quoted braces and the empty value are text"},
      Row{"a { s { x 1 } s { x 2 } k v k w }",
          {},
          "a{s{x=1},s{x=2},k=v,k=w}",
          "nesting; duplicate keys kept in order"},
      Row{"a { } b { k v }", {}, "a{};b{k=v}", "several root pairs"},
      Row{"a [$X] { k [$WIN32] v s [!$OSX] { } }",
          {},
          "a@[$X]{k@[$WIN32]=v,s@[!$OSX]{}}",
          "conditionals recorded on root, value and section"},
      Row{"#base \"x.vdf\"\n#include y.vdf\n#BASE z\na { }",
          {},
          "#base:x.vdf;#include:y.vdf;#base:z;a{}",
          "directives in order, names lower-cased"},
      Row{R"(a { k "C:\\Steam\n" q "x\q" })",
          {},
          R"(a{k=C:\\Steam\n,q=x\q})",
          "escapes off by default: backslashes as written"},
      Row{R"(a { k "C:\\Steam\n\t\v\b\r\f\a\?\'\"" })", escapes, "a{k=C:\\Steam\n\t\v\b\r\f\a?'\"}",
          "escapes on: the whole set"},
      Row{R"(a { k "say \"hi\"" })", escapes, "a{k=say \"hi\"}", "an escaped quote ends nothing"},
      Row{"a { k \"" + long_token + "\" " + long_token + " v }",
          {},
          "a{k=" + long_token + "," + long_token + "=v}",
          "tokens one byte shorter than kVdfMaxTokenBytes"},
      Row{std::string("a { k v }\0b {", 13), {}, "a{k=v}", "the text ends at a NUL byte"},
      Row{"", {}, "", "an empty document"},
      Row{"// only a comment", {}, "", "a document of comments"},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const VdfParse parse = bw::content::parse_vdf(rows[i].text, rows[i].options);
    require(parse.ok(), i, std::string(rows[i].note) + ": parses");
    require(render(parse.document) == rows[i].expected, i, rows[i].note);
  }
  const VdfParse deepest = bw::content::parse_vdf(nested(bw::content::kVdfMaxDepth));
  require(deepest.ok(), "blocks kVdfMaxDepth deep parse");
}

// (vdf.hpp, VdfError): each failure named at the 1-based line where its token
// starts, or the last line for an end of text.
void errors_named_with_line() {
  const VdfOptions escapes{.escapes = true};
  const std::string too_long(bw::content::kVdfMaxTokenBytes, 'x');
  struct Row {
    std::string text;
    VdfOptions options;
    E expected;
    std::size_t line;
    std::string_view note;
  };
  const std::array rows{
      Row{"a {\n k v\n", {}, E::UnexpectedEnd, 3, "end where a key was due"},
      Row{"a {\n \"\" v\n}", {}, E::EmptyKey, 2, "an empty key"},
      Row{"a {\n k }", {}, E::UnexpectedCloseBrace, 2, "'}' where a value was due"},
      Row{"a\n k v", {}, E::MissingOpenBrace, 2, "a root key followed by a token"},
      Row{"a {\n k [$X]\n [$Y] v }",
          {},
          E::ConditionalBeforeValue,
          3,
          "a second conditional where the value was due"},
      Row{"a {\n k \"abc\n}\n", {}, E::UnterminatedString, 2, "no closing quote"},
      Row{"a {\n k \"" + too_long + "\" }",
          {},
          E::TokenTooLong,
          2,
          "a quoted token of kVdfMaxTokenBytes"},
      Row{"a {\n k \"x\n\\q\" }", escapes, E::InvalidEscape, 3, "an escape outside the set"},
      Row{nested(bw::content::kVdfMaxDepth + 1U),
          {},
          E::NestingTooDeep,
          bw::content::kVdfMaxDepth + 1U,
          "one block deeper than kVdfMaxDepth"},
      Row{"\n#base", {}, E::DirectiveWithoutArgument, 2, "a directive at the end"},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const VdfParse parse = bw::content::parse_vdf(rows[i].text, rows[i].options);
    require(parse.error == rows[i].expected, i, rows[i].note);
    require(parse.line == rows[i].line, i, std::string(rows[i].note) + ": line");
  }
  require(bw::content::parse_vdf("a { k v }").line == 0U, "no line without a failure");
}

// (vdf.hpp, find_child, find_root): keys compare ignoring ASCII case; the
// first match in document order wins; an absent key is null.
void find_child_ignores_case() {
  const VdfParse parse = bw::content::parse_vdf(
      "AppState { appid 696180 InstallDir \"Bullet Witch\" installdir second }\n"
      "appstate { appid 1 }");
  require(parse.ok(), "fixture parses");
  const VdfNode* root = bw::content::find_root(parse.document, "APPSTATE");
  require(root != nullptr && root->key == "AppState", "the first root, any case");
  const VdfNode* dir = bw::content::find_child(*root, "installdir");
  require(dir != nullptr && dir->value == "Bullet Witch", "the first child, any case");
  require(bw::content::find_child(*root, "INSTALLDIR") == dir, "upper case finds the same");
  require(bw::content::find_child(*root, "install") == nullptr, "a prefix is not a match");
  require(bw::content::find_child(*root, "missing") == nullptr, "an absent key");
  require(bw::content::find_root(parse.document, "other") == nullptr, "an absent root");
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array kScenarios{
    Scenario{"grammar_table", &grammar_table},
    Scenario{"errors_named_with_line", &errors_named_with_line},
    Scenario{"find_child_ignores_case", &find_child_ignores_case},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
