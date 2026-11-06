#include "bw/content/vdf.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace bw::content {

namespace {

enum class TokenKind : std::uint8_t {
  End,
  Quoted,
  Bare,
  Conditional,
  OpenBrace,
  CloseBrace,
};

struct Token {
  TokenKind kind{TokenKind::End};
  std::string text{};
  // 1-based line where the token starts, or the last line for End.
  std::size_t line{};
};

struct TokenRead {
  Token token{};
  VdfError error{VdfError::None};
  std::size_t error_line{};
};

// The C locale's isspace set, stated so no locale enters the parse.
[[nodiscard]] bool is_vdf_space(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

[[nodiscard]] std::optional<char> translate_escape(char c) noexcept {
  switch (c) {
  case 'n':
    return '\n';
  case 't':
    return '\t';
  case 'v':
    return '\v';
  case 'b':
    return '\b';
  case 'r':
    return '\r';
  case 'f':
    return '\f';
  case 'a':
    return '\a';
  case '\\':
    return '\\';
  case '?':
    return '?';
  case '\'':
    return '\'';
  case '"':
    return '"';
  default:
    return std::nullopt;
  }
}

class Tokenizer {
public:
  Tokenizer(std::string_view text, bool escapes) noexcept : text_(text), escapes_(escapes) {}

  [[nodiscard]] std::size_t line() const noexcept { return line_; }

  [[nodiscard]] TokenRead next() {
    TokenRead read{};
    skip_space_and_comments();
    read.token.line = line_;
    if (at_ >= text_.size()) {
      read.token.kind = TokenKind::End;
      return read;
    }
    const char c = text_[at_];
    if (c == '"') {
      return quoted();
    }
    if (c == '{' || c == '}') {
      read.token.kind = c == '{' ? TokenKind::OpenBrace : TokenKind::CloseBrace;
      read.token.text.assign(1, c);
      ++at_;
      return read;
    }
    return bare();
  }

private:
  void advance() noexcept {
    if (text_[at_] == '\n') {
      ++line_;
    }
    ++at_;
  }

  void skip_space_and_comments() noexcept {
    for (;;) {
      while (at_ < text_.size() && is_vdf_space(text_[at_])) {
        advance();
      }
      if (at_ + 1U < text_.size() && text_[at_] == '/' && text_[at_ + 1U] == '/') {
        while (at_ < text_.size() && text_[at_] != '\n') {
          ++at_;
        }
        continue;
      }
      return;
    }
  }

  // A '"' starts a token that runs to the next unescaped '"'. With escapes
  // off every byte between the quotes is taken as written.
  [[nodiscard]] TokenRead quoted() {
    TokenRead read{};
    read.token.kind = TokenKind::Quoted;
    read.token.line = line_;
    advance();
    for (;;) {
      // An unclosed quote is named, not returned as the text read so far.
      if (at_ >= text_.size()) {
        read.error = VdfError::UnterminatedString;
        read.error_line = read.token.line;
        return read;
      }
      char c = text_[at_];
      if (c == '"') {
        advance();
        return read;
      }
      if (escapes_ && c == '\\') {
        const std::size_t escape_line = line_;
        advance();
        if (at_ >= text_.size()) {
          read.error = VdfError::UnterminatedString;
          read.error_line = read.token.line;
          return read;
        }
        const std::optional<char> translated = translate_escape(text_[at_]);
        if (!translated) {
          read.error = VdfError::InvalidEscape;
          read.error_line = escape_line;
          return read;
        }
        c = *translated;
      }
      advance();
      // A truncated path names another file, so a token of kVdfMaxTokenBytes
      // bytes or more is refused, not truncated.
      if (read.token.text.size() + 1U >= kVdfMaxTokenBytes) {
        read.error = VdfError::TokenTooLong;
        read.error_line = read.token.line;
        return read;
      }
      read.token.text.push_back(c);
    }
  }

  // A bare token runs to whitespace, '"', '{' or '}'; one that starts with
  // '[' is a conditional.
  [[nodiscard]] TokenRead bare() {
    TokenRead read{};
    read.token.line = line_;
    read.token.kind = text_[at_] == '[' ? TokenKind::Conditional : TokenKind::Bare;
    while (at_ < text_.size()) {
      const char c = text_[at_];
      if (c == '"' || c == '{' || c == '}' || is_vdf_space(c)) {
        break;
      }
      if (read.token.text.size() + 1U >= kVdfMaxTokenBytes) {
        read.error = VdfError::TokenTooLong;
        read.error_line = read.token.line;
        return read;
      }
      read.token.text.push_back(c);
      advance();
    }
    return read;
  }

  std::string_view text_;
  bool escapes_;
  std::size_t at_{0};
  std::size_t line_{1};
};

[[nodiscard]] char fold_ascii(char c) noexcept {
  if (c >= 'A' && c <= 'Z') {
    return static_cast<char>(c - 'A' + 'a');
  }
  return c;
}

[[nodiscard]] bool equals_ignoring_ascii_case(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (fold_ascii(a[i]) != fold_ascii(b[i])) {
      return false;
    }
  }
  return true;
}

class Parser {
public:
  Parser(std::string_view text, VdfOptions options) noexcept : tokens_(text, options.escapes) {}

  // "#include" and "#base" are admitted before the first root pair only, and
  // recorded, not followed: the parser has no file access. The text is read
  // as UTF-8, which is how Steam writes these files.
  [[nodiscard]] VdfParse parse() {
    bool directives_allowed = true;
    for (;;) {
      TokenRead key = tokens_.next();
      if (failed(key)) {
        return std::move(result_);
      }
      if (key.token.kind == TokenKind::End) {
        return std::move(result_);
      }
      if (directives_allowed && is_key_text(key.token)) {
        const std::optional<std::string> directive = directive_name(key.token.text);
        if (directive) {
          if (!read_directive(*directive, key.token.line)) {
            return std::move(result_);
          }
          continue;
        }
      }
      directives_allowed = false;
      VdfNode node{};
      if (!read_key(key, node)) {
        return std::move(result_);
      }
      TokenRead open = tokens_.next();
      if (failed(open)) {
        return std::move(result_);
      }
      if (open.token.kind == TokenKind::Conditional) {
        node.conditional = std::move(open.token.text);
        open = tokens_.next();
        if (failed(open)) {
          return std::move(result_);
        }
      }
      if (open.token.kind != TokenKind::OpenBrace) {
        // A root key followed by anything but '{', end of text included.
        fail(VdfError::MissingOpenBrace, open.token.line);
        return std::move(result_);
      }
      node.has_children = true;
      if (!read_members(node, open.token, 1)) {
        return std::move(result_);
      }
      result_.document.roots.push_back(std::move(node));
    }
  }

private:
  [[nodiscard]] static bool is_key_text(const Token& token) noexcept {
    return token.kind == TokenKind::Quoted || token.kind == TokenKind::Bare;
  }

  [[nodiscard]] static std::optional<std::string> directive_name(std::string_view text) {
    if (equals_ignoring_ascii_case(text, "#include")) {
      return std::string("#include");
    }
    if (equals_ignoring_ascii_case(text, "#base")) {
      return std::string("#base");
    }
    return std::nullopt;
  }

  void fail(VdfError error, std::size_t line) noexcept {
    result_.error = error;
    result_.line = line;
  }

  [[nodiscard]] bool failed(const TokenRead& read) noexcept {
    if (read.error == VdfError::None) {
      return false;
    }
    fail(read.error, read.error_line);
    return true;
  }

  [[nodiscard]] bool read_directive(std::string name, std::size_t line) {
    TokenRead argument = tokens_.next();
    if (failed(argument)) {
      return false;
    }
    const TokenKind kind = argument.token.kind;
    if (kind == TokenKind::End || kind == TokenKind::OpenBrace || kind == TokenKind::CloseBrace ||
        argument.token.text.empty()) {
      fail(VdfError::DirectiveWithoutArgument, kind == TokenKind::End ? line : argument.token.line);
      return false;
    }
    result_.document.directives.push_back(
        VdfDirective{std::move(name), std::move(argument.token.text)});
    return true;
  }

  // A key is any token but end of text and an unquoted '}', and is not empty.
  // An unquoted '{' or a conditional where a key is due is taken as key text.
  [[nodiscard]] bool read_key(TokenRead& key, VdfNode& node) {
    if (key.token.kind == TokenKind::End) {
      fail(VdfError::UnexpectedEnd, key.token.line);
      return false;
    }
    if (key.token.kind == TokenKind::CloseBrace) {
      fail(VdfError::UnexpectedCloseBrace, key.token.line);
      return false;
    }
    // An empty key is EmptyKey at the root as well as inside a block.
    if (key.token.text.empty()) {
      fail(VdfError::EmptyKey, key.token.line);
      return false;
    }
    node.key = std::move(key.token.text);
    return true;
  }

  // A conditional is read only between a key and its value. It is recorded on
  // its node and never evaluated, and the node is kept whatever it says. Every
  // value stays text; the caller converts the fields it reads.
  [[nodiscard]] bool read_members(VdfNode& parent, const Token& open, std::size_t depth) {
    if (depth > kVdfMaxDepth) {
      fail(VdfError::NestingTooDeep, open.line);
      return false;
    }
    for (;;) {
      TokenRead key = tokens_.next();
      if (failed(key)) {
        return false;
      }
      if (key.token.kind == TokenKind::CloseBrace) {
        return true;
      }
      VdfNode child{};
      if (!read_key(key, child)) {
        return false;
      }
      TokenRead value = tokens_.next();
      if (failed(value)) {
        return false;
      }
      if (value.token.kind == TokenKind::Conditional) {
        child.conditional = std::move(value.token.text);
        value = tokens_.next();
        if (failed(value)) {
          return false;
        }
        if (value.token.kind == TokenKind::Conditional) {
          fail(VdfError::ConditionalBeforeValue, value.token.line);
          return false;
        }
      }
      switch (value.token.kind) {
      case TokenKind::End:
        fail(VdfError::UnexpectedEnd, value.token.line);
        return false;
      case TokenKind::CloseBrace:
        fail(VdfError::UnexpectedCloseBrace, value.token.line);
        return false;
      case TokenKind::OpenBrace:
        child.has_children = true;
        if (!read_members(child, value.token, depth + 1U)) {
          return false;
        }
        break;
      case TokenKind::Quoted:
      case TokenKind::Bare:
      case TokenKind::Conditional:
        child.value = std::move(value.token.text);
        break;
      }
      // Duplicate keys are kept, in document order.
      parent.children.push_back(std::move(child));
    }
  }

  Tokenizer tokens_;
  VdfParse result_{};
};

} // namespace

VdfParse parse_vdf(std::string_view text, VdfOptions options) {
  // The text ends at its first NUL byte, as a C string does.
  const std::size_t nul = text.find('\0');
  if (nul != std::string_view::npos) {
    text = text.substr(0, nul);
  }
  Parser parser(text, options);
  return parser.parse();
}

const VdfNode* find_child(const VdfNode& node, std::string_view key) noexcept {
  for (const VdfNode& child : node.children) {
    if (equals_ignoring_ascii_case(child.key, key)) {
      return &child;
    }
  }
  return nullptr;
}

const VdfNode* find_root(const VdfDocument& document, std::string_view key) noexcept {
  for (const VdfNode& root : document.roots) {
    if (equals_ignoring_ascii_case(root.key, key)) {
      return &root;
    }
  }
  return nullptr;
}

} // namespace bw::content
