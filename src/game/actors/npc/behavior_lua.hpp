// Synchronous original Lua score adapter for one actor evaluation scope.
// Borrows a prepared VM and copied numeric context identity; neither world
// ownership nor mutable actor access escapes. Caller loads the matching wrapper
// and binds actual source observation functions before selection. A numeric
// first return narrows to float as 502e30 does; other scalar returns score
// zero. Missing functions, script errors or suspension throw, never imply a
// valid candidate. Script Context owns its instruction/heap limits. No caller
// byte views are retained beyond evaluate. Core I.1/SL.C.1 keep invocation and
// error boundaries explicit; MEM.9 uses fixed argument metadata. Both phases
// read with Context/selection design. No new AI cadence or replacement action
// policy. Level preparation borrows a separate synchronous mutation capability
// which must outlive the scorer and must not reenter selection or Lua. Original
// level-random refresh happens before a full descent, never per Lua getter.
// This extension reread Core I.1/E.4 and MEM.9 in both phases.
#pragma once
#include "behavior_selection.hpp"
#include "bw/script/context.hpp"
namespace bw::game {
class BehaviorLevelPreparation {
 public:
  BehaviorLevelPreparation() = default;
  virtual ~BehaviorLevelPreparation() = default;
  BehaviorLevelPreparation(const BehaviorLevelPreparation&) = delete;
  BehaviorLevelPreparation& operator=(const BehaviorLevelPreparation&) = delete;
  BehaviorLevelPreparation(BehaviorLevelPreparation&&) = delete;
  BehaviorLevelPreparation& operator=(BehaviorLevelPreparation&&) = delete;
  virtual void prepare_level() = 0;
};
float evaluate_behavior(script::Context&, std::uint32_t actor_identity,
                        const formats::BehaviorNode&);
class BehaviorLuaScoring final : public BehaviorScoring {
 public:
  BehaviorLuaScoring(script::Context& context, std::uint32_t actor_identity,
                     BehaviorLevelPreparation& preparation)
      : context_(context),
        actor_identity_(actor_identity),
        preparation_(preparation) {}
  void begin_level() override;
  float evaluate(const formats::BehaviorNode&) override;

 private:
  script::Context& context_;
  std::uint32_t actor_identity_;
  BehaviorLevelPreparation& preparation_;
};
}  // namespace bw::game
