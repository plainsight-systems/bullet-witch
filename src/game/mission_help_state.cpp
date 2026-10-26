// Compose live permanent observations with the explicitly selected development
// entry context. The original campaign grants supply a bounded topic set;
// ability topics follow acquired effective levels, never blanket visibility.
// Permanent read commands and mission-only read commands retain separate
// stores. Both-phase Core I.1/C.9 and MEM.9 apply as documented in the header.
#include "mission_help_state.hpp"

#include <stdexcept>

namespace bw::game {
namespace {
// Campaign startup, clear and encounter grants recovered from st01–st06.
// Topic 18 is absent from these producers and the native acquisition table.
constexpr std::array<unsigned, 23> kCampaignTopics{
    0,  1,  2,  3,  4,  5,  6,  7,  8,  19, 20, 21,
    22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
constexpr unsigned kFirstAbilityTopic = 9;
}  // namespace
HelpProgress MissionHelpState::progress() const {
  auto result = saved_.progress();
  if (player_ &&
      player_->origin() == ProgressionOrigin::development_completed) {
    for (const auto topic : kCampaignTopics) result.available.at(topic) = true;
    const auto& spells = player_->levels().spells;
    for (std::size_t spell = 0; spell < spells.size(); ++spell) {
      if (spells.at(spell) != 0) {
        result.available.at(kFirstAbilityTopic + spell) = true;
      }
    }
  }
  for (std::size_t topic = 0; topic < inherited_read_.size(); ++topic) {
    result.read.at(topic) = result.read.at(topic) || inherited_read_.at(topic);
  }
  return result;
}
void MissionHelpState::mark_read(unsigned topic) {
  if (topic >= help_topic_count || !progress().available.at(topic)) {
    throw std::invalid_argument("Help topic is unavailable in this mission");
  }
  if (saved_.progress().available.at(topic)) {
    saved_.mark_read(topic);
  } else {
    inherited_read_.at(topic) = true;
  }
}
}  // namespace bw::game
