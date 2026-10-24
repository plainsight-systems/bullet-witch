// Prepared clip semantics keep incompatible pose representations visible but
// disabled without changing catalog identity (Core I.1/E.4, CDSA.32).
// Pause navigation publishes explicit requests before external transactions.
// Review pages borrow prepared catalog labels; only a bounded page is visited.
// Completion copies any transient error into fixed storage, resumes accepted
// previews, and otherwise retains pause. Menu consumption never advances game
// clocks. Both MCP phases: I.1 (#ri-explicit) separates input from admission;
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds all
// storage.
#include "pause_flow.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
namespace bw::game {
namespace {
constexpr unsigned kCostumes = 6, kWeapons = 4;
constexpr std::size_t kClipsPerPage = 12, kClipLabelLimit = 30;
constexpr unsigned kPreviousPage = std::numeric_limits<unsigned>::max();
constexpr unsigned kNextPage = kPreviousPage - 1;
constexpr std::uint32_t kConfirm = 0x1010, kCancel = 0x2020, kUp = 0x01000000,
                        kDown = 0x02000000;
constexpr std::array<std::string_view, 17> kReviews{
    "Stop preview",   "Next phase (synthetic)",
    "Preview raise",  "Preview lower",
    "Preview shot",   "Preview reload",
    "Preview melee",  "Preview element",
    "Body clips",     "Upper clips",
    "Additive clips", "Weapon clips",
    "Face",           "Scripted jump",
    "Damage",         "Death",
    "Spells"};
constexpr std::array<std::string_view, kWeapons> kWeaponNames{
    "Gunrod (default)", "Shotgun", "Cannon", "Gatling"};
constexpr std::array<std::string_view, 3> kMeleeNames{
    "Strike", "Directional strike", "Alternate strike"};
constexpr std::array<std::string_view, 6> kFaces{"Face 0", "Face 1", "Face 3",
                                                 "Face 4", "Face 5", "Face 6"};
constexpr std::array<unsigned, 6> kFaceIds{0, 1, 3, 4, 5, 6};
constexpr std::array<std::string_view, 6> kJumps{
    "Standard",       "Alternate",   "Raised backward",
    "Raised forward", "Raised left", "Raised right"};
constexpr std::array<unsigned, 6> kJumpIds{0, 1, 8, 9, 10, 11};
constexpr std::array<std::string_view, 12> kDamageNames{
    "Light / front",   "Light / back",    "Light / left",
    "Light / right",   "Heavy / front",   "Heavy / back",
    "Heavy / left",    "Heavy / right",   "Crouched / front",
    "Crouched / back", "Crouched / left", "Crouched / right"};
constexpr std::array<std::string_view, 11> kDeaths{
    "Death group A - 1",        "Death group A - 2",       "Death group A - 3",
    "Death group A - 4",        "Death group A - 5",       "Death group A - 6",
    "Death group B - 1",        "Death group B - 2",       "Death group B - 3",
    "Crouched death - group A", "Crouched death - group B"};
constexpr std::array<std::string_view, 8> kSpells{
    "Ancient Wall", "Raven Panic", "Rose Bomb", "Will Power",
    "Sacrifice",    "Tornado",     "Meteor",    "Thunder"};
bool catalog_family(AnimationReviewKind kind) {
  return kind == AnimationReviewKind::body_clip ||
         kind == AnimationReviewKind::upper_clip ||
         kind == AnimationReviewKind::additive_clip ||
         kind == AnimationReviewKind::weapon_clip;
}
}  // namespace
PauseFlow::PauseFlow(unsigned costume) : costume_(costume) {
  if (costume >= kCostumes) {
    throw std::invalid_argument("Invalid pause costume");
  }
  populate(Page::root);
}
void PauseFlow::bind_review_catalogs(
    const animation::PreparedClipCatalog& body,
    const animation::PreparedClipCatalog& weapon, unsigned current_weapon,
    std::span<const MotionPoseSemantics> body_semantics,
    std::span<const MotionPoseSemantics> weapon_semantics) {
  if (current_weapon >= kWeapons || body.size() >= kNextPage ||
      weapon.size() >= kNextPage ||
      (!body_semantics.empty() && body_semantics.size() != body.size()) ||
      (!weapon_semantics.empty() && weapon_semantics.size() != weapon.size())) {
    throw std::invalid_argument("Invalid pause review catalog");
  }
  body_ = &body;
  weapon_ = &weapon;
  weapon_class_ = current_weapon;
  body_semantics_ = body_semantics;
  weapon_semantics_ = weapon_semantics;
  // Rebinding replaces stale borrowed labels after the atomic transaction,
  // before navigation or presentation can inspect the retired catalog.
  populate(Page::root);
}
void PauseFlow::append(std::string_view label, unsigned id, bool enabled) {
  presentation_.rows.at(presentation_.count++) = {label, {}, enabled, id};
}
void PauseFlow::message(std::string_view text) {
  const auto size = std::min(text.size(), message_.size() - 1);
  std::copy_n(text.begin(), size, message_.begin());
  message_.at(size) = '\0';
  presentation_.message = {message_.data(), size};
}
void PauseFlow::populate(Page page) {
  page_ = page;
  presentation_ = {};
  presentation_.page =
      page == Page::costumes ? MenuPage::costume : MenuPage::options;
  presentation_.message = "A: Select   B: Back   Menu / Esc: Resume";
  if (page == Page::root) {
    presentation_.title = "Paused";
    append("Resume", 0);
    append("Costume", 1);
    append("Weapon class (preview)", 2, body_ != nullptr);
    append("Animation review", 3, body_ != nullptr);
  } else if (page == Page::costumes) {
    presentation_.title = "Costume";
    presentation_.selected = costume_;
    for (unsigned i = 0; i < kCostumes; ++i) append("Costume", i);
  } else if (page == Page::weapons) {
    presentation_.title = "Weapon class - preview";
    presentation_.selected = weapon_class_;
    for (unsigned i = 0; i < kWeapons; ++i) append(kWeaponNames.at(i), i);
  } else if (page == Page::review) {
    presentation_.title = "Animation review";
    for (std::size_t i = 0; i < kReviews.size(); ++i) {
      append(kReviews.at(i), static_cast<unsigned>(i));
    }
  } else {
    presentation_.title = kReviews.at(static_cast<std::size_t>(family_));
    presentation_.message = "Presentation preview only - no combat outcomes";
    if (catalog_family(family_)) {
      const auto* catalog =
          family_ == AnimationReviewKind::weapon_clip ? weapon_ : body_;
      if (catalog == nullptr || catalog->size() == 0) {
        append("No prepared clips", 0, false);
        return;
      }
      presentation_.message =
          family_ == AnimationReviewKind::additive_clip
              ? "Additive only; incompatible or unresolved clips disabled"
              : "Ordinary only; incompatible or unresolved clips disabled";
      const auto first =
          std::min(catalog_page_ * kClipsPerPage, catalog->size() - 1);
      const auto last = std::min(first + kClipsPerPage, catalog->size());
      if (first != 0) append("Previous page", kPreviousPage);
      for (auto i = first; i < last; ++i) {
        const auto classifications = family_ == AnimationReviewKind::weapon_clip
                                         ? weapon_semantics_
                                         : body_semantics_;
        const auto semantics = classifications.empty()
                                   ? MotionPoseSemantics::unknown
                                   : classifications[i];
        const auto required = family_ == AnimationReviewKind::additive_clip
                                  ? MotionPoseSemantics::additive
                                  : MotionPoseSemantics::ordinary;
        append(catalog->name(i).substr(0, kClipLabelLimit),
               static_cast<unsigned>(i), semantics == required);
      }
      if (last != catalog->size()) append("Next page", kNextPage);
    } else {
      const auto rows = [this](auto& labels) {
        for (std::size_t i = 0; i < labels.size(); ++i) {
          append(labels.at(i), static_cast<unsigned>(i));
        }
      };
      switch (family_) {
        case AnimationReviewKind::melee:
          rows(kMeleeNames);
          break;
        case AnimationReviewKind::facial:
          for (std::size_t i = 0; i < kFaces.size(); ++i) {
            append(kFaces.at(i), kFaceIds.at(i));
          }
          break;
        case AnimationReviewKind::scripted_jump:
          for (std::size_t i = 0; i < kJumps.size(); ++i) {
            append(kJumps.at(i), kJumpIds.at(i));
          }
          break;
        case AnimationReviewKind::damage:
          rows(kDamageNames);
          break;
        case AnimationReviewKind::death:
          rows(kDeaths);
          break;
        case AnimationReviewKind::spell:
          rows(kSpells);
          break;
        default:
          throw std::logic_error("Invalid review submenu");
      }
    }
  }
}
void PauseFlow::select() {
  const auto& row = presentation_.rows.at(presentation_.selected);
  if (!row.enabled) return;
  const auto id = row.compatibility_id;
  if (page_ == Page::costumes) {
    if (id == costume_) {
      populate(Page::root);
    } else {
      request_ = id;
    }
  } else if (page_ == Page::weapons) {
    if (id == weapon_class_) {
      populate(Page::root);
    } else {
      weapon_request_ = id;
    }
  } else if (page_ == Page::root) {
    switch (id) {
      case 0:
        paused_ = false;
        break;
      case 1:
        populate(Page::costumes);
        break;
      case 2:
        populate(Page::weapons);
        break;
      case 3:
        populate(Page::review);
        break;
      default:
        throw std::logic_error("Invalid pause selection");
    }
  } else if (page_ == Page::review) {
    family_ = static_cast<AnimationReviewKind>(id);
    if (catalog_family(family_) || family_ == AnimationReviewKind::melee ||
        family_ >= AnimationReviewKind::facial) {
      catalog_page_ = 0;
      populate(Page::variants);
    } else {
      review_request_ = AnimationReviewRequest{family_, 0};
    }
  } else if (id == kPreviousPage || id == kNextPage) {
    if (id == kPreviousPage) {
      --catalog_page_;
    } else {
      ++catalog_page_;
    }
    populate(Page::variants);
  } else {
    review_request_ = AnimationReviewRequest{family_, id};
  }
}
bool PauseFlow::update(bool pause_pressed, MenuInput input) {
  const bool was_paused = paused_;
  if (pause_pressed) {
    paused_ = !paused_;
    request_.reset();
    weapon_request_.reset();
    review_request_.reset();
    populate(Page::root);
    return true;
  }
  if (!paused_) return false;
  if (request_ || weapon_request_ || review_request_) return true;
  if ((input.pressed & kCancel) != 0) {
    if (page_ == Page::root) {
      paused_ = false;
    } else {
      populate(page_ == Page::variants ? Page::review : Page::root);
    }
  } else if ((input.pressed & kConfirm) != 0) {
    select();
  } else if ((input.repeated & kUp) != 0) {
    presentation_.selected =
        (presentation_.selected + presentation_.count - 1) %
        presentation_.count;
  } else if ((input.repeated & kDown) != 0) {
    presentation_.selected = (presentation_.selected + 1) % presentation_.count;
  }
  return was_paused;
}
void PauseFlow::complete_costume(bool success) {
  if (!request_) throw std::logic_error("No pending costume selection");
  if (success) costume_ = *request_;
  request_.reset();
  populate(Page::root);
  if (!success) {
    message("Costume could not be loaded. Current costume retained.");
  }
}
void PauseFlow::complete_weapon(bool success) {
  if (!weapon_request_) throw std::logic_error("No pending weapon selection");
  if (success) weapon_class_ = *weapon_request_;
  weapon_request_.reset();
  populate(Page::root);
  if (!success) message("Weapon could not be loaded. Current weapon retained.");
}
void PauseFlow::complete_review(AnimationReviewResult result) {
  if (!review_request_) throw std::logic_error("No pending animation preview");
  review_request_.reset();
  if (result.accepted) {
    paused_ = false;
    populate(Page::root);
  } else {
    message(result.message);
  }
}
}  // namespace bw::game
