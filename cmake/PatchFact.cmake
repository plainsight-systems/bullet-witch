# Bounded role-gain adaptation for the exact pinned FACT revision. The original
# XACT output-voice gain API is a success-returning TODO in this revision.
# Preserve authored wave/RPC/category/fade volume and multiply a separately
# owned cue scalar at FAudioVoice_SetVolume. No timing, decoding or categories
# change. The bridge publishes under FACT's existing recursive API lock.
function(bw_fact_replace path before after)
  file(READ "${path}" body)
  string(FIND "${body}" "${after}" patched)
  if(NOT patched EQUAL -1)
    return()
  endif()
  string(FIND "${body}" "${before}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "Unexpected pinned FACT source: ${path}")
  endif()
  string(REPLACE "${before}" "${after}" body "${body}")
  file(WRITE "${path}" "${body}")
endfunction()
bw_fact_replace("${bw_audio_fact_SOURCE_DIR}/src/FACT_internal.h"
  "uint32_t state;\n\tFACTWave *simpleWave;"
  "uint32_t state;\n\tfloat bwMixGain; /* Original game request's mix group, not asset category. */\n\tFACTWave *simpleWave;")
bw_fact_replace("${bw_audio_fact_SOURCE_DIR}/src/FACT.c"
  "FAudio_zero(*ppCue, sizeof(FACTCue));"
  "FAudio_zero(*ppCue, sizeof(FACTCue));\n\t(*ppCue)->bwMixGain = 1.0f;")
bw_fact_replace("${bw_audio_fact_SOURCE_DIR}/src/FACT.c"
  "pWave->state &= ~FACT_STATE_PREPARED;\n\t\tFAudioSourceVoice_Start"
  "pWave->state &= ~FACT_STATE_PREPARED;\n\t\tFACTWave_SetVolume(pWave, pWave->volume);\n\t\tFAudioSourceVoice_Start")
bw_fact_replace("${bw_audio_fact_SOURCE_DIR}/src/FACT.c"
  "pWave->voice,\n\t\tpWave->volume,\n\t\t0"
  "pWave->voice,\n\t\tpWave->volume * (pWave->parentCue ? pWave->parentCue->bwMixGain : 1.0f),\n\t\t0")
