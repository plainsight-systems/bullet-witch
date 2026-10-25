// Consult mission user-pause admission before pumping the coroutine. This is
// distinct from normal world tick admission and root/control service updates.
// C.9/I.1 design, E.4 implementation, both-phase MEM.9: one borrowed authority,
// no allocation or clock sampling in the gate.
// Mission spell restriction binding borrows the explicit mission player owner
// through VM teardown. Only unchanged Lua publication changes the mask: load
// completion does not run start() or preapply mission-specific constants.
// Core I.30/C.9 design and E.4 (#re-design-invariants) implementation validate
// before publication; both-phase MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) adds no frame storage allocation.
// MissionDriver implements the header's script/load ownership boundary.
// Preserve the original VM load order and 256 KiB heap; load completion stops
// before the original actor/world setup gates. Unsupported work remains pending
// with a named blocked reason; exceptions become terminal failure. Cancellation
// destroys bridge captures before cancelling services/releasing resource tags.
// Design/implementation E.6 (#re-raii) retains all owners during pending work;
// cpp-performance MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) allocates
// context/package/string storage from the injected runtime domain. O(package
// bytes) setup, bounded pump work; no script/package reload per poll.
#include "mission_driver.hpp"

#include <array>
#include <cmath>
#include <stdexcept>

#include "bw/formats/named_file_package.hpp"

namespace bw::game {
namespace {
constexpr std::size_t kMissionHeapBytes = 0x40000;
constexpr std::size_t kMaximumScriptName = 31;
void count(std::span<const script::Value> args, std::size_t expected) {
  if (args.size() != expected) {
    throw std::invalid_argument("Mission native argument count");
  }
}
std::string_view name(std::span<const script::Value> args) {
  count(args, 1);
  const auto* text = std::get_if<std::string>(&args[0]);
  if (text == nullptr || text->empty()) {
    throw std::invalid_argument("Mission native name required");
  }
  return *text;
}
}  // namespace
MissionDriver::MissionDriver(
    resources::ResourceService& resources, MissionLoadServices& loads,
    resources::ResourceTag tag, std::string_view script_name,
    const std::shared_ptr<memory::Domain>& runtime_memory,
    const MissionControl& control, MissionPlayerState* player)
    : resources_(resources),
      loads_(loads),
      tag_(tag),
      memory_(runtime_memory),
      script_name_(script_name, memory::Allocator<char>(runtime_memory)),
      package_name_(memory::Allocator<char>(runtime_memory)),
      stage_name_(memory::Allocator<char>(runtime_memory)),
      player_(player),
      control_(control) {
  if (script_name.empty() || script_name.size() > kMaximumScriptName ||
      script_name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") !=
          std::string_view::npos) {
    throw std::invalid_argument("Invalid mission script identity");
  }
  package_name_ = "script.";
  package_name_ += script_name_;
  package_name_ += ".dpk";
  context_.emplace(memory_, kMissionHeapBytes);
  bindings();
  // Roll back partial tag registration if the second dependency is unavailable.
  try {
    resources_.request(package_name_, tag_);
    resources_.request("script.default_callback.dpk", tag_);
  } catch (...) {
    resources_.release(tag_);
    throw;
  }
}
MissionDriver::~MissionDriver() { cancel(); }
void MissionDriver::cancel() noexcept {
  context_.reset();
  loads_.cancel();
  resources_.release(tag_);
  progress_.phase = MissionPhase::cancelled;
}
resources::NamedFilePackage MissionDriver::load_package(ScriptPackage source) {
  if (!context_) {
    throw std::logic_error("Mission context is not live");
  }
  const auto lease = resources_.acquire(source.resource);
  if (!lease) throw std::runtime_error("Mission script package unpublished");
  auto package = resources::NamedFilePackage::parse(lease->bytes(), memory_);
  context_.value().load_binary(package.member(source.member));
  return package;
}
void MissionDriver::bindings() {
  if (!context_) {
    throw std::logic_error("Mission context is not live");
  }
  context_.value().bind("_SetMagicMask", [this](auto args) {
    count(args, 1);
    const auto* value = std::get_if<double>(&args[0]);
    constexpr unsigned kAllSpellBits = 0x1ff;
    if (player_ == nullptr || value == nullptr || !std::isfinite(*value) ||
        *value < 0 || *value > kAllSpellBits || std::floor(*value) != *value) {
      throw std::invalid_argument("Invalid mission spell restriction");
    }
    player_->set_script_disabled_spells(static_cast<std::uint32_t>(*value));
    return script::Reply{};
  });
  context_.value().bind("_GetCheckPoint", [](auto args) {
    count(args, 0);
    // This driver currently admits fresh concept missions, never Continue
    // saves.
    return script::Reply{{0.0}, {}};
  });
  context_.value().bind("_ExecLoadingScene", [this](auto args) {
    count(args, 1);
    const auto* value = std::get_if<double>(&args[0]);
    constexpr unsigned kLoadingItems = 19;
    if (value == nullptr || !std::isfinite(*value) || *value < 0 ||
        *value >= kLoadingItems || std::floor(*value) != *value) {
      throw std::invalid_argument("Invalid loading-scene item");
    }
    loads_.begin_loading_scene(static_cast<unsigned>(*value));
    return script::Reply{};
  });
  context_.value().bind("_SetRiffName", [this](auto args) {
    stage_name_ = name(args);
    // Copy the stage name on first entry and preserve the original single
    // yield.
    return script::Reply{{}, [first = true]() mutable {
                           if (first) {
                             first = false;
                             return true;
                           }
                           return false;
                         }};
  });
  struct Binding {
    const char* symbol;
    MissionLoadKind kind;
  };
  constexpr std::array bindings{
      Binding{"_LoadCharaLight", MissionLoadKind::character_light},
      Binding{"_LoadSky", MissionLoadKind::sky},
      Binding{"_LoadStage", MissionLoadKind::stage},
      Binding{"_LoadTextBinary", MissionLoadKind::text},
      Binding{"_LoadScoreFile", MissionLoadKind::score},
      Binding{"_LoadKakurePoint", MissionLoadKind::hiding_places},
      Binding{"_LoadSound", MissionLoadKind::sound},
      Binding{"_LoadSettbl_SoundEnv", MissionLoadKind::sound_environment},
      Binding{"_LoadSettbl_SoundReverb", MissionLoadKind::sound_reverb},
      Binding{"_LoadSettbl_Magic", MissionLoadKind::magic},
      Binding{"_LoadBgObj", MissionLoadKind::background_objects},
      Binding{"_LoadPathFile", MissionLoadKind::paths},
      Binding{"_LoadSettbl", MissionLoadKind::placements}};
  for (const auto& binding : bindings) {
    context_.value().bind(
        binding.symbol, [this, kind = binding.kind](auto args) {
          loads_.request(kind, name(args));
          return script::Reply{{}, [this] {
                                 const auto result = loads_.poll();
                                 progress_.diagnostic = result.diagnostic;
                                 if (result.state == MissionLoadState::failed) {
                                   throw std::runtime_error(result.diagnostic);
                                 }
                                 progress_.phase =
                                     result.state == MissionLoadState::blocked
                                         ? MissionPhase::blocked
                                         : MissionPhase::loading;
                                 return result.state != MissionLoadState::ready;
                               }};
        });
  }
}
const MissionProgress& MissionDriver::advance() {
  if (!control_.mission_script_admitted()) return progress_;
  if (progress_.phase == MissionPhase::failed ||
      progress_.phase == MissionPhase::cancelled ||
      progress_.phase == MissionPhase::awaiting_world_setup) {
    return progress_;
  }
  try {
    if (!context_) {
      throw std::logic_error("Mission context is not live");
    }
    if (progress_.phase == MissionPhase::scripts) {
      const auto report = resources_.pump(tag_);
      if (report.state == resources::LoadState::failed) {
        throw std::runtime_error(report.diagnostic);
      }
      if (report.state != resources::LoadState::ready) return progress_;
      memory::String member(script_name_, memory::Allocator<char>(memory_));
      member += ".lub";
      loads_.retain_package(load_package({package_name_, member}));
      (void)load_package(
          {"script.default_callback.dpk", "default_callback.lub"});
      if (!context_) {
        throw std::logic_error("Mission context is not live");
      }
      context_.value().start("load");
      progress_.phase = MissionPhase::loading;
    }
    if (!context_) {
      throw std::logic_error("Mission context is not live");
    }
    const auto result = context_.value().pump();
    if (result.state == script::RunState::failed) {
      throw std::runtime_error(result.diagnostic);
    }
    if (result.state == script::RunState::complete) {
      progress_ = {MissionPhase::awaiting_world_setup,
                   "Mission load completed; original actor/world setup gates "
                   "are not implemented"};
    }
  } catch (const std::exception& failure) {
    progress_ = {MissionPhase::failed, failure.what()};
  }
  return progress_;
}
}  // namespace bw::game
