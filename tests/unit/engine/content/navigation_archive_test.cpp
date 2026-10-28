// Synthetic archive tests exercise indexed padding, ownership and malformed
// graph rejection. Original bytes are covered only by the separate corpus run.
// SL.con.3 bounds every fixture mutation; MEM.9 verifies setup-owned values.
#include "bw/content/formats/navigation_archive.hpp"

#include "../../../fixtures/navigation_fixture.hpp"
namespace {
using bw::resources::Bytes;
using camera_fixture::require;
using camera_fixture::word;
using navigation_fixture::archive;

}  // namespace
void navigation_archive_tests() {
  const auto domain =
      bw::memory::Domain::create(std::make_shared<camera_fixture::Backing>());
  auto bytes = archive();
  const bw::formats::NavigationArchive decoded(bytes, domain);
  bytes.clear();
  require(decoded.groups().size() == 2 && decoded.patrol(0, 7).size() == 2 &&
              decoded.patrol(0, 7)[1][0] == 4.0F &&
              decoded.vertices(1).size() == 2 &&
              decoded.vertices(1)[1].authored_word == 1 &&
              decoded.edges(1)[0].second == 1,
          "Indexed navigation values must outlive source bytes");
  require(decoded.patrol(1, 7).empty() && decoded.patrol(0, 8).empty() &&
              decoded.vertices(9).empty() && decoded.edges(0).empty(),
          "Missing original path/group must remain absent");
  require(decoded.find_group("patrol.pat") == 0 &&
              decoded.find_group("search.pat") == 1 &&
              !decoded.find_group("absent.pat"),
          "Original group names must retain exact owned lookup");
  const bw::formats::NavigationArchive absent({}, domain);
  require(absent.groups().empty(), "Optional absent archive grew paths");
  bytes = archive();
  word(bytes, 8 + 8 + 16, 0xffffffffU);
  bool rejected = false;
  try {
    (void)bw::formats::NavigationArchive(bytes, domain);
  } catch (const bw::resources::Error&) {
    rejected = true;
  }
  require(rejected, "Malformed group offset escaped archive bounds");
}
