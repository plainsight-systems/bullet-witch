// Progression handoff observations verify the request owns its snapshot across
// later Profile mutation. Core C.9/I.30 design, E.4 (#re-design-invariants)
// implementation and both-phase MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) keep this fixed-value in-process check.
// V1: in-process startup-session lifecycle and title readiness contracts.
// Controlled media/surface owners outlive each session. Inputs are explicit
// frame deltas, pressed actions and resource readiness; observe visibility,
// retained-resource retirement, status and Wait. No native media, file I/O,
// rendering or original-root execution. Algorithm: hold readiness despite
// elapsed time/input; complete the accepted route through scheduler idle and
// destruction; separately abandon an active title to check owner cleanup.
// Bounded fixture work/storage. Design/implementation MCP: cpp-guidelines I.1
// (#ri-explicit) makes media/readiness explicit; cpp-performance COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) verifies
// exclusive operation retirement without asserting incidental cleanup counts.
#include "game/startup_services.hpp"

#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using bw::game::FlowStatus;
using bw::game::Screen;
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
template <class F>
void rejects(F operation, const char* message) {
  bool rejected = false;
  try {
    operation();
  } catch (const std::exception&) {
    rejected = true;
  }
  require(rejected, message);
}
class Media final : public bw::game::LogoMedia {
 public:
  bool open_movie(std::string_view path) override {
    paths.emplace_back(path);
    active = opens;
    return opens;
  }
  bool movie_complete() const override { return ended; }
  void stop_movie() noexcept override {
    active = false;
    ++stops;
  }
  void begin_fade(bw::game::FadeDirection, float) override {}
  bool fade_pending() const override { return fading; }
  bool opens{true}, ended{}, fading{}, active{};
  unsigned stops{};
  std::vector<std::string> paths;
};
class Surface final : public bw::game::TitleSurface {
 public:
  void begin() override {
    active = true;
    visible = false;
    begun = true;
  }
  bool ready() override {
    require(active, "Readiness requires retained title resources");
    return resources_ready;
  }
  void show(bool show_title,
            bw::game::TitlePresentation presentation) override {
    require(active, "Drawing requires retained title resources");
    visible = show_title;
    current = presentation;
  }
  void end() noexcept override {
    visible = false;
    active = false;
    current = {};
  }
  bw::game::TitlePresentation current;
  bool resources_ready{}, active{}, visible{}, begun{};
};
class Saves final : public bw::platform::SaveStorage {
 public:
  bw::platform::SaveRead read(std::string_view name, std::size_t) override {
    const auto& bytes = file(name);
    return bytes.empty()
               ? bw::platform::SaveRead{bw::platform::SaveStatus::missing, {}}
               : bw::platform::SaveRead{bw::platform::SaveStatus::ok, bytes};
  }
  bw::platform::SaveStatus write(std::string_view name,
                                 std::span<const std::byte> bytes) override {
    auto& destination = file(name);
    destination.assign(bytes.begin(), bytes.end());
    ++writes;
    return bw::platform::SaveStatus::ok;
  }
  std::vector<std::byte>& file(std::string_view name) {
    if (name == "BulletWitchSystem") return system;
    if (name == "BulletWitchRecord") return record;
    if (name == "BulletWitchSaveData") return checkpoint;
    throw std::invalid_argument("Unexpected startup profile file");
  }
  std::vector<std::byte> system, record, checkpoint;
  unsigned writes{};
};
class MenuSurface final : public bw::game::MenuSurface {
 public:
  void begin() override { active = true; }
  bool ready() override { return active; }
  void show(const bw::game::MenuPresentation& view) override { current = view; }
  void end() noexcept override { active = false; }
  bw::game::MenuPresentation current;
  bool active{};
};
class PowerUpOutput final : public bw::game::PowerUpOutput {
 public:
  void begin() override { active = true; }
  bool ready() override { return active; }
  void show(const bw::game::PowerUpPresentation& value) override {
    current = value;
  }
  void end() noexcept override { active = false; }
  bw::game::PowerUpPresentation current;
  bool active{};
};
template <class Ready>
void advance_to(bw::game::StartupSession& session, Ready ready,
                const char* reason) {
  constexpr unsigned kMaximumFrames = 600;
  for (unsigned frame = 0; frame < kMaximumFrames && !ready(); ++frame) {
    (void)session.advance({1.F / 60.F, 0});
  }
  require(ready(), reason);
}
void load_profile(bw::game::StartupSession& session, MenuSurface& surface,
                  bool creates_content) {
  using bw::game::FrontendDialog;
  session.execute(Screen::character_load);
  if (creates_content) {
    advance_to(
        session,
        [&] {
          return surface.current.dialog == FrontendDialog::profile_created;
        },
        "Fresh profile waits through creation before its acknowledgment");
    require(surface.current.storage_background &&
                surface.current.dialog_interactive,
            "Creation acknowledgment is the original interactive storage page");
    constexpr float kDialogAdmission = .4F;
    (void)session.advance({kDialogAdmission, 0});
    (void)session.advance({0, 0x1000});
    advance_to(
        session,
        [&] {
          return surface.current.dialog ==
                 FrontendDialog::profile_enable_autosave;
        },
        "Creation acknowledgment precedes the original autosave question");
    (void)session.advance({kDialogAdmission, 0});
    (void)session.advance({0, 0x1000});
  } else {
    advance_to(
        session,
        [&] {
          return surface.current.dialog == FrontendDialog::profile_loading;
        },
        "Existing profile follows its original timed load page");
    require(!surface.current.dialog_interactive,
            "Loading status does not fabricate a confirmation prompt");
  }
  advance_to(
      session, [&] { return !session.pending(Screen::character_load); },
      "Profile load retires after original timing and dialog decisions");
  (void)session.advance({});
}
void frontend_handoff() {
  Media media;
  Saves saves;
  MenuSurface surface;
  bw::game::Profile profile;
  bw::game::FrontEnd frontend(profile, saves, media, surface, {});
  bw::game::StartupSession session(media, nullptr, &frontend);
  rejects([&] { session.start_mission("st01_00"); },
          "Mission cannot precede confirmed selection");
  load_profile(session, surface, true);
  require(profile.autosave_enabled() && !profile.has_checkpoint(),
          "Fresh startup applies the selected autosave choice without a "
          "checkpoint");
  session.execute(Screen::title_menu);
  for (unsigned i = 0; i < 5; ++i) (void)session.advance({});
  require(surface.current.page == bw::game::MenuPage::main && surface.active,
          "Profile completion leads to real menu policy");
  advance_to(
      session,
      [&] {
        return surface.current.main.motion == bw::game::MainMenuMotion::idle;
      },
      "Main menu finishes authored entry before input");
  (void)session.advance({0, 0x1010});
  advance_to(
      session,
      [&] { return surface.current.page == bw::game::MenuPage::difficulty; },
      "New game publishes difficulty after main exit");
  require(surface.current.page == bw::game::MenuPage::difficulty,
          "New game opens difficulty");
  advance_to(
      session,
      [&] {
        return surface.current.child.motion ==
               bw::game::ChildMenuMotion::active;
      },
      "Difficulty finishes its authored entry before confirmation");
  (void)session.advance({0, 0x1010});
  advance_to(
      session, [&] { return !session.pending(Screen::title_menu); },
      "Confirmed mission leaves the menu after its authored exit");
  require(!session.pending(Screen::title_menu) &&
              session.status(Screen::title_menu) == 1 && !surface.active,
          "Menu completes and retires presentation before mission handoff");
  session.set_global(0, 1);
  rejects([&] { session.start_mission("st02_00"); },
          "Mismatched Lua mission rejected");
  session.start_mission("st01_00");
  require(
      session.gameplay_request() &&
          session.gameplay_request()->mission == "st01_00" &&
          session.gameplay_request()->selection.difficulty == 1 &&
          session.gameplay_request()->checkpoint.empty() &&
          session.gameplay_request()->progression == profile.progression() &&
          session.mission_pending(),
      "Owned new-game request preserves choice without inventing "
      "checkpoint or mission success");
  const auto& captured_request = session.gameplay_request();
  if (!captured_request) throw std::runtime_error("Missing captured request");
  const auto captured_progression = captured_request->progression;
  (void)profile.award_result({0, 0, bw::game::MissionRewardMode::campaign});
  profile.set_current_difficulty(2);
  require(session.gameplay_request() &&
              session.gameplay_request()->selection.difficulty == 1 &&
              session.gameplay_request()->progression == captured_progression &&
              profile.progression() != captured_progression,
          "Captured request is independent of later profile changes");
  rejects([&] { session.start_mission("st01_00"); },
          "Duplicate handoff rejected");
  rejects([&] { (void)session.mission_status(); },
          "Active mission cannot report retirement");
  using bw::game::MissionTransition;
  rejects(
      [&] { session.validate_mission_transition({MissionTransition::none}); },
      "Empty transition cannot acknowledge teardown");
  constexpr unsigned kLiveCostume = 5, kNextLiveCostume = 2;
  const auto permanent_costume = profile.costume();
  const auto permanent_progression = profile.progression();
  const auto writes_before_reentry = saves.writes;
  session.validate_mission_transition(
      {MissionTransition::restart_stage, kLiveCostume});
  require(session.mission_pending() && session.gameplay_request() &&
              session.gameplay_request()->progression == captured_progression,
          "Validation before teardown leaves entry observations unchanged");
  session.complete_mission_transition(
      {MissionTransition::restart_stage, kLiveCostume});
  require(
      session.mission_pending() && session.gameplay_request() &&
          session.gameplay_request()->mission == "st01_00" &&
          session.gameplay_request()->selection.costume == kLiveCostume &&
          session.gameplay_request()->progression == profile.progression() &&
          session.gameplay_request()->checkpoint.empty(),
      "Stage restart retains committed live costume and keeps root waiting");
  session.complete_mission_transition(
      {MissionTransition::checkpoint, kNextLiveCostume});
  require(
      session.mission_pending() && session.gameplay_request() &&
          session.gameplay_request()->mission == "st01_00" &&
          session.gameplay_request()->selection.costume == kNextLiveCostume &&
          profile.costume() == permanent_costume &&
          profile.progression() == permanent_progression &&
          saves.writes == writes_before_reentry,
      "Both reentry paths keep live appearance without Profile/save writes");
  rejects([&] { session.stop_mission_music(); },
          "Reentry does not release root music boundary");
  session.complete_mission_retirement();
  require(!session.mission_pending(), "Real retirement releases root wait");
  rejects([&] { (void)session.mission_status(); },
          "Root music boundary precedes result consumption");
  session.stop_mission_music();
  require(session.mission_status() == 2,
          "Retirement is abort status, never mission success");
  rejects([&] { session.complete_mission_retirement(); },
          "Duplicate retirement rejected");
  rejects([&] { session.unload_concept_mission(); },
          "Campaign cannot consume concept unload");
}
void checkpoint_reentry_boundary() {
  Media media;
  Saves saves;
  bw::game::Profile saved_profile;
  require(saved_profile.create_defaults(saves).status ==
              bw::platform::SaveStatus::ok,
          "Checkpoint fixture includes original System and Record files");
  saves.checkpoint.resize(307200);
  const auto word = [&](std::size_t offset, unsigned value) {
    for (unsigned index = 0; index < 4; ++index) {
      saves.checkpoint.at(offset + index) =
          static_cast<std::byte>((value >> (index * 8U)) & 255U);
    }
  };
  word(0, 0x10001);
  word(4, 0x8e0);
  word(8, 0x500);
  word(12, 0x30);
  word(16, 0x3a4);
  word(20, 0x530);
  saves.checkpoint.at(0x30) = std::byte{1};
  MenuSurface surface;
  bw::game::Profile profile;
  bw::game::FrontEnd frontend(profile, saves, media, surface, {});
  bw::game::StartupSession session(media, nullptr, &frontend);
  load_profile(session, surface, false);
  session.execute(Screen::title_menu);
  for (unsigned i = 0; i < 5; ++i) (void)session.advance({});
  advance_to(
      session,
      [&] {
        return surface.current.main.motion == bw::game::MainMenuMotion::idle;
      },
      "Continue menu finishes authored entry");
  (void)session.advance({}, 0x02000000);
  (void)session.advance({0, 0x1010});
  advance_to(
      session,
      [&] {
        return surface.current.dialog ==
               bw::game::FrontendDialog::continue_game;
      },
      "Continue opens the original load confirmation");
  constexpr unsigned kDialogFrames = 24;
  for (unsigned frame = 0; frame < kDialogFrames; ++frame) {
    (void)session.advance({1.F / 60.F, 0});
  }
  (void)session.advance({0, 0x1010});
  advance_to(
      session, [&] { return !session.pending(Screen::title_menu); },
      "Continue completes its source departure");
  session.start_mission("st01_00");
  using bw::game::MissionTransition;
  rejects(
      [&] {
        session.validate_mission_transition({MissionTransition::checkpoint});
      },
      "Imported checkpoint cannot pass pre-teardown restore admission");
  rejects(
      [&] {
        session.complete_mission_transition({MissionTransition::checkpoint});
      },
      "Unsupported checkpoint cannot be acknowledged as restored");
  require(session.mission_pending() && session.gameplay_request() &&
              !session.gameplay_request()->checkpoint.empty(),
          "Failed checkpoint admission preserves original request and wait");
  session.complete_mission_transition(
      {MissionTransition::restart_stage, profile.costume()});
  require(session.mission_pending() && session.gameplay_request() &&
              session.gameplay_request()->checkpoint.empty() &&
              profile.has_checkpoint(),
          "Stage restart clears runtime checkpoint but preserves saved bytes");
}
void power_up_task_lifecycle() {
  Media media;
  Saves saves;
  MenuSurface surface;
  PowerUpOutput output;
  bw::game::Profile profile;
  bw::game::FrontEnd frontend(profile, saves, media, surface, {}, &output);
  bw::game::StartupSession session(media, nullptr, &frontend);
  load_profile(session, surface, true);
  (void)profile.award_result({0, 0});
  session.execute(Screen::power_up);
  require(session.pending(Screen::power_up), "Power-up Exec queues activation");
  const auto settle_power_up = [&] {
    for (unsigned i = 0; i < 90; ++i) (void)session.advance({1.F / 60.F, 0});
  };
  settle_power_up();
  require(
      output.current.page == bw::game::PowerUpPage::categories && output.active,
      "Native task owns visible purchase policy");
  rejects([&] { frontend.start(Screen::title_menu); },
          "Another front-end cannot steal an active power-up surface");
  for (unsigned i = 0; i < 3; ++i) {
    (void)session.advance({1.F / 60.F, 0x1010});
    settle_power_up();
  }
  require(profile.progression().levels.stats[0] == 2 &&
              profile.progression().points == 150 && profile.dirty(),
          "Confirmed screen purchase reaches authoritative Record");
  for (unsigned i = 0; i < 2; ++i) {
    (void)session.advance({1.F / 60.F, 0x2020});
    settle_power_up();
  }
  (void)session.advance({1.F / 60.F, 0x2020});
  media.fading = true;
  settle_power_up();
  require(session.pending(Screen::power_up) && output.active,
          "Power-up cannot complete before conceal");
  media.fading = false;
  for (unsigned i = 0; i < 5; ++i) (void)session.advance({});
  require(
      !session.pending(Screen::power_up) && !output.active && profile.dirty(),
      "Wait observes task retirement; later original save still owns I/O");
}
void title_readiness_and_lifecycle() {
  Media media;
  Surface surface;
  {
    bw::game::StartupSession session(media, &surface);
    session.execute(Screen::title);
    require(session.pending(Screen::title) && !surface.begun,
            "Title Exec registers without synchronous activation");
    media.ended = true;
    for (unsigned frame = 0; frame < 3; ++frame) {
      (void)session.advance({100, 0x1010});
    }
    require(surface.active && !surface.visible && media.active &&
                session.pending(Screen::title) &&
                session.progress(Screen::title).status == FlowStatus::pending &&
                session.status(Screen::title) == 0,
            "Missing readiness cannot fabricate completion from input, time or "
            "movie end");
    media.ended = false;
    surface.resources_ready = true;
    for (unsigned frame = 0; frame < 3; ++frame) {
      (void)session.advance({});
    }
    require(
        surface.visible && surface.active && session.status(Screen::title) == 0,
        "Ready title becomes visible without inheriting blocked input or time");
    (void)session.advance({0, 0x1010});
    for (unsigned frame = 0;
         frame < 20 &&
         session.progress(Screen::title).status != FlowStatus::complete;
         ++frame) {
      (void)session.advance({0.25F, 0});
    }
    require(session.progress(Screen::title).status == FlowStatus::complete &&
                session.status(Screen::title) == 1 && !surface.active &&
                !surface.visible && !media.active,
            "Accepted title publishes outcome one and retires both surface and "
            "movie");
    require(session.pending(Screen::title),
            "Title result does not bypass the scheduler idle transition");
    (void)session.advance({});
    require(!session.pending(Screen::title),
            "Title Wait observes idle and requests destruction");
    (void)session.advance({});
    require(!surface.active && !media.active,
            "Task destruction preserves retired presentation state");
    session.execute(Screen::title);
    (void)session.advance({});
    require(
        surface.active && media.active && session.status(Screen::title) == 0,
        "Re-execution starts fresh resources and clears published outcome");
  }
  require(!surface.active && !surface.visible && !media.active,
          "Session destruction retires an active title and its movie");
}
void session_lifecycle() {
  Media media;
  bw::game::StartupSession session(media);
  require(!session.window_close_enabled(), "Close inhibited before activation");
  rejects([&] { (void)session.pending(Screen::company_logo); },
          "Wait without Exec must fail");
  session.execute(Screen::company_logo);
  require(session.pending(Screen::company_logo),
          "Newly requested task is pending before activation");
  require(!session.window_close_enabled(), "Exec does not activate inline");
  rejects([&] { session.execute(Screen::company_logo); },
          "Duplicate Exec must fail");
  (void)session.advance({});
  require(session.window_close_enabled(), "Activation permits window close");
  (void)session.advance({});
  require(media.paths.size() == 1 &&
              media.paths.front() == "data/movie/marvelousLogo.ssu",
          "Recovered first logo path");
  for (int i = 0; i < 3; ++i) {
    (void)session.advance({});
  }
  require(session.pending(Screen::company_logo),
          "Movie completion is a real gate");
  media.ended = true;
  for (int i = 0; i < 20 && session.progress(Screen::company_logo).status !=
                                FlowStatus::complete;
       ++i) {
    (void)session.advance({});
  }
  require(
      session.progress(Screen::company_logo).status == FlowStatus::complete &&
          session.status(Screen::company_logo) == 1 && media.stops == 2,
      "Both logos completed and released playback");
  require(media.paths.size() == 2 &&
              media.paths.back() == "data/movie/xeedLogo.ssu",
          "Recovered second logo path");
  require(session.pending(Screen::company_logo),
          "Result alone does not bypass pending lifecycle transition");
  (void)session.advance({});
  require(!session.pending(Screen::company_logo),
          "Wait observes idle and requests destruction");
  (void)session.advance({});
  session.execute(Screen::memory_card);
  for (int i = 0; i < 7; ++i) {
    (void)session.advance({});
  }
  require(
      session.progress(Screen::memory_card).status == FlowStatus::complete &&
          session.pending(Screen::memory_card),
      "Startup check completion also waits for idle lifecycle");
  (void)session.advance({});
  require(!session.pending(Screen::memory_card),
          "Startup check Wait completes");
  (void)session.advance({});
  session.execute(Screen::memory_card);
  require(session.pending(Screen::memory_card),
          "Re-execution must not reuse stale result");
}
void session_failures_and_globals() {
  Media media;
  bw::game::StartupSession session(media);
  require(session.global(1) == 0,
          "Cold startup initializes title return request");
  session.set_global(1, 1);
  require(session.global(1) == 1, "Slot one maps to title return request");
  session.set_global(1, 0);
  require(session.global(1) == 0, "Root can consume title return request");
  rejects(
      [&] { session.set_global(1, std::numeric_limits<double>::infinity()); },
      "Nonfinite session value rejected");
  rejects([&] { (void)session.global(0); },
          "Unimplemented stage owner rejected");
  rejects([&] { session.execute(Screen::title); },
          "Title requires an explicit presentation capability");
  rejects([&] { (void)session.latest_stage(); }, "No fabricated saved stage");
  rejects([&] { (void)session.advance({-1, 0}); }, "Invalid delta rejected");
  media.opens = false;
  session.execute(Screen::company_logo);
  (void)session.advance({});
  (void)session.advance({});
  require(session.progress(Screen::company_logo).status == FlowStatus::failed &&
              media.stops == 1,
          "Movie-open failure releases owned media and records failure");
  rejects([&] { (void)session.pending(Screen::company_logo); },
          "Failed movie cannot satisfy a successful wait");
  (void)session.advance({});
  rejects([&] { (void)session.pending(Screen::company_logo); },
          "Idle lifecycle cannot conceal flow failure");
}
}  // namespace
void startup_session_contracts() {
  frontend_handoff();
  checkpoint_reentry_boundary();
  power_up_task_lifecycle();
  session_lifecycle();
  session_failures_and_globals();
  title_readiness_and_lifecycle();
}
