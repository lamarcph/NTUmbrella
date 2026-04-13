#pragma once

#include <distingnt/api.h>
#include <cstring>
#include "PolyLofiVoice.h"

// ---------------------------------------------------------------------------
// WavetableManager — self-contained wavetable loading and voice distribution.
//
// Owns the async load requests, DRAM buffers, and SD card mount tracking.
// Extracted from PolyLofi.cpp to reduce coupling between the plugin glue
// code and the wavetable I/O subsystem.
// ---------------------------------------------------------------------------

struct WavetableManager {
    static constexpr int NUM_SLOTS = 3;
    static constexpr int WT_BUFFER_FRAMES = 256 * 2048;

    // -----------------------------------------------------------------------
    // init() — call from construct() after allocating DRAM.
    // dramPtr is advanced by dramBytes() worth of int16_t buffers.
    // -----------------------------------------------------------------------
    void init(char*& dramPtr) {
        for (int i = 0; i < NUM_SLOTS; ++i) {
            int16_t* wtBuf = reinterpret_cast<int16_t*>(dramPtr);
            dramPtr += WT_BUFFER_FRAMES * sizeof(int16_t);
            memset(wtBuf, 0, WT_BUFFER_FRAMES * sizeof(int16_t));

            request[i]           = {};
            request[i].table     = wtBuf;
            request[i].tableSize = WT_BUFFER_FRAMES;
            request[i].callback  = onLoadComplete;

            cbData[i].mgr   = this;
            cbData[i].slot  = i;
            request[i].callbackData = &cbData[i];

            awaitingCallback[i] = false;
            needsPush[i]        = false;
            wavetableIndex[i]   = 0;
        }
        cardMounted_ = false;
    }

    // -----------------------------------------------------------------------
    // loadWavetable() — called from parameterChanged when user selects a new
    // wavetable index.  Triggers an async SD card read if possible.
    // -----------------------------------------------------------------------
    void loadWavetable(int slot, int index) {
        wavetableIndex[slot] = index;
        if (cardMounted_ && !awaitingCallback[slot]) {
            request[slot].index = wavetableIndex[slot];
            if (NT_readWavetable(request[slot]))
                awaitingCallback[slot] = true;
        }
    }

    // -----------------------------------------------------------------------
    // update() — called from step() every audio block.
    // 1. Detects SD card mount/unmount and triggers deferred loads.
    // 2. Pushes completed wavetable data to all voices.
    // -----------------------------------------------------------------------
    void update(PolyLofiVoice* voices[], int numVoices) {
        // SD card mount detection
        bool mounted = NT_isSdCardMounted();
        if (cardMounted_ != mounted) {
            cardMounted_ = mounted;
            if (mounted) {
                for (int i = 0; i < NUM_SLOTS; ++i) {
                    if (!awaitingCallback[i]) {
                        request[i].index = wavetableIndex[i];
                        if (NT_readWavetable(request[i]))
                            awaitingCallback[i] = true;
                    }
                }
            }
        }

        // Push completed loads to voices
        for (int i = 0; i < NUM_SLOTS; ++i) {
            if (needsPush[i]) {
                _NT_wavetableRequest& req = request[i];
                if (req.numWaves > 0 && req.waveLength > 0) {
                    const int16_t* base = req.usingMipMaps
                        ? (req.table + req.waveLength * req.numWaves)
                        : req.table;
                    for (int v = 0; v < numVoices; ++v) {
                        voices[v]->setOscWavetable(i, base, req.numWaves, req.waveLength);
                    }
                }
                needsPush[i] = false;
            }
        }
    }

    // -----------------------------------------------------------------------
    // inject() — test helper.  Bypasses SD card loading entirely.
    // -----------------------------------------------------------------------
    void inject(int slot, const int16_t* data, uint32_t numWaves,
                uint32_t waveLength, PolyLofiVoice* voices[], int numVoices) {
        for (int v = 0; v < numVoices; ++v) {
            voices[v]->setOscWavetable(slot, data, numWaves, waveLength);
        }
    }

    // -----------------------------------------------------------------------
    // DRAM budget for calculateRequirements().
    // -----------------------------------------------------------------------
    static constexpr uint32_t dramBytes() {
        return NUM_SLOTS * WT_BUFFER_FRAMES * sizeof(int16_t);
    }

    // --- Public state (read by routing code) ---
    bool cardMounted() const { return cardMounted_; }
    bool isLoading(int slot) const { return awaitingCallback[slot]; }
    int  getIndex(int slot) const { return wavetableIndex[slot]; }

private:
    struct CbData {
        WavetableManager* mgr;
        int slot;
    };

    static void onLoadComplete(void* data) {
        CbData* cbd = static_cast<CbData*>(data);
        cbd->mgr->awaitingCallback[cbd->slot] = false;
        if (!cbd->mgr->request[cbd->slot].error) {
            cbd->mgr->needsPush[cbd->slot] = true;
        }
    }

    _NT_wavetableRequest request[NUM_SLOTS] = {};
    CbData               cbData[NUM_SLOTS]  = {};
    bool                 awaitingCallback[NUM_SLOTS] = {};
    bool                 needsPush[NUM_SLOTS]        = {};
    int                  wavetableIndex[NUM_SLOTS]    = {};
    bool                 cardMounted_ = false;
};
