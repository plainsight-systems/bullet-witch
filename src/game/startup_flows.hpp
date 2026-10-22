// G2: reconstructed company-logo and PC startup-check update policies.
// Inputs are one finite nonnegative game delta and pressed action bits per
// update. Outputs describe visibility/result; completion requests task removal,
// not destruction or presentation completion. See docs/startup-runtime.md.
// LogoMedia is exclusively borrowed until flow destruction; no concurrent
// calls. The caller owns activation side effects, scheduling, draw and event
// pumping. Algorithm: execute one recovered state branch per update, then add
// delta to phase clocks. Logo gates poll real media/fade observations; check UI
// phases advance without invented storage work or delays. Missing capabilities
// throw; logo failures become terminal diagnostics, never successful native
// results. Costs: O(total path bytes) construction, O(1) flow work per update;
// no normal update allocations. No measured speed claim. Fixed identity
// prevents double cleanup of the exclusively borrowed media operation. Design
// and implementation MCP: cpp-guidelines I.1 (#ri-explicit) requires explicit
// observations and owner lifetimes; cpp-performance COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) prevents
// duplicate media ownership. Moves are also disabled to preserve identity.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace bw::game {
enum class FlowStatus : std::uint8_t { idle, pending, complete, failed };
struct ScreenProgress {
  FlowStatus status{FlowStatus::idle};
  bool visible{};
  unsigned result{};
};
enum class FadeDirection : std::uint8_t { reveal, conceal };
class LogoMedia {
 public:
  LogoMedia() = default;
  virtual ~LogoMedia() = default;
  LogoMedia(const LogoMedia&) = delete;
  LogoMedia& operator=(const LogoMedia&) = delete;
  LogoMedia(LogoMedia&&) = delete;
  LogoMedia& operator=(LogoMedia&&) = delete;
  // False means an attempted native-equivalent open failed. Unsupported host
  // capability must throw, not return false and silently skip all logos.
  virtual bool open_movie(std::string_view path) = 0;
  virtual bool movie_complete() const = 0;
  virtual void stop_movie() noexcept = 0;
  virtual void begin_fade(FadeDirection direction, float duration) = 0;
  virtual bool fade_pending() const = 0;
};
struct LogoUpdate {
  float delta{};
  std::uint32_t pressed_actions{};
};
class CompanyLogoFlow {
 public:
  // Paths must be nonempty and in recovered table order; no default playlist
  // is guessed. Media may borrow a path through stop_movie().
  CompanyLogoFlow(LogoMedia& media, std::vector<std::string> movie_paths);
  ~CompanyLogoFlow();
  CompanyLogoFlow(const CompanyLogoFlow&) = delete;
  CompanyLogoFlow& operator=(const CompanyLogoFlow&) = delete;
  CompanyLogoFlow(CompanyLogoFlow&&) = delete;
  CompanyLogoFlow& operator=(CompanyLogoFlow&&) = delete;
  void start();
  void update(LogoUpdate input);
  ScreenProgress progress() const { return progress_; }
  const std::string& diagnostic() const { return diagnostic_; }
  float phase_elapsed() const { return elapsed_; }

 private:
  enum class Phase : std::uint8_t {
    initialize,
    open,
    revealing,
    playing,
    conceal,
    concealing,
    hide,
    finish
  };
  void transition(Phase phase);
  void stop() noexcept;
  std::reference_wrapper<LogoMedia> media_;
  std::vector<std::string> paths_;
  std::size_t index_{};
  Phase phase_{Phase::initialize};
  ScreenProgress progress_;
  float elapsed_{};
  bool owns_movie_{};
  std::string diagnostic_;
};
// Descriptive names for native secondary phase values 0, 500, 1000, 1300.
// They are UI markers, not milliseconds or evidence of storage I/O.
enum class CheckUiPhase : std::uint8_t { reset, reveal, hold, close };
class StartupCheckFlow {
 public:
  void start();
  void update(float delta);
  ScreenProgress progress() const { return progress_; }
  CheckUiPhase ui_phase() const { return ui_phase_; }
  float phase_elapsed() const { return elapsed_; }
  float ui_elapsed() const { return ui_elapsed_; }

 private:
  enum class Phase : std::uint8_t {
    show,
    hold,
    close,
    begin_close,
    closing,
    hide,
    finish
  };
  ScreenProgress progress_;
  Phase phase_{Phase::show};
  CheckUiPhase ui_phase_{CheckUiPhase::reset};
  float elapsed_{}, ui_elapsed_{};
};
}  // namespace bw::game
