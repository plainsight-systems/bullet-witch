# Audio bank construction reference

October 23, 2026. This supports the One Shot Kill loading continuation, not a
claim that gameplay audio is implemented. The game's XACT 3.7/XAudio2 2.7
boundary is mapped in [audio and movies](audio-and-movies.md).

Inspected FAudio/FACT tag 26.10 at commit
[`6839b88e304a046ae1a609ff14a085371e02a4e0`](https://github.com/FNA-XNA/FAudio/tree/6839b88e304a046ae1a609ff14a085371e02a4e0).
The local checkout is ignored under `research/references/faudio`.
It remains a reference dependency, not linked or copied production code.

`src/FACT.c`, `FACTAudioEngine_CreateSoundBank`, calls
`FACT_INTERNAL_ParseSoundBank` under the API lock. The parser constructs the
sound/cue/variation/transition/name metadata and registers the bank in the engine
list. `FACTAudioEngine_CreateInMemoryWaveBank` similarly parses wave metadata and
registers a prepared-bank notification. Its parser retains an input stream and
wave entry/seek information. Neither constructor starts source voices or decodes
all wave payloads. Data construction and later cue execution are distinct.

`FACTAudioEngine_Initialize` separately parses global settings, establishes the
audio engine/mastering voice, configures optional reverb and starts runtime
work. `ShutDown` stops work and audio before destroying banks and device objects.
The presence of CPU-only bank constructors is not proof that an uninitialized
FACT engine is a supported application API, nor that retaining raw bank bytes
reproduces its constructed metadata.

For source reconstruction, compare the original load callback's actual
postconditions with this separation before deciding that loading requires a
complete cue mixer. A bounded native bank definition/registry may implement the
construction boundary if it preserves those postconditions; playback readiness
must remain a separate contract. Adopting FACT as a runtime would instead require
an explicit audio adapter, pinned dependencies and allocator/device lifetime
integration. No such runtime choice is made by this reference inspection.

The reviewed checkout uses the zlib license. Its source is retained only as an
ignored reference; project implementation and verification must separately
record any future reuse and its terms.

## Implemented construction boundary

The native `_LoadSound` callback reaches `0x44d920`, which creates an in-memory
wave bank (`0x507c10`) and sound bank (`0x507b50`), registers their handles and
reads the cue count (`0x507490`). It does not play a cue. `0x5074d0` first opens
the game-specific `FBSC` wrapper: a count, eight- or twelve-byte cue descriptors,
and an embedded XACT sound bank. Nonpositive descriptor parameters request a
global engine default; the reconstructed definition preserves that request
without guessing the not-yet-recovered runtime value.

`AudioBankDefinition` now constructs wave/seek ranges, sounds, tracks, events,
wave choices, parameter-code references and cue/name associations. Metadata
uses the resource domain; encoded waves and strings borrow retained input
owners. The mission publishes its bank slot only after both definitions and
all cue/sound/wave links validate. Cancellation destroys metadata before its
archive owner. This is bank-definition readiness, not gameplay audio playback
or resolution of global RPC/DSP settings.

The bounded reader supports little-endian version-46 noncompact in-memory wave
banks and version-46 sound banks with one wave-bank dependency and direct-sound
cues. Supported track events include stop, play-wave, wave/effect variation,
pitch/volume equations and ramps, and markers. Other bank variants, cue
variations and cue transitions fail explicitly; they are not silently accepted.
This first constructor is driven by the selected mission's real bank while its
record algorithms apply to any input within that declared family.

Actual `STAGE4` construction yielded six waves, five cues, five sounds, five
tracks/events and eleven RPC references. All five cue names match the wrapper
names. Metadata charges return to zero after destruction. This probe does not
claim sample decoding, audible behavior or native device acceptance. Ignored
inputs/probe live under `research/evidence/reconstruction/mission-launch/ancillary/`;
source exports are under `research/evidence/engine/pass-12-bank-conversion/`.
