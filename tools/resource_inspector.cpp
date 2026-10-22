// DPK inspection extension (A1): copy each recognized package once for its
// owned decoder, serialize bounded metadata; no handlers execute. Cost O(DPK
// bytes + members). Optimization: no per-member payload copy.
// Design/implementation consultation: R.20 (#rr-owner), SL.con.3 (#rsl-bounds),
// COPY.4 (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md). Read-only
// resource inspection driver (V1/C1 in the draft file mapping). See
// docs/resources.md for the implemented commands and JSON contract. Contract:
// argv selects cpr, rat, inventory or resolve. Files are read only; JSON
// results go to stdout and command errors to stderr. Inventory emits per-file
// errors and continues, then fails overall if any file failed. Nonzero exit
// distinguishes usage, malformed/host errors, missing resources and ambiguity.
// No record handlers, GPU initialization or rendering occurs.
//
// Algorithm: (1) validate arguments; (2) load one file, enumerate/sort input
// paths, or mount catalogs; (3) invoke the owned reader/resolver; (4) serialize
// metadata and bounded payload prefixes; (5) report errors with stable codes.
// Inputs remain alive through serialization; references are borrowed locally.
// Costs: O(P log P) inventory sort for P paths, one archive plus JSON buffer at
// a time. Resolve retains mounted catalogs. JSON work scales with output size.
// Optimization: inspect payloads through spans, never copy complete payloads
// merely to display a prefix. This is a design property, not a benchmark.
//
// Retrieved guidance: cpp-guidelines I.1 (#ri-explicit), I.30
// (#ri-encapsulate) guided command/error and ownership boundaries during
// design; SL.con.3 (#rsl-bounds) guided bounded argv and payload access in
// implementation. cpp-performance CDSA.32 (guidelines/cpu-dsa/
// CDSA.32-transform-static-data-into-the-consumer-layout.md), consulted in both
// phases, applies through reuse of the reader's built catalog indices; this
// driver performs no speculative payload-layout conversion.

#include <algorithm>
#include <charconv>
#include <iostream>
#include <sstream>

#include "bw/content/access.hpp"
#include "bw/formats/dpk.hpp"
#include "bw/resources.hpp"

using bw::resources::CatalogResource;
using bw::resources::CatalogSet;
using bw::resources::CprArchive;
using bw::resources::Error;
using bw::resources::error_name;
using bw::resources::ErrorCode;
using bw::resources::normalize_name;
using bw::resources::Range;
using bw::resources::RatCatalog;
namespace {
constexpr unsigned kFirstPrintable = 32;
constexpr unsigned kDelete = 127;
constexpr unsigned kNibbleBits = 4;
constexpr unsigned kNibbleMask = 15;
constexpr std::size_t kCprHeaderBytes = 96;
constexpr std::size_t kCprRecordBytes = 48;
constexpr std::size_t kPayloadPreviewBytes = 32;
constexpr int kMinimumResolveArgs = 6;
std::string quote(std::string_view text) {
  std::string out = "\"";
  constexpr std::string_view hex = "0123456789abcdef";
  for (char raw : text) {
    const auto c = static_cast<unsigned char>(raw);
    if (c == '"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < kFirstPrintable || c >= kDelete) {
      out += "\\u00";
      out += hex[c >> kNibbleBits];
      out += hex[c & kNibbleMask];
    } else {
      out += static_cast<char>(c);
    }
  }
  return out + '"';
}
void optional_text(std::ostream& out, const std::optional<std::string>& value) {
  out << (value ? quote(*value) : "null");
}
template <class T>
void numbers(std::ostream& out, const T& values) {
  out << '[';
  bool comma = false;
  for (auto x : values) {
    if (comma) out << ',';
    comma = true;
    out << x;
  }
  out << ']';
}
void range(std::ostream& out, Range r) {
  out << "{\"offset\":" << r.offset << ",\"size\":" << r.size << '}';
}
void range(std::ostream& out, const std::optional<Range>& r) {
  if (r) {
    range(out, *r);
  } else {
    out << "null";
  }
}
template <class T>
void sections(std::ostream& out, const T& values) {
  out << '[';
  bool comma = false;
  for (auto value : values) {
    if (comma) out << ',';
    comma = true;
    range(out, value);
  }
  out << ']';
}
void dependencies(std::ostream& out, const CatalogResource& resource) {
  out << '[';
  bool comma = false;
  for (const auto& d : resource.dependencies) {
    if (comma) out << ',';
    comma = true;
    out << "{\"kind\":" << quote(d.local_index ? "local_index" : "named")
        << ",\"local_index\":";
    if (d.local_index) {
      out << *d.local_index;
    } else {
      out << "null";
    }
    out << ",\"name\":" << quote(d.name) << '}';
  }
  out << ']';
}
void cpr(std::ostream& out, const CprArchive& archive) {
  out << "{\"bytes\":" << archive.bytes().size()
      << ",\"header_u32_from_0x04\":";
  numbers(out, archive.header());
  out << ",\"sections\":";
  sections(out, archive.sections());
  out << ",\"records\":[";
  for (std::size_t i = 0; i < archive.records().size(); ++i) {
    const auto& record = archive.records()[i];
    if (i) out << ',';
    out << "{\"index\":" << i
        << ",\"table_offset\":" << kCprHeaderBytes + i * kCprRecordBytes
        << ",\"type\":" << quote(record.type)
        << ",\"name\":" << quote(record.name)
        << ",\"known_type\":" << (record.known_type ? "true" : "false")
        << ",\"handler_status\":\"not_implemented\",\"publication_name\":";
    optional_text(out, record.publication_name);
    out << ",\"fields_after_type\":";
    numbers(out, record.raw);
    out << ",\"first_payload\":";
    range(out, record.first_payload);
    out << ",\"second_payload\":";
    range(out, record.second_payload);
    out << ",\"selected_payload\":";
    range(out, record.selected_payload);
    out << ",\"payload_prefix_hex\":\"";
    constexpr std::string_view hex = "0123456789abcdef";
    auto payload = archive.payload(i);
    for (std::size_t j = 0;
         j < std::min<std::size_t>(kPayloadPreviewBytes, payload.size()); ++j) {
      out << hex[payload[j] >> kNibbleBits] << hex[payload[j] & kNibbleMask];
    }
    out << "\",\"dpk_members\":[";
    if (payload.size() >= 4 && payload[0] == 'd' && payload[1] == 'p' &&
        payload[2] == 'k' && payload[3] == 0) {
      auto package =
          bw::resources::DpkPackage::parse({payload.begin(), payload.end()});
      bool comma = false;
      for (const auto& member : package.members()) {
        if (comma) {
          out << ',';
        }
        comma = true;
        out << "{\"name\":" << quote(member.name)
            << ",\"offset\":" << member.stored.offset
            << ",\"stored_bytes\":" << member.stored.size
            << ",\"declared_bytes\":" << member.declared_size << '}';
      }
    }
    out << "],\"arguments\":[";
    for (std::size_t j = 0; j < record.arguments.size(); ++j) {
      if (j) out << ',';
      const auto& a = record.arguments[j];
      out << "{\"raw\":";
      numbers(out, a.raw);
      out << ",\"name\":";
      optional_text(out, a.name);
      out << ",\"string_value\":";
      optional_text(out, a.string_value);
      out << '}';
    }
    out << "]}";
  }
  out << "]}";
}
void rat(std::ostream& out, const RatCatalog& catalog) {
  out << "{\"bytes\":" << catalog.bytes().size() << ",\"sections\":";
  sections(out, catalog.sections());
  out << ",\"files\":[";
  for (std::size_t i = 0; i < catalog.files().size(); ++i) {
    if (i) out << ',';
    const auto& f = catalog.files()[i];
    out << "{\"index\":" << i << ",\"directory\":" << quote(f.directory)
        << ",\"filename\":" << quote(f.filename)
        << ",\"relative_path\":" << quote(f.relative_path) << '}';
  }
  out << "],\"resources\":[";
  for (std::size_t i = 0; i < catalog.resources().size(); ++i) {
    if (i) out << ',';
    const auto& r = catalog.resources()[i];
    out << "{\"index\":" << i << ",\"raw\":";
    numbers(out, r.raw);
    out << ",\"name\":" << quote(r.name)
        << ",\"full_name\":" << quote(r.full_name) << ",\"extra\":";
    optional_text(out, r.extra);
    out << ",\"lookup_matches\":"
        << (catalog.find(r.full_name) == i ? "true" : "false")
        << ",\"dependencies\":";
    dependencies(out, r);
    out << '}';
  }
  out << "],\"namespaces\":[";
  for (std::size_t i = 0; i < catalog.namespaces().size(); ++i) {
    if (i) out << ',';
    const auto& n = catalog.namespaces()[i];
    out << "{\"offset\":" << n.offset << ",\"parent\":" << n.parent
        << ",\"name\":" << quote(n.name) << ",\"children\":";
    numbers(out, n.children);
    out << ",\"resources\":";
    numbers(out, n.resources);
    out << '}';
  }
  out << "]}";
}
std::string inspect(const std::filesystem::path& path, std::string_view kind,
                    std::string_view label) {
  std::ostringstream out;
  out << "{\"kind\":" << quote(kind) << ",\"path\":" << quote(label)
      << ",\"data\":";
  if (kind == "cpr") {
    cpr(out, bw::content::open_cpr(bw::platform::HostFiles{}, path));
  } else {
    rat(out, bw::content::open_rat(bw::platform::HostFiles{}, path));
  }
  return out.str() + '}';
}
void error(std::ostream& out, std::string_view code, std::string_view message) {
  out << "{\"error\":" << quote(code) << ",\"message\":" << quote(message)
      << "}\n";
}
int usage() {
  std::cerr
      << "Usage: bw-resource cpr FILE | rat FILE | inventory GAME_DIRECTORY\n"
         "       bw-resource resolve NAME --catalog PRIORITY RAT [--catalog "
         "PRIORITY RAT ...]\n";
  return 1;
}
}  // namespace
int main(int argc, char** argv) {
  const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
  try {
    if (argc < 3) return usage();
    std::string command = arguments[1];
    if ((command == "cpr" || command == "rat") && argc == 3) {
      std::cout << inspect(arguments[2], command, arguments[2]) << '\n';
    } else if (command == "inventory" && argc == 3) {
      std::filesystem::path base(arguments[2]);
      std::vector<std::filesystem::path> paths;
      for (const auto& entry :
           std::filesystem::recursive_directory_iterator(base)) {
        auto ext = normalize_name(entry.path().extension().string());
        if (entry.is_regular_file() && (ext == ".rat" || ext == ".cpr")) {
          paths.push_back(entry.path());
        }
      }
      std::sort(paths.begin(), paths.end());
      bool failed = false;
      for (const auto& path : paths) {
        auto label = path.lexically_relative(base).generic_string();
        try {
          std::cout << inspect(
                           path,
                           normalize_name(path.extension().string()).substr(1),
                           label)
                    << '\n';
        } catch (const Error& e) {
          std::cout << "{\"path\":" << quote(label)
                    << ",\"error\":" << quote(error_name(e.code))
                    << ",\"message\":" << quote(e.what()) << "}\n";
          failed = true;
        }
      }
      if (paths.empty()) {
        throw Error(ErrorCode::not_found, "No RAT or CPR files found");
      }
      if (failed) return 2;
    } else if (command == "resolve" && argc >= kMinimumResolveArgs &&
               (argc - 3) % 3 == 0) {
      CatalogSet set;
      for (std::size_t i = 3; i < arguments.size(); i += 3) {
        if (std::string_view(arguments[i]) != "--catalog") {
          return usage();
        }
        int priority{};
        std::string_view number(arguments[i + 1]);
        const auto* number_begin = number.data();
        // from_chars requires an end pointer; the view bounds this one-past
        // pointer and remains alive throughout parsing.
        // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
        const auto* number_end = number_begin + number.size();
        auto [end, ec] = std::from_chars(number_begin, number_end, priority);
        if (ec != std::errc{} || end != number_end) return usage();
        set.mount(
            priority, arguments[i + 2],
            bw::content::open_rat(bw::platform::HostFiles{}, arguments[i + 2]));
      }
      auto hit = set.resolve(arguments[2]);
      const auto& mount = set.catalogs()[hit.catalog_index];
      const auto& r = mount.catalog.resources()[hit.resource_index];
      auto path =
          bw::content::archive_path(bw::platform::HostFiles{}, mount, r.raw[3]);
      auto archive = bw::content::open_cpr(bw::platform::HostFiles{}, path);
      std::ostringstream out;
      out << "{\"name\":" << quote(r.full_name)
          << ",\"catalog\":" << quote(mount.path.string())
          << ",\"priority\":" << mount.priority
          << ",\"resource_index\":" << hit.resource_index
          << ",\"archive_path\":" << quote(path.string())
          << ",\"dependencies\":";
      dependencies(out, r);
      out << ",\"publication_record_indices\":[";
      bool comma = false;
      for (std::size_t i = 0; i < archive.records().size(); ++i) {
        const auto& name = archive.records()[i].publication_name;
        if (name && normalize_name(*name) == normalize_name(r.full_name)) {
          if (comma) out << ',';
          comma = true;
          out << i;
        }
      }
      out << "],\"archive\":";
      cpr(out, archive);
      out << "}\n";
      std::cout << out.str();
    } else {
      return usage();
    }
    return 0;
  } catch (const Error& e) {
    error(std::cerr, error_name(e.code), e.what());
    return e.code == ErrorCode::not_found   ? 3
           : e.code == ErrorCode::ambiguous ? 4
                                            : 2;
  } catch (const std::exception& e) {
    error(std::cerr, "host", e.what());
    return 2;
  }
}
