# NoiseBouquet — Porting Plan

*This project was developed under the working name `NTNoisePlethora` and
renamed to **NoiseBouquet** when it stabilised, to distinguish it from
Befaco's original product. Historical references below to
`NTNoisePlethora.cpp` / `NTNoisePlethora.o` refer to what are now
`NoiseBouquet.cpp` / `NoiseBouquet.o`. The folder layout in §3 reflects
the original development tree.*

---

A port of Befaco's **Noise Plethora** (originally a Teensy 4.x eurorack module, with a
later VCV Rack port) to the Expert Sleepers **Disting NT** plugin platform.

Source of truth for the port: `../Befaco/src/noise-plethora/` (the VCV Rack adaptation,
which already replaced ARM-only Teensy intrinsics with portable C++).

---

## 1. What Noise Plethora is

A "noise box" composed of **3 banks × 10 algorithms = 30 noise/drone patches**, plus a
4th "test" bank (TestPlugin / WhiteNoise / TeensyAlt). Each algorithm exposes exactly
**two control knobs `(k1, k2)`** that morph timbre dramatically. Internally each algorithm
is built from Teensy Audio Library blocks (oscillators, filters, ring-mod, bitcrusher,
freeverb, granular, flange, wavefolder, mixer …).

### Bank layout (from `Banks_Def.hpp`)

| Bank | Programs |
|------|----------|
| 1 | radioOhNo, Rwalk_SineFMFlange, xModRingSqr, XModRingSine, CrossModRing, resonoise, grainGlitch, grainGlitchII, grainGlitchIII, basurilla |
| 2 | clusterSaw, pwCluster, crCluster2, sineFMcluster, TriFMcluster, PrimeCluster, PrimeCnoise, FibonacciCluster, partialCluster, phasingCluster |
| 3 | BasuraTotal, Atari, WalkingFilomena, S_H, arrayOnTheRocks, existencelsPain, whoKnows, satanWorkout, Rwalk_BitCrushPW, Rwalk_LFree |
| 4 | TestPlugin, WhiteNoise, TeensyAlt (debug / sanity) |

**Decision (user):** the **analog post-filter emulation** that the VCV wrapper
(`Befaco/src/NoisePlethora.cpp`) wraps around the noise core — a 2nd/4th-order
`StateVariableFilter` per channel A/B/C with `FILTER_TYPE_*_PARAM` / `FILTERED_OUTPUT`
and a `bypassFilters` flag — is **not ported**. Rationale: user has external analog
filters. Only the raw noise output of each algorithm is exposed.

The digital filters used **inside** algorithms by the original Teensy hardware module
(`teensy/filter_variable` — used by `resonoise`, `existencelsPain`, `whoKnows`, `TeensyAlt`)
**are kept**: they are part of each algorithm's intended sound, not the analog stage.

All **30 algorithms** are in scope.

---

## 2. Source architecture (Befaco/VCV)

### Plugin contract (`plugins/NoisePlethoraPlugin.hpp`)

```cpp
class NoisePlethoraPlugin {
  virtual void init();
  virtual void process(float k1, float k2);                 // called per audio block
  virtual void processGraphAsBlock(TeensyBuffer& out) = 0;  // fills 128 int16 samples
  float processGraph();                                     // sample-pull wrapper
};
REGISTER_PLUGIN(WhiteNoise);   // factory registration via static Registrar<>
```

Audio is produced as **128-sample int16 blocks** through a manually-wired graph of
`AudioStream` nodes (Teensy library style). The VCV layer pulls samples one at a time
and refills the buffer when empty.

### Teensy shim (`teensy/`)

Already trimmed for portability:

- `audio_core.hpp` — `AudioStream`, `audio_block_t {int16_t data[128]}`, sine LUT, RNG.
- `TeensyAudioReplacements.hpp` — `int16_to_float_1v`, `float_to_int16`, float
  versions of white/grit noise.
- `synth_*` — dc / sine / pwm / waveform / whitenoise / pinknoise.
- `effect_*` — bitcrusher / combine / flange / freeverb / granular / multiply / wavefolder.
- `filter_variable`, `mixer`.

### Friction points for NT

1. **Includes `<rack.hpp>`** in many files (uses `rack::dsp::RingBuffer`, `rack::math`
   helpers, etc.). Must be replaced with a tiny `nt_rack_shim.hpp`.
2. **STL** (`std::map`, `std::function`, `std::shared_ptr`, `std::string`) is used
   only by the factory/Banks. NT plugins build with `-fno-rtti -fno-exceptions`; STL
   technically works but bloats firmware. The factory must be replaced with a flat
   `constexpr` table of function pointers.
3. **`virtual` + `new`** in `MyFactory::Create`. NT plugins do not allow heap allocation
   in `step()`; algorithm switching should allocate from a **placement-new arena** in
   DRAM (largest-algorithm sizing, like a tagged union).
4. **int16 audio at fixed 44100 Hz** vs NT's float audio at ~96 kHz. We will keep the
   internal int16/44.1 kHz pipeline (algorithms rely on `AUDIO_SAMPLE_RATE_EXACT` for
   freq scaling and aliasing limits) and resample to NT's rate at the boundary.
5. **`processGraph()` is sample-pull** with a 128-sample ring buffer — fits NT's
   `step(busFrames, numFramesBy4)` model directly.

---

## 3. Disting NT mapping

### Audio I/O

| Direction | Bus param | Default | Mode |
|-----------|-----------|---------|------|
| Out       | `NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Out", 13, 1)` | bus 13 | Add/Replace |

**Decision (user):** mono only. Single output bus, no stereo handling, no Out R param.

### Control parameters

| # | Name        | Range / Unit | Notes |
|---|-------------|--------------|-------|
| 1 | Bank        | 1..4 enum    | `parameterString` returns "Bank 1..4" |
| 2 | Program     | 1..10 enum   | `parameterString` returns the algo name from `Banks_Def.hpp`; empty slots show "--" |
| 3 | X (k1)      | 0..1000 / ‰  | Maps to `k1` in `process(k1,k2)` |
| 4 | Y (k2)      | 0..1000 / ‰  | Maps to `k2` |
| 5 | X CV        | bus 0..28, `kNT_unitCvInput` | added to X, scaled +/-5V → +/-1.0 |
| 6 | Y CV        | bus 0..28, `kNT_unitCvInput` | added to Y |
| 7 | X CV depth  | -100..100 %  |
| 8 | Y CV depth  | -100..100 %  |
| 9 | Gain        | 0..200 % (default 100) | Post-algo trim, applied with per-program `Bank::gain` |
|10 | Out bus     | (declared via macro)  |
|11 | Out mode    | (Add/Replace)         |

Two parameter pages:
- **Patch**: Bank, Program, X, Y, Gain
- **Routing**: X CV, X CV depth, Y CV, Y CV depth, Out

### Memory

- **SRAM** (algorithm struct): pointers + small state.
- **DRAM** (`requirements.dram`): one **algorithm arena** sized to the largest concrete
  algorithm (likely freeverb/granular based ~20–60 kB). Bank/Program changes call the
  arena's destructor + placement-new of the new algo. Plus a small ring buffer for the
  44.1k → SR resampler.
- **DTC**: live X/Y smoothed values, output ring read pointer.

**Decision (user):** program changes are allowed to take noticeable time (the new algo's
`init()` may run synchronously in the parameter-change handler — no need for an async
worker). A short fade-out → switch → fade-in (~30 ms) is still applied in `step()` to
suppress clicks while construction happens.

### Sample-rate strategy

Noise Plethora algorithms hard-code `AUDIO_SAMPLE_RATE_EXACT = 44100.0f`. Two options:

- **A. Run algorithms at 44100 Hz**, linear-interp upsample to NT rate. Simpler;
  preserves character. Budget ~46% CPU savings vs. running at 96k natively.
- **B. Re-sample-rate the internals** by passing NT sample rate where used. Cheaper
  signal-wise but requires touching every `synth_*` and `effect_*`.

→ **Choose A for M1–M5.** Revisit later only if some algorithm aliases badly.

---

## 4. Repository layout

```
NTNoisePlethora/
├── PLAN.md                    (this file)
├── README.md
├── Makefile                   (copied from Oneiroi, multi-file build)
├── NTNoisePlethora.cpp        (factory + parameters + step + UI glue)
├── include/
│   ├── nt_rack_shim.hpp       (replaces <rack.hpp>: RingBuffer, math utils)
│   ├── PluginRegistry.hpp     (constexpr table replacing MyFactory)
│   ├── BlockResampler.hpp     (44.1k → NT sample rate)
│   └── Banks.hpp              (NT-friendly: no STL strings)
├── teensy/                    (vendored verbatim from Befaco, with shim swap)
│   ├── audio_core.hpp
│   ├── TeensyAudioReplacements.hpp
│   ├── synth_*.{cpp,hpp}
│   ├── effect_*.{cpp,h,hpp}
│   ├── filter_variable.{cpp,hpp}
│   └── mixer.hpp
├── plugins/                   (vendored P_*.hpp, edited only as needed)
│   ├── NoisePlethoraPlugin.hpp   (rewritten: no STL, no virtual factory)
│   ├── P_*.hpp                   (one per algorithm)
│   └── Banks.cpp
├── tests/
│   ├── test_framework.h       (reuses test_harness/)
│   └── test_smoke.cpp         (per-algorithm: builds, doesn't NaN, RMS in range)
└── bin/                       (test outputs, .gitignored)
```

The Disting NT plugin is the single object `NTNoisePlethora.o` (Disting NT can load
multiple `.o`'s as one plugin by virtue of the linker, but Oneiroi style is
single-cpp ⇒ we pre-include all `P_*.hpp` algos via a generated `AllPlugins.inc`).

---

## 5. Milestones

### M1 — Skeleton + Bank 4 (sanity)  ✅ partial
Goal: builds for `cortex-m7`, loads on hardware, makes noise.
- Vendor `teensy/` and `plugins/` directories.
- Write `nt_rack_shim.hpp` (RingBuffer = fixed-size int16 buffer + read/write idx).
- Rewrite `NoisePlethoraPlugin.hpp`: drop STL, drop `<rack.hpp>`, expose direct
  `processGraphAsBlock(int16_t* out)` API.
- Rewrite factory as `constexpr` array of `(name, factory_fn, instance_size)`.
- Implement arena placement-new for Program switching.
- Port Bank 4: **TestPlugin, WhiteNoise, TeensyAlt** (TeensyAlt exercises `filter_variable`).
- `BlockResampler`: linear interp 44.1k → NT sample rate.
- Test harness: `test_smoke` that loads each algo and verifies non-NaN, non-DC output.

**Exit criteria**: `make` clean; `bin/tests` green; on-device manual: WhiteNoise audible.

**Status**: M1 originally landed only `WhiteNoise` (no Bank/Program selectors,
no arena machinery). Those pieces — plus TestPlugin and TeensyAlt — were
folded into M2a (below).

### M2a — Multi-algorithm framework + finish Bank 4  ✅ done
- `include/PluginRegistry.hpp` — `constexpr Entry kEntries[]` table indexed by
  `(bank, slot)`; compile-time `kMaxAlgoSize` / `kMaxAlgoAlign` for the arena.
- `Bank` (1..4) and `Program` (1..10) parameters with `parameterString` callback
  rendering algorithm names ("WhiteNoise", "TeensyAlt", "--" for empty slots).
- One placement-new arena in DTC; `constructAlgo()` destructs the previous
  algorithm in-place and constructs the new one.
- Synchronous `init()` on Program change with a 30 ms fade-out → switch →
  fade-in envelope in `step()` to mask the click (PLAN §9 #4).
- Vendored `synth_waveform.hpp` and `filter_variable.{hpp,cpp}`.
- Vendored `P_TestPlugin.hpp` and `P_TeensyAlt.hpp` verbatim; only `DEBUG()` no-op
  added to `nt_rack_shim.hpp`.
- 15 host tests pass (4 golden-hash WAV captures + 11 lifecycle/switching tests).
- cortex-m7 build size: text=4.5 kB, data=460 B, bss=6 B, total ≈ 5 kB plus the
  algo arena.

### M2b — Bank 2 (pure synthesis cluster algos)  ✅ done
These ended up relying on `synth_waveform`, `synth_whitenoise`, `synth_dc`, and
`mixer` for the currently ported set. `synth_sine` / `filter_variable` were not
needed by Bank 2 after all.
- Vendored `teensy/mixer.hpp` and `teensy/synth_dc.hpp`.
- Vendored and registered: `clusterSaw`, `pwCluster`, `crCluster2`,
  `sineFMcluster`, `TriFMcluster`, `PrimeCluster`, `PrimeCnoise`,
  `FibonacciCluster`, `partialCluster`, `phasingCluster`.
- `PluginRegistry.hpp` now fully populates Bank 2 with the upstream gains
  (`PrimeCluster` / `PrimeCnoise` at `0.8`, all others at `1.0`).
- 10 new golden WAV renders added in append-only order, preserving the seed
  stability rule from M2a.
- Host validation: 26 tests pass (14 golden WAV captures + 12 lifecycle /
  switching / naming tests).
- cortex-m7 build size after M2b: text=21.1 kB, data=1060 B, bss=6 B.

### M3 — Effects layer  ✅ done
- Vendored `effect_bitcrusher.{h,cpp}`, `effect_wavefolder.{hpp,cpp}`,
  `effect_multiply.{h,cpp}`, and `effect_combine.{hpp,cpp}` into `teensy/`.
- Enabled those headers in `TeensyAudioReplacements.hpp` so upcoming Bank 3
  algorithms can compile against the shared umbrella include directly.
- Added focused direct-unit host tests in `tests/test_effect_units.cpp`:
  deterministic 1-second renders for bitcrusher, digital combine, multiply,
  and wavefolder.
- Added 4 new golden WAV hashes without disturbing the plugin-path seed order.
- Host validation after M3: 30 tests pass (18 golden WAV captures + 12 plugin
  lifecycle / switching / naming tests).
- cortex-m7 build size after M3: text=21.4 kB, data=1108 B, bss=6 B.

### M4 — Bank 3 (effects-using algos)  ✅ done
- BasuraTotal, Atari, WalkingFilomena, S_H, arrayOnTheRocks, existencelsPain,
  whoKnows, satanWorkout, Rwalk_BitCrushPW, Rwalk_LFree.
- Vendored `effect_freeverb.{hpp,cpp}`, `synth_pwm.{hpp,cpp}`, and
  `synth_pinknoise.{hpp,cpp}` into `teensy/`, and enabled them in
  `TeensyAudioReplacements.hpp`.
- Vendored and registered all 10 Bank 3 algorithms in `PluginRegistry.hpp`.
- Extended the Rack shim with `dsp::Timer`, a deterministic `nt_random::uniform()`
  helper for the random-walk patches, and shared math compatibility used by the
  embedded toolchain.
- Added 10 Bank 3 golden WAV renders and one Bank 3 audible-sweep coverage test
  to `tests/test_integration.cpp`, preserving the append-only WAV ordering rule.
- Host validation after M4: 41 tests pass with stable golden hashes after a
  regeneration run and immediate rerun.
- cortex-m7 build size after M4: text=35.9 kB, data=1756 B, bss=8 B.

### M5 — Heavy effects + Bank 1  ✅ done
- Vendored `effect_flange.{h,cpp}`, `effect_granular.{hpp,cpp}`, and the
  header-only `synth_sine.hpp` dependency used by the Bank 1 FM patches.
- Vendored and registered all 10 Bank 1 algorithms: radioOhNo,
  Rwalk_SineFMFlange, xModRingSqr, XModRingSine, CrossModRing, resonoise,
  grainGlitch I/II/III, and basurilla.
- Extended `tests/test_integration.cpp` with 10 appended Bank 1 WAV renders,
  Bank 1 naming checks, and one Bank 1 audible-sweep coverage test.
- Preserved the append-only WAV ordering rule by appending Bank 1 renders at
  the end of the WAV block so existing Bank 2/3 hashes stayed stable.
- Host validation after M5: 52 tests pass with stable golden hashes.
- cortex-m7 build size after M5: text=43.4 kB, data=2368 B, bss=8 B.

### M6 — Custom UI
- Optional. Display current Bank/Program name large, X/Y as a 2D pad cursor in the
  remaining screen area. Encoders adjust X/Y with fine/coarse via shift.

### M7 — Polish  ✅ done
- Per-program gain compensation remains driven by `PluginRegistry.hpp` gains.
- Program-change crossfade remains in place at 30 ms fade-out / 30 ms fade-in.
- Added a short one-pole X/Y smoothing stage in `NTNoisePlethora.cpp` to reduce
  parameter zipper noise without changing steady-state output.
- Added a focused host regression in `tests/test_integration.cpp` that verifies
  a large TestPlugin X jump ramps over multiple blocks instead of taking full
  effect in the first block.
- Host validation after M7: 53 tests pass with stable golden hashes.
- cortex-m7 build size after M7: text=43.5 kB, data=2368 B, bss=8 B.

---

## 6. Test strategy

Modeled on `test_harness/` already used by Tracker / PolyLofi:

1. **Build smoke test** (host gcc): instantiate each algorithm, run 10 blocks, assert
   no NaN/Inf, assert RMS in `(1e-4, 1.0)`.
2. **Golden hash** (optional, per-algo): SHA-256 of a 1-second render at fixed seed,
   stored in `tests/golden_hashes.txt`. Regenerated when intentional changes are made.
3. **Param sweep**: render at (X,Y) ∈ {0, 0.5, 1.0}² for each algorithm; assert no
   blowups.
4. **Resampler test**: white noise 44.1k → 96k → spectrum should be flat below
   ~22 kHz with no DC.

Tests run with the existing `test_harness.mk` pattern (host build, no ARM toolchain
needed for CI).

---

## 7. Licensing

Befaco source is **GPL-3.0** (per `Befaco/LICENSE`). NTNoisePlethora is therefore
GPL-3.0. The plan must preserve original copyright headers in every vendored file
and add a `LICENSE` and `NOTICE` at the project root attributing Befaco / Norman Goldwasser
(noise-plethora original) and the VCV adapter author.

---

## 8. Risks

| Risk | Mitigation |
|------|------------|
| Granular/freeverb DRAM > NT budget | Put delay lines in DRAM via `requirements.dram`; if still too large, drop those algos in Bank 1 or shrink buffers. |
| 30 algorithms × instance_size → arena too big | Use placement-new + max-sized arena; 30 algos but only 1 active at a time. |
| 44.1 kHz internal causes audible aliasing on FM/cross-mod patches | Optional per-algo flag `prefersNativeSR`; revisit in M7. |
| `std::function`/`std::map` bloat in factory | Replaced by static constexpr table — zero STL at runtime. |
| `<rack.hpp>` deeply embedded | Centralized shim header; sed-ready include rewrite during vendoring. |
| First-time crackle on Program change (`init()` cost) | Crossfade + run `init()` in the param-change callback, not in `step()`. |

---

## 9. Decisions log

| # | Decision | Rationale |
|---|----------|-----------|
| 1 | **One mega-plugin** with Bank+Program selectors. | User preference. |
| 2 | **Mono only**, single output bus. | User preference (analog post-processing). |
| 3 | **Drop the VCV wrapper's analog post-filter emulation** (the `StateVariableFilter2ndOrder`/`4thOrder` chain in `Befaco/src/NoisePlethora.cpp` with `FILTER_TYPE_*_PARAM` / `FILTERED_OUTPUT` / `bypassFilters`). All 30 noise algorithms — including those using the digital `filter_variable` internally (`resonoise`, `existencelsPain`, `whoKnows`, `TeensyAlt`) — are kept. | User has external analog filters; the noise core itself is what's being ported. The internal SVFs are part of each algorithm's intended sound, not the analog stage. |
| 4 | **Synchronous `init()`** on Program change is acceptable; fade-out/fade-in (~30 ms) covers it audibly. | User OK'd slow bank loads. |
| 5 | **Algo arena lives in DTC**, not DRAM, sized at compile time to `max(sizeof(T))` over all registered algorithms. | Bank 4 algos are all under 2 kB; we'll revisit if M5's freeverb/granular push past the DTC budget. |
| 6 | **Tests pin noise seeds via fixed test ordering**: all WAV-rendering tests run first in `main()`, in append-only order. The `AudioSynthNoiseWhite::instance_count` static is private, so a `setSeed()` workaround was avoided. | Adding new non-WAV tests does not invalidate any golden hash. |

Still open:
- **Custom UI** in M6: required or nice-to-have?
- Is the **GPL-3.0** licensing acceptable for the umbrella repo's distribution?

---

## 10. Next action

The remaining optional milestone is M6:
1. Decide whether the custom UI is worth doing for this plugin or whether the
  current stock NT parameter UI is sufficient.
2. If M6 is in scope, design the Bank/Program display plus 2D X/Y pad UI.
3. If M6 is skipped, the port is effectively feature-complete and the next work
  is listening-driven polish or release packaging.
  any per-program gain adjustments discovered during listening tests.
