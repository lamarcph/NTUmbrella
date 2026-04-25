// =============================================================================
// PluginRegistry.hpp  —  Disting NT-friendly algorithm registry
// =============================================================================
// Replaces upstream's MyFactory + std::map<std::string,std::function<...>>
// with a flat constexpr table.
//
// Each entry has:
//   - bank, slot       (1-based; matches Banks_Def.hpp layout)
//   - name             (display string)
//   - factory(arena)   (placement-new the algorithm into an externally
//                       supplied arena; returns the base pointer)
//   - destroy(p)       (in-place ~T())
//   - sizeOf           (sizeof(T) — used to size the shared algo arena)
//   - alignOf          (alignof(T))
//   - gain             (per-program output trim from Banks_Def.hpp)
//
// All algorithms live in a single arena sized to the largest entry; the host
// destructs the previous algo and placement-news the new one when Bank or
// Program changes.
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <new>

#include "../algos/NoisePlethoraPlugin.hpp"
#include "../algos/P_WhiteNoise.hpp"

#ifndef NP_MINIMAL
#include "../algos/P_radioOhNo.hpp"
#include "../algos/P_Rwalk_SineFMFlange.hpp"
#include "../algos/P_xModRingSqr.hpp"
#include "../algos/P_XModRingSine.hpp"
#include "../algos/P_CrossModRing.hpp"
#include "../algos/P_resonoise.hpp"
#include "../algos/P_grainGlitch.hpp"
#include "../algos/P_grainGlitchII.hpp"
#include "../algos/P_grainGlitchIII.hpp"
#include "../algos/P_basurilla.hpp"
#include "../algos/P_clusterSaw.hpp"
#include "../algos/P_pwCluster.hpp"
#include "../algos/P_crCluster2.hpp"
#include "../algos/P_sineFMcluster.hpp"
#include "../algos/P_TriFMcluster.hpp"
#include "../algos/P_PrimeCluster.hpp"
#include "../algos/P_PrimeCnoise.hpp"
#include "../algos/P_FibonacciCluster.hpp"
#include "../algos/P_partialCluster.hpp"
#include "../algos/P_phasingCluster.hpp"
#include "../algos/P_BasuraTotal.hpp"
#include "../algos/P_Atari.hpp"
#include "../algos/P_WalkingFilomena.hpp"
#include "../algos/P_S_H.hpp"
#include "../algos/P_arrayOnTheRocks.hpp"
#include "../algos/P_existencelsPain.hpp"
#include "../algos/P_whoKnows.hpp"
#include "../algos/P_satanWorkout.hpp"
#include "../algos/P_Rwalk_BitCrushPW.hpp"
#include "../algos/P_Rwalk_LFree.hpp"
#include "../algos/P_TestPlugin.hpp"
#include "../algos/P_TeensyAlt.hpp"
#endif // NP_MINIMAL

namespace np_registry {

using Factory = NoisePlethoraPlugin* (*)(void* arena);
using Destroy = void (*)(NoisePlethoraPlugin* p);

template<class T>
NoisePlethoraPlugin* make(void* arena) {
    return new (arena) T();
}

template<class T>
void unmake(NoisePlethoraPlugin* p) {
    static_cast<T*>(p)->~T();
    (void)p;
}

struct Entry {
    uint8_t        bank;     // 1..4
    uint8_t        slot;     // 1..10 (Bank 4 has only 1..3 valid)
    const char*    name;
    Factory        factory;
    Destroy        destroy;
    uint16_t       sizeOf;
    uint16_t       alignOf;
    float          gain;     // per-program output trim
};

// One entry per implemented algorithm. Banks/slots not present here are
// rendered as "--" by parameterString and produce silence.
inline constexpr Entry kEntries[] = {
        // -- Bank 1: heavy effects and ring-mod --
        { 1, 1, "radioOhNo", &make<radioOhNo>, &unmake<radioOhNo>,
            (uint16_t)sizeof(radioOhNo), (uint16_t)alignof(radioOhNo), 1.0f },
        { 1, 2, "Rwalk_SineFMFlange", &make<Rwalk_SineFMFlange>, &unmake<Rwalk_SineFMFlange>,
            (uint16_t)sizeof(Rwalk_SineFMFlange), (uint16_t)alignof(Rwalk_SineFMFlange), 1.0f },
        { 1, 3, "xModRingSqr", &make<xModRingSqr>, &unmake<xModRingSqr>,
            (uint16_t)sizeof(xModRingSqr), (uint16_t)alignof(xModRingSqr), 1.0f },
        { 1, 4, "XModRingSine", &make<XModRingSine>, &unmake<XModRingSine>,
            (uint16_t)sizeof(XModRingSine), (uint16_t)alignof(XModRingSine), 1.0f },
        { 1, 5, "CrossModRing", &make<CrossModRing>, &unmake<CrossModRing>,
            (uint16_t)sizeof(CrossModRing), (uint16_t)alignof(CrossModRing), 1.0f },
        { 1, 6, "resonoise", &make<resonoise>, &unmake<resonoise>,
            (uint16_t)sizeof(resonoise), (uint16_t)alignof(resonoise), 1.0f },
        { 1, 7, "grainGlitch", &make<grainGlitch>, &unmake<grainGlitch>,
            (uint16_t)sizeof(grainGlitch), (uint16_t)alignof(grainGlitch), 1.0f },
        { 1, 8, "grainGlitchII", &make<grainGlitchII>, &unmake<grainGlitchII>,
            (uint16_t)sizeof(grainGlitchII), (uint16_t)alignof(grainGlitchII), 1.0f },
        { 1, 9, "grainGlitchIII", &make<grainGlitchIII>, &unmake<grainGlitchIII>,
            (uint16_t)sizeof(grainGlitchIII), (uint16_t)alignof(grainGlitchIII), 1.0f },
        { 1, 10, "basurilla", &make<basurilla>, &unmake<basurilla>,
            (uint16_t)sizeof(basurilla), (uint16_t)alignof(basurilla), 1.0f },

        // -- Bank 2: cluster synthesis --
        { 2, 1, "clusterSaw", &make<clusterSaw>, &unmake<clusterSaw>,
            (uint16_t)sizeof(clusterSaw), (uint16_t)alignof(clusterSaw), 1.0f },
        { 2, 2, "pwCluster", &make<pwCluster>, &unmake<pwCluster>,
            (uint16_t)sizeof(pwCluster), (uint16_t)alignof(pwCluster), 1.0f },
        { 2, 3, "crCluster2", &make<crCluster2>, &unmake<crCluster2>,
            (uint16_t)sizeof(crCluster2), (uint16_t)alignof(crCluster2), 1.0f },
        { 2, 4, "sineFMcluster", &make<sineFMcluster>, &unmake<sineFMcluster>,
            (uint16_t)sizeof(sineFMcluster), (uint16_t)alignof(sineFMcluster), 1.0f },
        { 2, 5, "TriFMcluster", &make<TriFMcluster>, &unmake<TriFMcluster>,
            (uint16_t)sizeof(TriFMcluster), (uint16_t)alignof(TriFMcluster), 1.0f },
        { 2, 6, "PrimeCluster", &make<PrimeCluster>, &unmake<PrimeCluster>,
            (uint16_t)sizeof(PrimeCluster), (uint16_t)alignof(PrimeCluster), 0.8f },
        { 2, 7, "PrimeCnoise", &make<PrimeCnoise>, &unmake<PrimeCnoise>,
            (uint16_t)sizeof(PrimeCnoise), (uint16_t)alignof(PrimeCnoise), 0.8f },
        { 2, 8, "FibonacciCluster", &make<FibonacciCluster>, &unmake<FibonacciCluster>,
            (uint16_t)sizeof(FibonacciCluster), (uint16_t)alignof(FibonacciCluster), 1.0f },
        { 2, 9, "partialCluster", &make<partialCluster>, &unmake<partialCluster>,
            (uint16_t)sizeof(partialCluster), (uint16_t)alignof(partialCluster), 1.0f },
        { 2, 10, "phasingCluster", &make<phasingCluster>, &unmake<phasingCluster>,
            (uint16_t)sizeof(phasingCluster), (uint16_t)alignof(phasingCluster), 1.0f },

        // -- Bank 3: effects-using algorithms --
        { 3, 1, "BasuraTotal", &make<BasuraTotal>, &unmake<BasuraTotal>,
            (uint16_t)sizeof(BasuraTotal), (uint16_t)alignof(BasuraTotal), 1.0f },
        { 3, 2, "Atari", &make<Atari>, &unmake<Atari>,
            (uint16_t)sizeof(Atari), (uint16_t)alignof(Atari), 1.0f },
        { 3, 3, "WalkingFilomena", &make<WalkingFilomena>, &unmake<WalkingFilomena>,
            (uint16_t)sizeof(WalkingFilomena), (uint16_t)alignof(WalkingFilomena), 1.0f },
        { 3, 4, "S_H", &make<S_H>, &unmake<S_H>,
            (uint16_t)sizeof(S_H), (uint16_t)alignof(S_H), 1.0f },
        { 3, 5, "arrayOnTheRocks", &make<arrayOnTheRocks>, &unmake<arrayOnTheRocks>,
            (uint16_t)sizeof(arrayOnTheRocks), (uint16_t)alignof(arrayOnTheRocks), 1.0f },
        { 3, 6, "existencelsPain", &make<existencelsPain>, &unmake<existencelsPain>,
            (uint16_t)sizeof(existencelsPain), (uint16_t)alignof(existencelsPain), 1.0f },
        { 3, 7, "whoKnows", &make<whoKnows>, &unmake<whoKnows>,
            (uint16_t)sizeof(whoKnows), (uint16_t)alignof(whoKnows), 1.0f },
        { 3, 8, "satanWorkout", &make<satanWorkout>, &unmake<satanWorkout>,
            (uint16_t)sizeof(satanWorkout), (uint16_t)alignof(satanWorkout), 1.0f },
        { 3, 9, "Rwalk_BitCrushPW", &make<Rwalk_BitCrushPW>, &unmake<Rwalk_BitCrushPW>,
            (uint16_t)sizeof(Rwalk_BitCrushPW), (uint16_t)alignof(Rwalk_BitCrushPW), 1.0f },
        { 3, 10, "Rwalk_LFree", &make<Rwalk_LFree>, &unmake<Rwalk_LFree>,
            (uint16_t)sizeof(Rwalk_LFree), (uint16_t)alignof(Rwalk_LFree), 1.0f },

    // -- Bank 4: test/sanity --
    { 4, 1, "TestPlugin", &make<TestPlugin>, &unmake<TestPlugin>,
      (uint16_t)sizeof(TestPlugin), (uint16_t)alignof(TestPlugin), 1.0f },
    { 4, 2, "WhiteNoise", &make<WhiteNoise>, &unmake<WhiteNoise>,
      (uint16_t)sizeof(WhiteNoise), (uint16_t)alignof(WhiteNoise), 1.0f },
    { 4, 3, "TeensyAlt",  &make<TeensyAlt>,  &unmake<TeensyAlt>,
      (uint16_t)sizeof(TeensyAlt),  (uint16_t)alignof(TeensyAlt),  1.0f },
};

inline constexpr size_t kNumEntries = sizeof(kEntries) / sizeof(kEntries[0]);

// Highest slot number per bank — used to clamp Program parameter & for
// parameterString.
inline constexpr uint8_t kSlotsPerBank[5] = { 0, 10, 10, 10, 10 };

// Compile-time max-of for arena sizing.
template<size_t N>
inline constexpr size_t maxSizeOf(const Entry (&e)[N]) {
    size_t m = 1;
    for (size_t i = 0; i < N; ++i) {
        if (e[i].sizeOf > m) m = e[i].sizeOf;
    }
    return m;
}
template<size_t N>
inline constexpr size_t maxAlignOf(const Entry (&e)[N]) {
    size_t m = alignof(std::max_align_t);
    for (size_t i = 0; i < N; ++i) {
        if (e[i].alignOf > m) m = e[i].alignOf;
    }
    return m;
}

inline constexpr size_t kMaxAlgoSize  = maxSizeOf(kEntries);
inline constexpr size_t kMaxAlgoAlign = maxAlignOf(kEntries);

// Look up a (bank, slot) pair. Returns nullptr if no algorithm is registered.
inline const Entry* find(uint8_t bank, uint8_t slot) {
    for (size_t i = 0; i < kNumEntries; ++i) {
        if (kEntries[i].bank == bank && kEntries[i].slot == slot) {
            return &kEntries[i];
        }
    }
    return nullptr;
}

} // namespace np_registry
