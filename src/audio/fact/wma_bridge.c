// Project codec adaptation for pinned FAudio, not an upstream implementation.
// The mixer retains loop/cursor ownership; prepared PCM makes repeated
// lookahead and loop reads idempotent. No codec or allocation enters its native
// callback. E.6/MEM.9 are applied through the shared audio header contracts.
#include "audio/fact/wma_bridge.h"

#include <limits.h>

#include "FACT_internal.h"

uint32_t FAudio_WMADEC_init(FAudioSourceVoice* voice, uint32_t type) {
  if (type != FAUDIO_FORMAT_WMAUDIO2) return FAUDIO_E_INVALID_CALL;
  // Non-owning presence marker: upstream tests this field before dispatching
  // sample-range reads. All actual storage belongs to the prepared bank.
  voice->src.wmadec = (struct FAudioWMADEC*)voice;
  return 0;
}
void FAudio_WMADEC_free(FAudioSourceVoice* voice) { voice->src.wmadec = NULL; }
void FAudio_WMADEC_end_buffer(FAudioSourceVoice* voice) { (void)voice; }
void decode_wma(FAudioVoice* voice, struct queued_buffer* buffer,
                float* destination, uint32_t samples) {
  const uint8_t* encoded = buffer->buffer.pAudioData;
  size_t encoded_bytes = buffer->buffer.AudioBytes;
  // Pinned FACT's WMA streaming path caches one entire compressed wave.
  // Resolve its immutable source entry rather than the temporary cache pointer.
  // The callback is FACT-owned for every voice admitted by this backend.
  const FACTWaveCallback* callback =
      (const FACTWaveCallback*)voice->src.callback;
  const FACTWave* wave = callback->wave;
  if (wave->parentBank->streaming) {
    const FACTWaveBankEntry* entry = &wave->parentBank->entries[wave->index];
    encoded = FAudio_memptr(wave->parentBank->io, entry->PlayRegion.dwOffset);
    encoded_bytes = entry->PlayRegion.dwLength;
  }
  if (!bw_fact_copy_samples(encoded, encoded_bytes,
                            voice->src.format->nChannels,
                            voice->src.format->nSamplesPerSec,
                            voice->src.curBufferOffset, destination, samples)) {
    FAudio_zero(destination,
                (size_t)samples * voice->src.format->nChannels * sizeof(float));
  }
}

// FACT26.10 parses each track RPC code array but only releases sound RPCs.
// Destroy active consumers under the existing recursive API lock, then close
// those omitted allocations before the unchanged native bank destructor.
void bw_fact_destroy_sound_bank(FACTSoundBank* bank) {
  if (!bank) return;
  FACTAudioEngine* engine = bank->parentEngine;
  FAudioMutex mutex = engine->apiLock;
  FAudio_PlatformLockMutex(mutex);
  while (bank->cueList) FACTCue_Destroy(bank->cueList);
  for (uint16_t sound = 0; sound < bank->soundCount; ++sound) {
    for (uint16_t track = 0; track < bank->sounds[sound].trackCount; ++track) {
      engine->pFree((void*)bank->sounds[sound].tracks[track].rpc_codes.codes);
    }
  }
  FACTSoundBank_Destroy(bank);
  FAudio_PlatformUnlockMutex(mutex);
}

uint32_t bw_fact_initialize(FACTAudioEngine* engine,
                            const FACTRuntimeParameters* parameters) {
  FAudio* audio = NULL;
  uint32_t status = FAudioCreateWithCustomAllocatorEXT(
      &audio, 0, FAUDIO_DEFAULT_PROCESSOR, engine->pMalloc, engine->pFree,
      engine->pRealloc);
  if (status != 0) return status;
  FACTRuntimeParameters configured = *parameters;
  configured.pXAudio2 = audio;
  status = FACTAudioEngine_Initialize(engine, &configured);
  // FACT assumes ownership once it assigns the supplied mixer. Parse failure
  // happens before that assignment, so release the unattached mixer here.
  if (engine->audio != audio) FAudio_Release(audio);
  return status;
}

// Joining while Release holds apiLock can deadlock its worker waiting on that
// lock. Join first; preserve and close the bank locks that ShutDown clears.
void bw_fact_release_engine(FACTAudioEngine* engine) {
  FAudioMutex sound_lock = engine->sbLock;
  FAudioMutex wave_lock = engine->wbLock;
  FACTAudioEngine_ShutDown(engine);
  FAudio_PlatformDestroyMutex(sound_lock);
  FAudio_PlatformDestroyMutex(wave_lock);
  FACTAudioEngine_Release(engine);
}

// Streaming I/O remains resident for the admitted cue closure. FACT borrows
// streaming handles, so this adapter closes the memory cursor after the bank.
uint32_t bw_fact_create_stream_bank(FACTAudioEngine* engine, void* data,
                                    size_t bytes, FACTWaveBank** bank) {
  if (bytes > INT_MAX) return FAUDIO_E_INVALID_CALL;
  FACTStreamingParameters parameters = {0};
  parameters.file = FAudio_memopen(data, (int)bytes);
  if (!parameters.file) return FAUDIO_E_OUT_OF_MEMORY;
  uint32_t result =
      FACTAudioEngine_CreateStreamingWaveBank(engine, &parameters, bank);
  if (result != 0) FAudio_close(parameters.file);
  return result;
}
void bw_fact_destroy_wave_bank(FACTWaveBank* bank) {
  void* stream = bank->streaming ? bank->io : NULL;
  FACTWaveBank_Destroy(bank);
  if (stream) FAudio_close(stream);
}
