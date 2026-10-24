// Public game receiver contracts: prepared payload interpretation, original
// entry guards, reset latches and explicit unresolved consumers. In-process
// byte fixtures use the same bounded parse API; no external assets/effects.
// Core SL.con.3 and Performance CDSA.32 apply as in production contracts.
#include "game/motion_presentation.hpp"

#include <memory_resource>

#include "game/character_audio.hpp"
#include "game/facial_animation.hpp"
#include "game/source_random.hpp"
#include "model_fixture.hpp"
#include "shape_fixture.hpp"
namespace {
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
class Backing final : public bw::memory::CpuBacking {
 public:
  std::size_t calls{};
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    try {
      ++calls;
      return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    } catch (const std::bad_alloc&) {
      return nullptr;
    }
  }
  void deallocate(void* pointer, std::size_t bytes,
                  std::size_t alignment) noexcept override {
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
};
class Sink final : public bw::game::MotionPresentationSink {
 public:
  std::size_t visibility_calls{}, face_calls{}, voice_calls{},
      unsupported_calls{}, sound_calls{}, resets{};
  bw::game::SoundMotionCommand last_sound;
  bw::game::VisibilityMotionCommand last_visibility;
  bw::game::FacialMotionCommand last_face;
  bw::game::VoiceMotionCommand last_voice;
  void visibility(bw::game::VisibilityMotionCommand command) noexcept override {
    ++visibility_calls;
    last_visibility = command;
  }
  void facial(bw::game::FacialMotionCommand command) noexcept override {
    ++face_calls;
    last_face = command;
  }
  void sound(
      bw::game::SoundMotionCommand command,
      bw::animation::MotionEventPhase phase,
      const bw::animation::MotionEventContext& context) noexcept override {
    (void)phase;
    (void)context;
    ++sound_calls;
    last_sound = command;
  }
  void voice(bw::game::VoiceMotionCommand command) noexcept override {
    ++voice_calls;
    last_voice = command;
  }
  void unsupported(bw::animation::MotionEventPhase,
                   const bw::animation::MotionEventContext&) noexcept override {
    ++unsupported_calls;
  }
  void reset_presentation() noexcept override { ++resets; }
};
struct Fixture {
  bw::resources::Bytes bytes = bw::resources::Bytes(44);
  Fixture() { model_fixture::word(bytes, 0, 0x30415046); }
  struct Record {
    std::uint32_t channel, tag, stride, start_offset, end_offset, value_offset,
        value;
  };
  void add(Record record) {
    const auto [channel, tag, stride, start_offset, end_offset, value_offset,
                value] = record;
    const auto section = static_cast<std::uint32_t>(bytes.size());
    bytes.resize(bytes.size() + 12 + stride);
    model_fixture::word(bytes, 4 + 4 * channel, section);
    model_fixture::word(bytes, section, tag);
    model_fixture::word(bytes, section + 4, 1);
    model_fixture::word(bytes, section + 8, section + 12);
    model_fixture::word(bytes, section + 12 + start_offset, 1);
    if (end_offset != 0) {
      model_fixture::word(bytes, section + 12 + end_offset, 3);
    }
    model_fixture::word(bytes, section + 12 + value_offset, value);
  }
};
}  // namespace
namespace {
void facial_banks_test() {
  const auto backing = std::make_shared<Backing>();
  const auto domain = bw::memory::Domain::create(backing);
  {
    const auto offset_bytes = shape_fixture::offsets();
    const auto model = bw::formats::ModelDefinition::parse(
        bw::resources::ResourceArchive::parse(model_fixture::archive(
            {.shape_offsets = offset_bytes, .shape = true})),
        domain);
    const bw::animation::PreparedModelShapes prepared(model, domain);
    const auto clip_bytes = shape_fixture::shape_clip();
    bw::content::CharacterSupport support(domain);
    support.shapes.push_back(
        {bw::content::CharacterPublication(
             "model.pw0.shape.nocompress.pw0_1", {},
             bw::memory::Allocator<std::byte>(domain)),
         bw::formats::ShapeAnimationDefinition::parse(clip_bytes, domain)});
    bw::game::FacialAnimation face(support, prepared, domain);
    bw::game::FacialAnimation replacement(support, prepared, domain);
    const auto no_shape_model = bw::formats::ModelDefinition::parse(
        bw::resources::ResourceArchive::parse(model_fixture::archive()),
        domain);
    const bw::animation::PreparedModelShapes no_shapes(no_shape_model, domain);
    bw::game::FacialAnimation absent(support, no_shapes, domain);
    const auto allocations = backing->calls;
    using Admission = bw::game::FacialAdmission;
    const bw::game::FacialMotionCommand request{0, 2, 1, 2};
    require(face.command(request, true) == Admission::applied,
            "Facial expression admission");
    face.advance(1.0 / 30.0);
    require(face.weights()[0] == .5F,
            "FAC duration controls expression frame rate");
    face.advance(1.0 / 30.0);
    require(face.weights()[0] == 1,
            "Nonrepeat player expression reaches endpoint");
    face.advance(1);
    require(face.weights()[0] == 1,
            "Player factory nonrepeat override holds endpoint");
    require(face.command(request, true) == Admission::applied,
            "Replacement retains outgoing face");
    face.advance(1.0 / 30.0);
    require(face.weights()[0] == 1,
            "Outgoing half fade plus incoming half expression");
    replacement.copy_state_from(face);
    replacement.advance(0);
    require(replacement.weights()[0] == face.weights()[0],
            "Paused costume copies clocks, not stale weights");
    require(face.command({0, 2, 0, 2}, true) == Admission::retired,
            "Zero weight retires current face");
    face.advance(2.0 / 30.0);
    require(face.weights()[0] == 0,
            "Source frame fade retires both outgoing entries");
    face.reset();
    for (int i = 0; i < 4; ++i) {
      require(face.command(request, true) == Admission::applied,
              "Bounded facial pool admission");
    }
    require(face.command(request, true) == Admission::exhausted,
            "FAC does not force overwrite full pool");
    require(absent.command(request, true) == Admission::absent_shapes,
            "Absent costume reports geometry capability while retaining "
            "expression clocks");
    absent.advance(1.0 / 30.0);
    replacement.copy_state_from(absent);
    replacement.advance(0);
    require(absent.weights().empty() && replacement.weights()[0] == .5F,
            "Returning from an absent costume preserves logical facial timing");
    face.reset_motion_banks();
    face.advance(0);
    require(face.weights()[0] == 0 && backing->calls == allocations,
            "Face reset and all callbacks allocate nothing");
  }
  require(domain->snapshot().live_bytes == 0,
          "Facial policy releases owned prepared storage");
}
}  // namespace
void motion_presentation_tests() {
  bw::game::SourceRandom random;
  require(
      random.next(100) == 70 && random.state() == 69070 && random.draws() == 1,
      "Shared game RNG preserves source recurrence and positive modulo");
  const auto state = random.state();
  bool invalid = false;
  try {
    (void)random.next(0);
  } catch (const std::invalid_argument&) {
    invalid = true;
  }
  require(invalid && random.state() == state && random.draws() == 1,
          "Invalid random bounds cannot consume shared game state");

  facial_banks_test();
  namespace game = bw::game;
  namespace animation = bw::animation;
  const auto backing = std::make_shared<Backing>();
  const auto domain = bw::memory::Domain::create(backing);
  {
    Fixture fixture;
    fixture.add({0, 0x53455f30, 52, 40, 44, 32, 9});
    fixture.add({1, 0x564f5f30, 52, 40, 44, 32, 77});
    fixture.add({3, 0x46414330, 48, 36, 40, 32, 11000005});
    fixture.add({4, 0x43414e30, 36, 32, 0, 32, 1});
    fixture.add({5, 0x494e5430, 40, 32, 36, 32, 1});
    fixture.add({8, 0x56495330, 40, 32, 0, 36, 1000});
    const auto events = bw::formats::MotionEvents::parse(fixture.bytes, domain);
    const game::PreparedMotionPresentation prepared(events, domain);
    require(prepared.ordinary_channels()[5].records.empty(),
            "Ordinary owner leaves interrupt reader unbound");
    Sink sink;
    game::MotionPresentationReceiver receiver(prepared, sink);
    animation::EventTimeline timeline(prepared.ordinary_channels());
    const auto allocations = backing->calls;
    timeline.advance(1, receiver);
    require(receiver.cancellation_enabled(), "Cancellation event latches");
    require(sink.visibility_calls == 1 && sink.last_visibility.target == 1000 &&
                sink.last_visibility.enabled,
            "Visibility is a listener identity, not a guessed body group");
    require(sink.face_calls == 1 && sink.last_face.expression == 0 &&
                sink.last_face.bank == 2 && sink.last_face.weight == 1 &&
                sink.last_face.duration == 5,
            "Packed facial expression preserves bank, weight and duration");
    require(sink.voice_calls == 1 && sink.last_voice.identifier == 77,
            "Voice identifier preserved");
    require(sink.sound_calls == 2 && sink.last_sound.action == 9 &&
                sink.unsupported_calls == 0,
            "Typed sound reports enter and update without guessing a cue");
    timeline.advance(4, receiver);
    require(sink.face_calls == 1 && sink.voice_calls == 1,
            "Entry consumers do not replay during updates");
    timeline.advance(1, receiver);
    require(sink.face_calls == 1 && sink.voice_calls == 1 &&
                sink.visibility_calls == 1,
            "Wrapped stale receiver entries retain previous-float guards");
    timeline.reset(receiver);
    require(!receiver.cancellation_enabled() && sink.resets == 1,
            "Reset clears latches and scoped effects");
    require(backing->calls == allocations,
            "Event dispatch and reset do not allocate");
    auto context =
        animation::MotionEventContext{3, 0, 0x46414330, {0, 0, {}}, 4, 0};
    receiver.receive(animation::MotionEventPhase::enter, context);
    receiver.receive(animation::MotionEventPhase::enter, context);
    require(sink.face_calls == 2,
            "Initial zero face interval is admitted only once");
    context.channel = 0;
    context.tag = 0x53455f30;
    const auto sound_calls = sink.sound_calls;
    receiver.receive(animation::MotionEventPhase::enter, context);
    receiver.receive(animation::MotionEventPhase::enter, context);
    require(sink.sound_calls == sound_calls + 1,
            "Sound has an independent initial zero interval latch");
    const auto ordinary = game::PreparedCharacterAudio::sound(0, 1);
    const auto metal = game::PreparedCharacterAudio::sound(0, 3);
    const auto direct = game::PreparedCharacterAudio::sound(1100, {});
    require(ordinary && ordinary->bank == 0 && ordinary->cue == 0x16c &&
                metal && metal->cue == 0x1c4 && direct && direct->cue == 0x74,
            "Native sound tables distinguish surface actions from direct IDs");
    require(
        !game::PreparedCharacterAudio::sound(0, {}) &&
            !game::PreparedCharacterAudio::sound(3039, 1),
        "Unknown source surface and out-of-range direct ID stay unavailable");
  }
  require(domain->snapshot().live_bytes == 0,
          "Prepared receiver resources retire");
}
