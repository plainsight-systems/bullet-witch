// Extended contracts: A1 DPK bounds; A3 publication/release using an in-memory
// file capability. No processes/files. Ordered checks cover phases, cycle
// termination, error/retry and lease lifetime, with fixture-sized allocations.
// Optimization: retain one archive lease across release rather than copying.
// Design: R.20 (#rr-owner) and COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) define shared lifetime. Implementation:
// SL.con.3 (#rsl-bounds) checks fixture writes; COPY.4 preserves value
// ownership. In-process resource contract verification (V1 in the draft file
// mapping). See docs/resources.md and tests/README.md. Synthetic bytes are
// owned locally; no file, subprocess, game installation or rendering context is
// required. Contract: successful inputs produce specified metadata/payloads;
// malformed and unsupported inputs produce the required error category. Failed
// checks throw and cause a nonzero process exit. Printed count is assertions,
// not tests.
//
// Algorithm: (1) construct small independent on-disk fixtures; (2) decode and
// check public results, ownership/moves and resolution; (3) mutate bounded
// fields or truncate each input length; (4) verify rejection categories.
// Costs: fixture-sized allocations per mutation; truncation loops repeat parse
// work intentionally to exercise every boundary. No performance measurement.
// Optimization: keep fixtures in memory and avoid provisioning a real corpus
// for isolated contract checks; full-corpus acceptance remains separate.
//
// Retrieved design guidance: cpp-guidelines I.1 (#ri-explicit) keeps checks on
// public contracts, I.30 (#ri-encapsulate) keeps fixture manipulation private;
// cpp-performance COPY.2 (guidelines/copy-move/
// COPY.2-mark-move-constructors-noexcept.md) informs ownership/move
// verification. Implementation: cpp-guidelines SL.con.3 (#rsl-bounds) informs
// bounded fixture writes; cpp-performance CDSA.32 (guidelines/cpu-dsa/
// CDSA.32-transform-static-data-into-the-consumer-layout.md) informs comparison
// of decoded/indexed results with independently specified raw fixtures.

// Naming design/implementation consultation: cpp-guidelines P.3 (#rp-what)
// selects role-based names. cpp-performance COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) preserves owning values and implicit
// moves; renaming adds no wrapper, allocation or payload copy.

#include "bw/resources.hpp"

#include <algorithm>
#include <functional>
#include <iostream>

#include "bw/formats/named_file_package.hpp"
#include "bw/resources/service.hpp"

using bw::resources::Bytes;
using bw::resources::CatalogSet;
using bw::resources::Error;
using bw::resources::ErrorCode;
using bw::resources::ResourceArchive;
using bw::resources::ResourceCatalog;
namespace {
class Checks {
 public:
  void check(bool value, const char* message);
  void rejects(const std::function<void()>& fn,
               ErrorCode expected = ErrorCode::malformed);
  int count() const { return checks; }

 private:
  int checks = 0;
};
void Checks::check(bool value, const char* message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
void Checks::rejects(const std::function<void()>& fn, ErrorCode expected) {
  ++checks;
  try {
    fn();
  } catch (const Error& e) {
    if (e.code != expected) {
      throw std::runtime_error("Wrong rejection category: " +
                               std::string(e.what()));
    }
    return;
  }
  throw std::runtime_error("Expected rejection");
}
void word(Bytes& b, std::size_t at, std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) {
    b.at(at + i) = static_cast<std::uint8_t>(value >> (i * 8));
  }
}
void text(Bytes& b, std::size_t at, std::string_view value) {
  for (std::size_t i = 0; i < value.size(); ++i) {
    b.at(at + i) = static_cast<std::uint8_t>(value[i]);
  }
}
Bytes archive_fixture() {
  // Two records, one in each section; argument string and publication differ
  // from record name.
  Bytes b(272);
  text(b, 0, "CPR1");
  word(b, 4, 1);
  word(b, 8, 0x11223344);
  word(b, 16, 96);
  word(b, 20, 12);
  word(b, 24, 48);
  word(b, 28, 4);
  word(b, 32, 8);
  word(b, 36, 8);
  word(b, 44, 160);
  text(b, 96, "USER");
  word(b, 100, 1);
  word(b, 104, 8);
  word(b, 108, 0xffffffff);
  word(b, 112, 1);
  word(b, 132, 3);
  text(b, 144, "ZZZZ");
  word(b, 148, 1);
  word(b, 156, 0xffffffff);
  word(b, 184, 1);
  word(b, 188, 2);
  word(b, 192, 20);
  word(b, 196, 2);
  word(b, 200, 24);
  text(b, 205, "member");
  text(b, 212, "script.root");
  text(b, 224, "key");
  text(b, 228, "value");
  text(b, 256, "ABC");
  text(b, 265, "XY");
  return b;
}
Bytes catalog_fixture() {
  // Root -> script; two resources and one archive; dependency on sibling plus
  // external name.
  Bytes b(228);
  text(b, 0, "RAT ");
  word(b, 4, 228);
  word(b, 16, 64);
  word(b, 20, 44);
  word(b, 32, 108);
  word(b, 36, 40);
  word(b, 40, 148);
  word(b, 44, 16);
  word(b, 48, 164);
  word(b, 52, 8);
  word(b, 56, 172);
  word(b, 60, 56);
  word(b, 68, 0xffffffff);
  word(b, 72, 1);
  word(b, 80, 20);
  word(b, 84, 1);
  word(b, 96, 2);
  word(b, 100, 0);
  word(b, 104, 1);
  word(b, 108, 8);
  word(b, 112, 20);
  word(b, 124, 0);
  word(b, 128, 13);
  word(b, 132, 20);
  word(b, 144, 0xffffffff);
  word(b, 148, 1);
  word(b, 152, 1);
  word(b, 156, 1);
  word(b, 160, 18);
  word(b, 164, 27);
  word(b, 168, 31);
  text(b, 173, "script");
  text(b, 180, "root");
  text(b, 185, "peer");
  text(b, 190, "external");
  text(b, 199, "sub");
  text(b, 203, "data.cpr");
  return b;
}
void archive_tests(Checks& checks) {
  auto b = archive_fixture();
  auto c = ResourceArchive::parse(b);
  checks.check(c.records().size() == 2, "CPR count");
  checks.check(c.records()[0].name == "member" &&
                   c.records()[0].publication_name == "script.root",
               "Publication identity");
  checks.check(c.records()[0].arguments[0].string_value == "value",
               "Argument string");
  checks.check(c.payload(0)[0] == 'A' && c.payload(1)[1] == 'Y',
               "Both payload regions");
  checks.check(!c.records()[1].known_type, "Unknown handler visible");
  auto padded = b;
  word(padded, 20, 16);
  word(padded, 28, 0);
  // Move the name pool four bytes; total metadata/payload position stays
  // unchanged.
  std::copy_backward(padded.begin() + 204, padded.begin() + 252,
                     padded.begin() + 256);
  checks.check(
      ResourceArchive::parse(padded).records()[0].arguments[0].string_value ==
          "value",
      "Argument area padding");
  auto moved = std::move(c);
  checks.check(moved.payload(0).size() == 3, "Owned bytes survive move");
  word(b, 108, 225);  // 0x28 + 225 == 265, preserving the first record's
                      // three-byte size.
  auto alias = ResourceArchive::parse(b);
  checks.check(alias.payload(0)[0] == 'X', "Inline reference override");
  for (std::size_t length = 0; length < 272; ++length) {
    auto short_b = archive_fixture();
    short_b.resize(length);
    checks.rejects([&] { ResourceArchive::parse(short_b); });
  }
  for (auto [at, value] :
       std::vector<std::pair<std::size_t, std::uint32_t>>{{16, 95},
                                                          {20, 11},
                                                          {44, 159},
                                                          {100, 48},
                                                          {112, 0xffffffff},
                                                          {116, 0xffffffff},
                                                          {128, 7},
                                                          {132, 0xffffffff},
                                                          {184, 8},
                                                          {108, 0xfffffffe},
                                                          {200, 48}}) {
    auto bad = archive_fixture();
    word(bad, at, value);
    checks.rejects([&] { ResourceArchive::parse(bad); });
  }
  auto unreferenced = archive_fixture();
  word(unreferenced, 112, 0);
  word(unreferenced, 192, 48);
  checks.rejects([&] { ResourceArchive::parse(unreferenced); });
  auto unterminated = archive_fixture();
  std::fill(unterminated.begin() + 205, unterminated.begin() + 252, 'a');
  checks.rejects([&] { ResourceArchive::parse(unterminated); });
  for (auto at : {4, 8, 40, 48}) {
    auto bad = archive_fixture();
    word(bad, static_cast<std::size_t>(at), 9);
    checks.rejects([&] { ResourceArchive::parse(bad); },
                   ErrorCode::unsupported);
  }
}
void catalog_tests(Checks& checks) {
  auto b = catalog_fixture();
  auto c = ResourceCatalog::parse(b);
  checks.check(c.find("SCRIPT.ROOT") == 0 && c.find("script.peer") == 1 &&
                   !c.find("absent"),
               "Dotted ASCII case lookup");
  checks.check(c.resources()[0].dependencies.size() == 2 &&
                   c.resources()[0].dependencies[0].name == "script.peer" &&
                   c.resources()[0].dependencies[1].name == "external",
               "Dependency order and identity");
  checks.check(c.files()[0].relative_path == "sub/data.cpr",
               "Relative archive path");
  for (std::size_t length = 0; length < b.size(); ++length) {
    auto short_b = b;
    short_b.resize(length);
    if (length >= 8) word(short_b, 4, static_cast<std::uint32_t>(length));
    checks.rejects([&] { ResourceCatalog::parse(short_b); });
  }
  for (auto [at, value] :
       std::vector<std::pair<std::size_t, std::uint32_t>>{{16, 0},
                                                          {20, 0xffffffff},
                                                          {36, 39},
                                                          {52, 7},
                                                          {56, 64},
                                                          {80, 0},
                                                          {80, 21},
                                                          {88, 20},
                                                          {96, 0xffffffff},
                                                          {104, 0},
                                                          {104, 2},
                                                          {112, 0},
                                                          {120, 1},
                                                          {124, 2},
                                                          {148, 0xffffffff},
                                                          {152, 2},
                                                          {156, 0xffffffff},
                                                          {160, 56},
                                                          {168, 56}}) {
    auto bad = b;
    word(bad, at, value);
    checks.rejects([&] { ResourceCatalog::parse(bad); });
  }
  for (auto path : {"../", "/xx", "C:x"}) {
    auto bad = b;
    text(bad, 199, path);
    checks.rejects([&] { ResourceCatalog::parse(bad); });
  }
  auto slash = b;
  text(slash, 199, "s\\b");
  checks.check(
      ResourceCatalog::parse(slash).files()[0].relative_path == "s/b/data.cpr",
      "Windows separators");
  CatalogSet set;
  set.mount(55, "high.rat", c);
  set.mount(30, "low.rat", c);
  set.mount(55, "other.rat", c);
  checks.check(set.resolve("script.root").catalog_index == 1,
               "Lower-key precedence");
  checks.rejects([&] { set.resolve("missing"); }, ErrorCode::not_found);
  set.mount(30, "conflict.rat", c);
  checks.rejects([&] { set.resolve("script.root"); }, ErrorCode::ambiguous);
  set.mount(20, "winner.rat", c);
  checks.check(set.resolve("script.root").catalog_index == 4,
               "Lower key resolves preceding ambiguity");
}
// Synthetic file capability; this exercises the service contract in process.
class MemoryFiles final : public bw::platform::Files {
 public:
  Bytes contents;
  bool fail{};
  Bytes read(const std::filesystem::path&) const override {
    if (fail) throw Error(ErrorCode::io, "fixture read failure");
    return contents;
  }
  std::filesystem::path canonical(
      const std::filesystem::path& path) const override {
    return path.lexically_normal();
  }
};
void lifecycle_tests(Checks& checks) {
  using bw::resources::LoadState;
  auto rat = catalog_fixture();
  // Both dependency entries point to root: traversal must terminate.
  word(rat, 152, 0);
  word(rat, 156, 0);
  auto archive = archive_fixture();
  // Second record is an unpublished USER; keep its bounded payload.
  text(archive, 144, "USER");
  MemoryFiles files;
  files.contents = archive;
  CatalogSet catalogs;
  catalogs.mount(30, "/fixture/data.rat", ResourceCatalog::parse(rat));
  bw::resources::ResourceService service(std::move(catalogs), files);
  checks.check(!service.acquire("script.root"), "Unpublished lookup");
  service.request("SCRIPT.ROOT", 1);
  service.request("script.root", 2);
  checks.check(service.pump(1).state == LoadState::opened,
               "Open precedes publication");
  checks.check(!service.acquire("script.root"),
               "Open alone is not publication");
  checks.check(service.pump(1).state == LoadState::published,
               "USER publication");
  auto lease = service.acquire("script.root");
  checks.check(lease && lease->bytes().size() == 3,
               "Published bounded payload");
  checks.check(service.pump(2).state == LoadState::ready,
               "Shared phase completion");
  service.release(1);
  checks.check(service.acquire("script.root").has_value(),
               "Other tag retains publication");
  service.release(2);
  if (!lease) {
    throw std::runtime_error("Missing retained lease");
  }
  checks.check(
      !service.acquire("script.root") && lease.value().bytes()[0] == 'A',
      "Lease survives unpublication");
  service.request("script.root", 1);
  files.fail = true;
  checks.check(service.pump(1).state == LoadState::failed,
               "Open failure is terminal");
  files.fail = false;
  checks.check(service.pump(1).state == LoadState::failed,
               "Failure does not busy retry");
  service.release(1);
  service.request("script.root", 1);
  checks.check(service.pump(1).state == LoadState::opened,
               "Release permits retry");
  service.release(1);
  files.contents = archive_fixture();
  service.request("script.root", 1);
  (void)service.pump(1);
  checks.check(service.pump(1).state == LoadState::failed &&
                   !service.acquire("script.root"),
               "Unsupported record prevents partial publication");
  service.release(1);
  files.contents = archive_fixture();
  text(files.contents, 144, "DDS ");
  word(files.contents, 156,
       225);  // Selected sibling payload at archive+40+225.
  service.request("script.root", 1);
  (void)service.pump(1);
  (void)service.pump(1);
  checks.check(service.pump(1).state == LoadState::ready,
               "Encoded image siblings do not require catalog publications");
  auto sprite_lease = service.acquire("script.root");
  service.release(1);
  checks.check(
      sprite_lease && sprite_lease->archive().payload(1).size() == 2 &&
          sprite_lease->archive().payload(1)[0] == 'X',
      "Lease retains bounded inline-selected sibling bytes after release");
}
void package_tests(Checks& checks) {
  Bytes b(99);
  text(b, 0, "dpk");
  word(b, 4, 32);
  word(b, 8, 1);
  word(b, 12, 96);
  word(b, 16, 1);
  word(b, 48, 3);
  word(b, 52, 3);
  word(b, 56, 96);
  word(b, 60, 64);
  text(b, 64, "root.lub");
  text(b, 96, "abc");
  auto package = bw::resources::NamedFilePackage::parse(b);
  checks.check(package.member("root.lub")[2] == 'c', "DPK member view");
  checks.rejects([&] { (void)package.member("ROOT.LUB"); },
                 ErrorCode::not_found);
  auto bad = b;
  word(bad, 56, 98);
  checks.rejects([&] { bw::resources::NamedFilePackage::parse(bad); });
  bad = b;
  word(bad, 60, 20);
  checks.rejects([&] { bw::resources::NamedFilePackage::parse(bad); });
  bad = b;
  word(bad, 52, 4);
  auto compressed = bw::resources::NamedFilePackage::parse(bad);
  checks.rejects([&] { (void)compressed.member("root.lub"); },
                 ErrorCode::unsupported);
  bad = b;
  word(bad, 16, 0xffffffff);
  checks.rejects([&] { bw::resources::NamedFilePackage::parse(bad); });
  auto moved = std::move(package);
  checks.check(moved.member("root.lub").size() == 3,
               "Moved package owns payload");
}
}  // namespace
int main() {
  Checks checks;
  try {
    archive_tests(checks);
    catalog_tests(checks);
    lifecycle_tests(checks);
    package_tests(checks);
    std::cout << checks.count() << " checks passed\n";
  } catch (const std::exception& e) {
    std::cerr << "Failure after " << checks.count() << " checks: " << e.what()
              << '\n';
    return 1;
  }
}
