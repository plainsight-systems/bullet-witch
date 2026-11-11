#ifndef BW_HOSTS_OPTION_TABLE_INL
#define BW_HOSTS_OPTION_TABLE_INL
// The tail of bw/hosts/option_table.hpp. Opened alone it includes that header,
// which leaves this file out while the guard above is defined.
#include "bw/hosts/option_table.hpp"

#include <vector>

// Definitions of bw/hosts/option_table.hpp.
namespace bw::hosts {

template <class Options>
inline OptionParse<Options> parse_options(std::span<const OptionRow<Options>> rows,
                                          std::span<const std::string_view> arguments) {
  const auto failure = [](OptionError error, std::string_view argument) {
    OptionParse<Options> failed{};
    failed.error = error;
    failed.argument = argument;
    return failed;
  };
  const auto row_named = [rows](std::string_view name) {
    std::size_t r = 0;
    while (r < rows.size() && rows[r].name != name) {
      ++r;
    }
    return r;
  };
  OptionParse<Options> result{};
  // The argument that gave each row; empty while the row is not given.
  std::vector<std::string_view> given(rows.size());
  for (std::size_t i = 0; i < arguments.size(); ++i) {
    const std::string_view argument = arguments[i];
    const std::size_t r = row_named(argument);
    if (r == rows.size()) {
      return failure(OptionError::UnknownOption, argument);
    }
    if (!given[r].empty() && !rows[r].repeatable) {
      return failure(OptionError::DuplicateOption, argument);
    }
    given[r] = argument;
    std::string_view value{};
    if (!rows[r].value_name.empty()) {
      if (i + 1 == arguments.size() || arguments[i + 1].starts_with("--")) {
        return failure(OptionError::MissingValue, argument);
      }
      ++i;
      value = arguments[i];
    }
    const std::string_view reason = rows[r].apply(result.options, value);
    if (!reason.empty()) {
      OptionParse<Options> refused = failure(OptionError::InvalidValue, value);
      refused.reason = reason;
      return refused;
    }
  }
  for (std::size_t r = 0; r < rows.size(); ++r) {
    if (given[r].empty() || rows[r].needs.empty()) {
      continue;
    }
    const std::size_t needed = row_named(rows[r].needs);
    if (needed == rows.size() || given[needed].empty()) {
      return failure(OptionError::RequiredOptionMissing, given[r]);
    }
  }
  return result;
}

template <class Options>
inline std::string usage_line(std::string_view program, std::span<const OptionRow<Options>> rows) {
  std::string line = "Usage: ";
  line += program;
  for (const OptionRow<Options>& row : rows) {
    line += " [";
    line += row.name;
    if (!row.value_name.empty()) {
      line += ' ';
      line += row.value_name;
    }
    line += ']';
    if (row.repeatable) {
      line += "...";
    }
  }
  return line;
}

} // namespace bw::hosts
#endif // BW_HOSTS_OPTION_TABLE_INL
