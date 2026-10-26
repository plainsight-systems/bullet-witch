// Narrow C ABI for the pinned FAudio codec hook. Prepared immutable sample
// storage is published before playback and retired after native shutdown.
// The callback never allocates, decodes, throws, locks or changes bank state.
// Failed lookups are counted for the control-thread error boundary.
// Design/implementation E.6 and MEM.9 follow the shared audio contracts.
#ifndef BW_AUDIO_FACT_WMA_BRIDGE_H
#define BW_AUDIO_FACT_WMA_BRIDGE_H
#include <FACT.h>
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Pinned FACT teardown omits per-track RPC arrays; release them under its lock.
void bw_fact_destroy_sound_bank(FACTSoundBank* bank);
// Retained-memory I/O preserves the original streaming bank schema.
uint32_t bw_fact_create_stream_bank(FACTAudioEngine* engine, void* data,
                                    size_t bytes, FACTWaveBank** bank);
void bw_fact_destroy_wave_bank(FACTWaveBank* bank);
void bw_fact_release_engine(FACTAudioEngine* engine);
// Supply the same injected allocator to the mixer as the cue engine.
uint32_t bw_fact_initialize(FACTAudioEngine* engine,
                            const FACTRuntimeParameters* parameters);
// Create a prepared PCM voice on the same FAudio device owned by FACT.
uint32_t bw_fact_create_music_voice(FACTAudioEngine* engine,
                                    const FAudioWaveFormatEx* format,
                                    FAudioSourceVoice** voice);
// Original role gain multiplies authored wave/RPC/category volume. Holds FACT's
// API lock while publishing and refreshing existing active/upcoming waves.
// The pinned patch carries this scalar into every subsequently prepared wave.
uint32_t bw_fact_set_cue_gain(FACTCue* cue, float gain);
int bw_fact_copy_samples(const uint8_t* encoded, size_t encoded_bytes,
                         uint32_t channels, uint32_t rate, size_t first_frame,
                         float* destination, size_t frames);
#ifdef __cplusplus
}
#endif
#endif
