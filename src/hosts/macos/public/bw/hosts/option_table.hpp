#ifndef BW_HOSTS_OPTION_TABLE_HPP
#define BW_HOSTS_OPTION_TABLE_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

// Responsibility: a command line as a table. One row per option names the
// option, what its value is called, the function that takes the value, and
// the option it needs; one parse loop and one usage line serve every table.
// Adding an option to an executable is adding a row. Nothing here reads the
// environment, the file system or a global, so the same rows and arguments
// always give the same result.
//
// Grammar, for any table: each option at most once, in any order. A row
// with an empty value_name is a flag and takes no value. Otherwise the
// argument after the option is its value, and the value is missing when
// that argument is absent or starts with "--".
//
// Contract.
//   1. Arguments are read left to right and the first argument in error
//      ends the parse; that error and the argument it names are returned.
//      For one option: an argument that matches no row is UnknownOption
//      (argument: itself); an option given before is DuplicateOption
//      (argument: the option); a missing value is
//      MissingValue (argument: the option); otherwise the row's apply is
//      called with the value, or with an empty view for a flag, and a
//      non-empty return is InvalidValue (argument: the value, reason: the
//      return).
//   2. After every argument parsed without error: a given option whose
//      `needs` names an option that was not given is RequiredOptionMissing
//      (argument: the option that was given). With several such, which one
//      is named is not promised. A `needs` that names no row is never
//      satisfied, so every use of that option is RequiredOptionMissing.
//   3. apply is called at most once per row, only for a given option, in
//      argument order. On an error the options returned are not meaningful.
//   4. usage_line(program, rows) is "Usage: " program, then for each row in
//      table order " [" name "]" for a flag and " [" name " " value_name
//      "]" otherwise.
//   Where one argument breaks two of the rules in 1, which is named is not
//   promised. Preconditions: names distinct and starting with "--", every
//   apply non-null. The reason text is for the person at the terminal; its
//   wording is not part of the contract.
//
// Read guidance: ES.3 (one loop, not a branch per option), I.1, Con.5 (a
// parse is a pure function of rows and arguments), E.27 (named errors),
// F.20 (the parse is returned by value).
namespace bw::hosts {

template <class Options> struct OptionRow {
  // "--seconds".
  std::string_view name{};
  // "N"; empty for a flag.
  std::string_view value_name{};
  // Stores the value in `options`; returns empty, or why the value is
  // refused. `value` views the caller's argument.
  std::string_view (*apply)(Options& options, std::string_view value){};
  // The name of an option that must also be given; empty for none.
  std::string_view needs{};
};

enum class OptionError : std::uint8_t {
  None,
  UnknownOption,
  DuplicateOption,
  MissingValue,
  InvalidValue,
  RequiredOptionMissing,
};

// options is meaningful when error is None. argument views the caller's
// argument and is empty when error is None; reason is non-empty only for
// InvalidValue and views static text.
template <class Options> struct OptionParse {
  Options options{};
  OptionError error{OptionError::None};
  std::string_view argument{};
  std::string_view reason{};
};

// The arguments after the program name.
template <class Options>
[[nodiscard]] OptionParse<Options> parse_options(std::span<const OptionRow<Options>> rows,
                                                 std::span<const std::string_view> arguments);

template <class Options>
[[nodiscard]] std::string usage_line(std::string_view program,
                                     std::span<const OptionRow<Options>> rows);

} // namespace bw::hosts

#ifndef BW_HOSTS_OPTION_TABLE_INL
#include "bw/hosts/option_table.inl"
#endif

#endif
