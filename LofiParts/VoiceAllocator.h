// =============================================================================
// VoiceAllocator.h — Note-to-voice allocation with quietest-steal policy
// =============================================================================
// Stateless allocator: finds the best voice for a note-on event.
//
// Policy:
//   1. Retrigger — reuse an active voice already playing the same note
//   2. Free      — use the first inactive voice
//   3. Steal     — take the quietest active voice (minimises audible artefacts)
//
// Usage:
//   auto r = VoiceAllocator::allocate(voices, NUM_VOICES, note);
//   if (r.stolen)
//       voices[r.index]->stealVoice(note, vel);
//   else
//       voices[r.index]->noteOn(note, vel);
// =============================================================================
#pragma once

struct VoiceAllocator {

    struct Result {
        int  index;   // voice index (always valid: 0 .. numVoices-1)
        bool stolen;  // true ⇒ caller should use stealVoice() instead of noteOn()
    };

    // Voice type must expose:  int note, bool active, float getCurrentAmplitudeLevel()
    template<typename VoiceT>
    static Result allocate(VoiceT* voices[], int numVoices, int note) {
        // 1. Retrigger: same note already sounding → steal for crossfade
        for (int i = 0; i < numVoices; ++i) {
            if (voices[i]->note == note && voices[i]->active)
                return { i, true };
        }
        // 2. Free voice
        for (int i = 0; i < numVoices; ++i) {
            if (!voices[i]->active)
                return { i, false };
        }
        // 3. Steal quietest
        int   best  = 0;
        float quiet = 999999.0f;
        for (int i = 0; i < numVoices; ++i) {
            float lvl = voices[i]->getCurrentAmplitudeLevel();
            if (lvl < quiet) { quiet = lvl; best = i; }
        }
        return { best, true };
    }
};
