// Preserve the original defeat task's scalar tracks, input priority and typed
// completion; prepare authored art/text once and emit ordered bounded quads.
// Core E.6 (#re-raii), retrieved for implementation, unwinds partial residency;
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps
// update/show allocation-free. Shared original_sprite_tint reproduces D3D9
// COLOR clamp; the source highlight is repeated alpha draws, not an invented
// bloom pass.
#include "mission_game_over.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "bw/content/formats/bitmap_font.hpp"
#include "bw/content/formats/text_table.hpp"
#include "bw/content/formats/world_texture.hpp"
#include "game/ui/common/localized_text.hpp"
#include "game/ui/common/sprite_color.hpp"
#include "game/ui/common/sprite_text.hpp"

namespace bw::game {
namespace {
constexpr float kBackgroundRate = .45F, kPromptRate = 2.7F, kChoicesRate = 3.1F,
                kTolerance = .01F, kConfirmationSeconds = .3F,
                kFadeSeconds = .5F;
constexpr std::uint32_t kConfirm = 0x1010, kLeft = 1U << 26, kRight = 1U << 27;
constexpr resources::ResourceTag kTag = 27;
constexpr std::array<std::string_view, 4> kGroups{
    "sprite.gameover", "sprite.font", "sprite.system", "sprite.text.sys"};
constexpr std::array<std::string_view, 3> kKeys{"0018", "0001", "0002"};
constexpr unsigned kBackground = 370, kButton = 371, kSolid = 10000;
constexpr std::size_t kImageCapacity = 4, kTextCapacity = 256;
constexpr float kHalf = .5F, kCenterX = 640, kPromptY = 283,
                kChoiceOffset = 192, kChoiceY = 380, kPromptSize = 64,
                kChoiceSize = 32, kInactiveAlpha = .8F, kPulseLight = .35F;
constexpr render::Color kText{230.F / 255, 228.F / 255, 213.F / 255, 1};
bool unit(float value) {
  return std::isfinite(value) && value >= 0 && value <= 1;
}
memory::Allocator<std::byte> storage_for(const resources::ResourceService& r) {
  const auto domain = r.memory_domain();
  return domain ? memory::Allocator<std::byte>(domain)
                : memory::Allocator<std::byte>{};
}
}  // namespace
void MissionGameOverFlow::begin() {
  view_ = {.phase = MissionGameOverPhase::background};
  phase_elapsed_ = pulse_elapsed_ = 0;
  sounds_ = {};
}
void MissionGameOverFlow::update(float seconds, MenuInput input) {
  if (!std::isfinite(seconds) || seconds < 0) {
    throw std::invalid_argument("Invalid game over presentation interval");
  }
  sounds_ = {};
  const auto phase = view_.phase;
  if (phase == MissionGameOverPhase::inactive ||
      phase == MissionGameOverPhase::complete || seconds == 0) {
    return;
  }
  // State predicates observe the prior published channel, as native task update
  // does before advancing its scalar channels in the common epilogue.
  if (phase == MissionGameOverPhase::background &&
      1 - view_.background_alpha < kTolerance) {
    view_.phase = MissionGameOverPhase::prompt;
  } else if (phase == MissionGameOverPhase::prompt &&
             1 - view_.prompt_alpha < kTolerance) {
    view_.phase = MissionGameOverPhase::choices;
  } else if (phase == MissionGameOverPhase::choices &&
             1 - view_.choices_alpha < kTolerance) {
    view_.phase = MissionGameOverPhase::interactive;
  } else if (phase == MissionGameOverPhase::interactive) {
    if ((input.pressed & kConfirm) != 0) {
      view_.phase = MissionGameOverPhase::confirming;
      phase_elapsed_ = 0;
      sounds_.add(MenuSound::panel);
    } else if ((input.repeated & (kLeft | kRight)) != 0) {
      // Native selector disables wrap; left takes priority if both repeat.
      view_.restart = (input.repeated & kLeft) != 0;
      sounds_.add(MenuSound::cursor);
    }
  } else if (phase == MissionGameOverPhase::confirming &&
             phase_elapsed_ >= kConfirmationSeconds) {
    view_.phase = MissionGameOverPhase::fading;
    phase_elapsed_ = 0;
  } else if (phase == MissionGameOverPhase::fading &&
             phase_elapsed_ >= kFadeSeconds) {
    view_.phase = MissionGameOverPhase::complete;
  }
  view_.background_alpha =
      std::min(1.F, view_.background_alpha + kBackgroundRate * seconds);
  if (view_.phase >= MissionGameOverPhase::prompt) {
    view_.prompt_alpha =
        std::min(1.F, view_.prompt_alpha + kPromptRate * seconds);
  }
  if (view_.phase >= MissionGameOverPhase::choices) {
    view_.choices_alpha =
        std::min(1.F, view_.choices_alpha + kChoicesRate * seconds);
  }
  if (view_.phase < MissionGameOverPhase::confirming) {
    view_.selection_pulse =
        kHalf -
        kHalf * std::cos(2 * std::numbers::pi_v<float> * pulse_elapsed_);
  }
  view_.confirmation_pulse =
      view_.phase == MissionGameOverPhase::confirming
          ? std::sin(std::numbers::pi_v<float> *
                     std::min(1.F, phase_elapsed_ / kConfirmationSeconds))
          : 0;
  if (view_.phase >= MissionGameOverPhase::fading) {
    view_.fade_alpha = std::min(1.F, phase_elapsed_ / kFadeSeconds);
  }
  pulse_elapsed_ = std::fmod(pulse_elapsed_ + seconds, 1.F);
  phase_elapsed_ += seconds;
}
std::optional<MissionGameOverOutcome> MissionGameOverFlow::outcome() const {
  if (view_.phase != MissionGameOverPhase::complete) return std::nullopt;
  return view_.restart ? MissionGameOverOutcome::restart
                       : MissionGameOverOutcome::return_to_menu;
}
MenuSoundRequests MissionGameOverFlow::take_sounds() {
  const auto result = sounds_;
  sounds_ = {};
  return result;
}
struct MissionGameOverSurface::State {
  struct Image {
    unsigned id{};
    render::TextureHandle texture;
    float width{}, height{};
  };
  resources::ResourceService& resources;
  render::SpriteOutput& output;
  memory::Allocator<std::byte> allocator;
  std::array<std::optional<resources::ResourceLease>, kGroups.size()> leases;
  std::array<Image, kImageCapacity> images{};
  std::size_t image_count{};
  std::optional<formats::BitmapFont> font;
  std::array<std::array<char, kTextCapacity>, kKeys.size()> texts{};
  std::array<render::Sprite, render::kMaximumSprites> commands{};
  std::size_t count{};
  bool begun{}, prepared{};
  State(resources::ResourceService& r, render::SpriteOutput& o)
      : resources(r), output(o), allocator(storage_for(r)) {}
  ~State() { end(); }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;
  void end() noexcept {
    if (begun) output.clear_sprites();
    while (image_count != 0) {
      output.release_texture(images.at(--image_count).texture);
    }
    font.reset();
    for (auto& lease : leases) lease.reset();
    resources.release(kTag);
    begun = prepared = false;
    count = 0;
  }
  void begin() {
    end();
    begun = true;
    for (const auto group : kGroups) resources.request(group, kTag);
  }
  const Image& image(unsigned id) const {
    const auto admitted = std::span(images).first(image_count);
    const auto found =
        std::find_if(admitted.begin(), admitted.end(),
                     [id](const Image& i) { return i.id == id; });
    if (found == admitted.end()) {
      throw std::runtime_error("Original game over artwork missing");
    }
    return *found;
  }
  bool ready() {
    if (prepared) return true;
    if (!begun) throw std::logic_error("Game over surface has not begun");
    const auto report = resources.pump(kTag);
    if (report.state == resources::LoadState::failed) {
      throw std::runtime_error(report.diagnostic);
    }
    if (report.state != resources::LoadState::ready) return false;
    constexpr std::size_t kFontGroup = 1, kTextGroup = 3;
    for (std::size_t group = 0; group < kGroups.size(); ++group) {
      leases.at(group) = resources.acquire(kGroups.at(group));
      if (!leases.at(group)) throw std::runtime_error("Game over asset absent");
      const auto& lease = *leases.at(group);
      if (group == kTextGroup) {
        const auto table = formats::TextTable::parse(lease.bytes());
        for (std::size_t i = 0; i < kKeys.size(); ++i) {
          localized_text::prepare_text(
              localized_text::system_text(table, kKeys.at(i)), texts.at(i));
        }
        continue;
      }
      const bool is_font = group == kFontGroup;
      if (is_font) font = formats::BitmapFont::parse(lease.bytes(), allocator);
      const auto& archive = lease.archive();
      std::size_t index = 0;
      for (std::size_t record = 0; record < archive.records().size();
           ++record) {
        if (archive.records()[record].type != "DDS ") continue;
        constexpr std::size_t kIdentityBytes = 2;
        constexpr unsigned kByteBits = 8;
        if (!is_font && index >= lease.bytes().size() / kIdentityBytes) {
          throw std::runtime_error("Game over sprite identity missing");
        }
        const auto id =
            is_font
                ? 0U
                : static_cast<unsigned>(lease.bytes()[index * kIdentityBytes]) |
                      (static_cast<unsigned>(
                           lease.bytes()[index * kIdentityBytes + 1])
                       << kByteBits);
        ++index;
        if (!is_font && id != kBackground && id != kButton) continue;
        if (image_count == images.size()) {
          throw std::length_error("Game over texture capacity exceeded");
        }
        for (std::size_t prior = 0; prior < image_count; ++prior) {
          if (images.at(prior).id == id) {
            throw std::runtime_error("Duplicate game over sprite identity");
          }
        }
        const auto decoded = formats::decode_world_texture(
            {archive.payload(record), archive.metadata(record)}, allocator);
        if (is_font &&
            (index != 1 || !font || decoded.width != font->atlas_width() ||
             decoded.height != font->atlas_height())) {
          throw std::runtime_error("Game over font atlas mismatch");
        }
        images.at(image_count++) = {id, output.upload_texture(decoded),
                                    static_cast<float>(decoded.width),
                                    static_cast<float>(decoded.height)};
      }
    }
    (void)image(kBackground);
    (void)image(kButton);
    (void)image(0);
    constexpr std::array<std::uint8_t, 4> kWhite{255, 255, 255, 255};
    images.at(image_count++) = {kSolid, output.upload_texture(1, 1, kWhite), 1,
                                1};
    prepared = true;
    return true;
  }
  void centered_button(float x, render::Color tint) {
    const auto& art = image(kButton);
    commands.at(count++) = {
        art.texture,
        {x - art.width * kHalf, kChoiceY - art.height * kHalf, art.width,
         art.height},
        {0, 0, 1, 1},
        tint};
  }
  void text(std::size_t index, float x, float y, float size,
            render::Color tint) {
    if (!font) throw std::logic_error("Game over font is absent");
    count += append_centered_sprite_text(
        *font, image(0).texture, texts.at(index).data(), {x, y, size}, tint,
        std::span(commands).subspan(count));
  }
  void show(const MissionGameOverPresentation& view) {
    if (!prepared) throw std::logic_error("Game over assets not prepared");
    if (!unit(view.background_alpha) || !unit(view.prompt_alpha) ||
        !unit(view.choices_alpha) || !unit(view.selection_pulse) ||
        !unit(view.confirmation_pulse) || !unit(view.fade_alpha) ||
        view.phase > MissionGameOverPhase::complete) {
      throw std::invalid_argument("Invalid game over presentation");
    }
    count = 0;
    if (view.phase != MissionGameOverPhase::inactive) {
      const auto& bg = image(kBackground);
      commands.at(count++) = {bg.texture,
                              {0, 0, bg.width, bg.height},
                              {0, 0, 1, 1},
                              {1, 1, 1, view.background_alpha}};
      auto prompt = kText;
      prompt.alpha = view.prompt_alpha;
      text(0, kCenterX, kPromptY, kPromptSize, prompt);
      const auto inactive = render::Color{
          kText.red * kHalf, kText.green * kHalf, kText.blue * kHalf,
          kInactiveAlpha * view.choices_alpha};
      for (std::size_t choice = 0; choice < 2; ++choice) {
        const auto x =
            kCenterX + (choice == 0 ? -kChoiceOffset : kChoiceOffset);
        const bool selected = (choice == 0) == view.restart;
        centered_button(x, selected ? render::Color{1, 1, 1, view.choices_alpha}
                                    : inactive);
      }
      const auto selected_x =
          kCenterX + (view.restart ? -kChoiceOffset : kChoiceOffset);
      const auto light = -kPulseLight + kPulseLight * view.selection_pulse;
      const auto base = render::Color{1, 1, 1, view.choices_alpha};
      centered_button(selected_x,
                      original_sprite_tint(base, {light, light, light, 0}));
      const auto confirm = std::min(1.F, light + view.confirmation_pulse);
      centered_button(selected_x, original_sprite_tint(
                                      base, {confirm, confirm, confirm, 0}));
      for (std::size_t choice = 0; choice < 2; ++choice) {
        auto tint = (choice == 0) == view.restart ? kText : inactive;
        if ((choice == 0) == view.restart) tint.alpha = view.choices_alpha;
        text(choice + 1,
             kCenterX + (choice == 0 ? -kChoiceOffset : kChoiceOffset),
             kChoiceY, kChoiceSize, tint);
      }
      if (view.fade_alpha > 0) {
        commands.at(count++) = {
            image(kSolid).texture,
            {0, 0, render::kCanvasWidth, render::kCanvasHeight},
            {0, 0, 1, 1},
            {0, 0, 0, view.fade_alpha}};
      }
    }
    output.set_sprites(std::span(commands).first(count));
  }
};
MissionGameOverSurface::MissionGameOverSurface(resources::ResourceService& r,
                                               render::SpriteOutput& o)
    : state_(std::allocate_shared<State>(storage_for(r), r, o)) {}
MissionGameOverSurface::~MissionGameOverSurface() = default;
void MissionGameOverSurface::begin() {
  try {
    state_->begin();
  } catch (...) {
    state_->end();
    throw;
  }
}
bool MissionGameOverSurface::ready() {
  try {
    return state_->ready();
  } catch (...) {
    state_->end();
    throw;
  }
}
void MissionGameOverSurface::show(const MissionGameOverPresentation& view) {
  state_->show(view);
}
void MissionGameOverSurface::end() noexcept { state_->end(); }
}  // namespace bw::game
