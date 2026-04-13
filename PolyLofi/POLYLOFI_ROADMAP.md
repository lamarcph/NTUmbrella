# PolyLofi — Inter-Oscillator TZFM & Hard Sync Plan

## Overview

Add envelope-modulated through-zero FM (TZFM) and oscillator hard sync between the 3 oscillators in PolyLofi, using a fixed directed routing topology.

## Routing Topology

One-way only, no feedback loops:

```
osc[2] ──FM/Sync──▶ osc[1]
osc[2] ──FM/Sync──▶ osc[0]
osc[1] ──FM/Sync──▶ osc[0]
```

- Osc3 can modulate Osc2 and Osc1
- Osc2 can modulate Osc1
- FM and sync share the same three lanes but with **separate controls** per lane

## Prerequisite: Fix `modEnv` (currently dead) — ✅ DONE

`modEnv` is declared, advanced, and read as a mod source, but three things are broken:

1. **`modEnv.gate(true/false)`** — never called in `noteOn()` / `noteOff()`, stays IDLE forever
2. **`modEnv.setParameters(...)`** — never called, uses uninitialized step values
3. **`modEnv.finalizeBlock()`** — never called at end of `processBlock`

### Fix

- Add `setModEnv(a, d, s, r)` and `setModShape(shape)` to `PolyLofiVoice`
- Call `modEnv.gate(true)` in `noteOn()`, `modEnv.gate(false)` in `noteOff()`
- Call `modEnv.finalizeBlock()` at end of `processBlock`
- Expose 5 new params: Mod Env Attack, Decay, Sustain, Release, Shape

## New Parameters (11 total) — ✅ DONE

| Parameter | Type | Range | Default | Notes |
|---|---|---|---|---|
| Mod Env Attack | ms | 0–3000 | 10 | kNT_scaling1000 |
| Mod Env Decay | ms | 0–3000 | 100 | kNT_scaling1000 |
| Mod Env Sustain | float | 0–1000 | 800 | kNT_scaling1000 |
| Mod Env Release | ms | 0–3000 | 200 | kNT_scaling1000 |
| Mod Env Shape | float | -990–990 | 0 | kNT_scaling1000 |
| FM 3→2 Depth | float | 0–10000 | 0 | Hz deviation base depth |
| FM 3→1 Depth | float | 0–10000 | 0 | Hz deviation base depth |
| FM 2→1 Depth | float | 0–10000 | 0 | Hz deviation base depth |
| Sync 3→2 | bool | 0–1 | 0 | Hard sync enable |
| Sync 3→1 | bool | 0–1 | 0 | Hard sync enable |
| Sync 2→1 | bool | 0–1 | 0 | Hard sync enable |

## New Mod Matrix Destinations (3) — ✅ DONE

Added to `ModDest` enum and `enumStringsModDest`:

- `kDestFM3to2` — "FM 3>2"
- `kDestFM3to1` — "FM 3>1"
- `kDestFM2to1` — "FM 2>1"

This lets any existing mod source (LFOs, all 3 envelopes, velocity) modulate FM depth through the existing matrix. No dedicated envelope-to-FM knobs needed.

## Oscillator API Extension (LofiMorphOscillator.h) — ✅ DONE

Current block methods do `_phase += increment; _phase &= (PHASE_SCALE - 1);` — wrap is silently masked.

### New unified method

```cpp
void getWaveBlockWithSync(
    int16_t* outputBuffer,      // audio output
    bool* syncOutput,           // per-sample: did this osc wrap? (can be nullptr)
    const bool* syncInput,      // per-sample: reset phase? (can be nullptr)
    WaveformType type,
    uint32_t numSamples
);
```

**Sync output** (master role): detect wrap when `phase_before + increment > PHASE_SCALE`  
**Sync input** (slave role): reset `_phase = 0` before phase advance on samples where `syncInput[i] == true`

Existing methods remain untouched for backward compatibility.

## Voice Rendering Order Change — ✅ DONE

Current: renders osc 0→1→2 in a loop, all with `prepareFmBlock(nullptr)`.

### New order in `processBlock`:

1. **Render osc[2]** first (pure, no FM input) → output to `osc2Buffer[MAX_BLOCK_SIZE]` (int16_t, Q1.15), also writes `sync2[MAX_BLOCK_SIZE]` (bool)
2. **Render osc[1]** — feed `osc2Buffer` as FM via `prepareFmBlock(osc2Buffer, n)` with `setFmDepth(baseFM3to2 + modOffsets[kDestFM3to2] * 10000.0)`, apply `sync2` if Sync 3→2 enabled → output to `osc1Buffer`, also writes `sync1`
3. **Render osc[0]** — needs two FM sources. Pre-mix into combined Q1.15 buffer:
   ```
   combinedFm[i] = clamp(osc2Buffer[i] * depth3to1_norm + osc1Buffer[i] * depth2to1_norm)
   ```
   Feed via `prepareFmBlock(combinedFm, n)` with unit-scale depth. Apply sync from osc2 and/or osc1 (OR of enabled sync flags).

### Memory impact

- 2 extra `int16_t[128]` buffers + 2 `bool[128]` buffers on stack per voice — trivial

## UI Pages — ✅ DONE

Add a new **"FM/Sync"** parameter page:

```
page6 = { FM3to2, FM3to1, FM2to1, Sync3to2, Sync3to1, Sync2to1,
           ModEnvAttack, ModEnvDecay, ModEnvSustain, ModEnvRelease, ModEnvShape }
```

## Implementation Order — ✅ ALL DONE

1. ✅ Fix `modEnv` wiring (gate, setParameters, finalizeBlock) + expose ADSR params
2. ✅ Add 3 FM depth params + 3 sync enables + 3 new mod dest enum entries
3. ✅ Add sync API to `LofiMorphOscillator`
4. ✅ Refactor `processBlock` rendering order with FM buses and sync
5. ✅ Wire all params in `PolyLofi.cpp` (DTC, construct, parameterChanged, pages)
6. ✅ Build and validate

## Risks

| Risk | Severity | Mitigation |
|---|---|---|
| CPU cost of per-sample sync detection | Low | Single comparison per sample |
| Pre-mixing two FM sources for osc[0] loses independent TZFM sign tracking | Medium | Acceptable for lo-fi character |
| `static int16_t fixBuffer` shared across oscs | Medium | Switch to separate named buffers |
| Parameter count growth (+11 params, +3 mod dests) | Low | NT platform supports it; dedicated page keeps it tidy |
---

# Future Improvements Brainstorm

## 1. PolyBLEP Anti-Aliased Waveforms — ✅ DONE

### Current State
The oscillator (`OscillatorFixedPoint`) generates naive aliased waveforms — the saw is a raw phase ramp, the square is a threshold comparison at `PHASE_SCALE / 2`. This is intentional for a lo-fi aesthetic, but having PolyBLEP as an option would hugely expand tonal range.

### What Is PolyBLEP?
Polynomial Band-Limited Step — a lightweight anti-aliasing correction applied at discontinuities (saw reset, square transitions). It only touches 1–2 samples around each transition, making it very cheap compared to BLIT or minBLEP.

### Implementation Difficulty: **Medium**

- **New waveform type**: Add `SAW_BLEP` and `SQUARE_BLEP` to `WaveformType` enum (or a global toggle per oscillator).
- **Core requirement**: At each phase wrap (saw) or half-cycle crossing (square), compute a 2nd-order polynomial residual and subtract it from the current and previous sample.
- **Key formula** (saw example):
  ```
  t = (phase_after_wrap) / phaseIncrement   // fractional distance past discontinuity
  polyblep(t) = t*t + 2*t + 1 when t is in [0,1)
  polyblep(1-t) at the sample before the discontinuity
  ```
- **Fixed-point concern**: The PolyBLEP correction is typically computed in float. Since we already convert to float in `processBlock` for the voice mix, we could either:
  - (a) Apply BLEP in float after the Q15→float conversion (simplest, ~2 float ops per transition), or
  - (b) Compute it in Q15 using integer polynomials (more complex but keeps the full signal path integer).
- **Integration points**:
  - New block methods: `getSawBlepWaveBlock()`, `getSquareBlepWaveBlock()` in `LofiMorphOscillator.h`
  - Extend `enumStringsWaveform[]` to include "Saw BLEP", "Square BLEP" (or a per-osc "Anti-Alias" toggle)
  - Update `getWaveBlockWithSync()` switch statement
- **Risk**: Adds a branch per sample. On Cortex-M7 with branch prediction this should be negligible since transitions only happen once per cycle.

### Waveform Types
**Separate waveform selections**: `SAW_BLEP` and `SQUARE_BLEP` are **distinct waveform types**, not morphable variants. They are separate entries in the `WaveformType` enum alongside `SINE`, `TRIANGLE`, `SQUARE`, `SAW`, and `MORPHED`.

**Always 100% correction**: When either PolyBLEP waveform type is selected, the correction is always applied at full strength. No parameters control the correction amount.

**No parameter consumption**: PolyBLEP waveforms do not use the morph parameter. The morph knob remains **unused** when `SAW_BLEP` or `SQUARE_BLEP` is active, making them compatible with any parameter-free oscillator role. If the user wants to use morph for something else, they must select a different waveform type.

---

## 2. Pulse Width Modulation (PWM) — ✅ DONE

### Current State
The square wave is hardcoded at 50% duty cycle: `return (_phase < (PHASE_SCALE / 2)) ? Q15_MAX_VAL : Q15_MIN_VAL;`. There is no pulse width parameter.

### Implementation Difficulty: **Easy**

- **Add `_pulseWidth` member** to `OscillatorFixedPoint` (uint32_t, range 0 to `PHASE_SCALE`, default `PHASE_SCALE/2`).
- **Add `setPulseWidth(float pw)`** method: `_pulseWidth = static_cast<uint32_t>(clamp(pw, 0.01, 0.99) * PHASE_SCALE);`
- **Modify `getSquareWave()`**: `return (_phase < _pulseWidth) ? Q15_MAX_VAL : Q15_MIN_VAL;`
- **Block version**: Same change in `getSquareWaveBlock()`, and in the `SQUARE` case of `getWaveBlockWithSync()`.
- **Per-sample modulation**: Add `_currentBlockPulseWidths[MAX_BLOCK_SIZE]` array and a `preparePwBlock()` method, similar to `prepareMorphBlock()`. This would enable LFO→PW modulation for classic PWM.

### UI / Parameter Design
**Morph parameter for PWM**: When `SQUARE` waveform is selected, the existing `Osc1 Morph` / `Osc2 Morph` / `Osc3 Morph` parameters control the **pulse width** (0 = narrow ~10%, 0.5 = 50% duty, 1.0 = wide ~90%). This is the primary use of the morph parameter for SQUARE mode.

**New mod dest**: `kDestPulseWidth` — enables PWM modulation from LFO/envelope via the existing mod matrix. Routes through the morph offset calculation.

**Coexistence rules**: 
- SQUARE with PWM **cannot** coexist with `MORPHED` waveform on the same oscillator (both need the morph parameter).
- SQUARE with PWM **cannot** coexist with `WAVETABLE` on the same oscillator (both need the morph parameter).
- SQUARE with PWM **can** coexist with `SAW_BLEP` or `SQUARE_BLEP` (they use no parameters), but only one SQUARE variant per oscillator.

### PolyBLEP + PWM Synergy
If both PolyBLEP and PWM features are implemented, they can work together: a `SQUARE_BLEP` waveform with PolyBLEP correction (no morph param needed) is parameter-free, while a `SQUARE` waveform with PWM control uses the morph param. Users pick one or the other on each oscillator.

---

## 3. LFO Speed Scaling Improvements

### Current State
LFO speed is `pThis->v[p] / 1000.0f * 10.0f` → linear 0–10 Hz. This gives poor resolution at slow speeds (0–1 Hz range is only 10% of the knob) and can't go below ~0.01 Hz or above 10 Hz. No sync options exist.

### 3a. Exponential / Logarithmic Speed Curve — ✅ DONE

**Difficulty: Easy**

Replace the linear mapping with an exponential curve for perceptually even distribution:
```
float normalized = pThis->v[p] / 1000.0f;  // 0.0 to 1.0
float lfoHz = 0.01f * powf(2000.0f, normalized);  // 0.01 Hz to 20 Hz
```
This gives:
- Knob at 0%: 0.01 Hz (100-second cycle)
- Knob at 25%: ~0.07 Hz
- Knob at 50%: ~0.45 Hz
- Knob at 75%: ~3 Hz
- Knob at 100%: 20 Hz

Could use `fast_powf()` from `CheapMaths.h` since this only runs on parameter change, not per-sample.

### 3b. MIDI Clock Sync — ✅ DONE

**Difficulty: Medium**

The NT API provides `midiRealtime(self, byte)` — currently set to `NULL` in the factory struct. MIDI clock sends `0xF8` at 24 PPQN (pulses per quarter note).

**Implementation plan:**
1. Add `midiRealtime` callback to the factory struct (currently `NULL`).
2. In the callback, detect `0xF8` (clock), `0xFA` (start), `0xFC` (stop).
3. Track inter-pulse timing: measure samples between consecutive `0xF8` messages to derive BPM.
4. Add a per-LFO "Sync Mode" enum param: `"Free", "1/1", "1/2", "1/4", "1/8", "1/16", "1/4T", "1/8T", "1/4.", "1/8."` etc.
5. When sync mode ≠ Free, override `lfoSpeed[n]` with `bpm * divisor / 60.0f`.
6. **New DTC fields**: `float currentBPM`, `uint32_t lastClockSampleCount`, `uint32_t clockPulseCounter`.
7. **New params**: 3× LFO Sync Mode enum (one per LFO).

**MIDI clock bytes reference:**
| Byte | Meaning |
|------|---------|
| 0xF8 | Timing Clock (24 PPQN) |
| 0xFA | Start |
| 0xFB | Continue |
| 0xFC | Stop |

### 3c. Hardware Clock Input (CV) — ✅ DONE

**Difficulty: Medium** (reference implementation exists in Oneiroi)

The Oneiroi plugin shows the pattern:
1. Add `NT_PARAMETER_CV_INPUT("Clock Input", 0, 0)` parameter.
2. In `step()`, read the CV bus: `float* clockIn = busFrames + (pThis->v[kParamClockInput] - 1) * numFrames;`
3. Detect rising edge with threshold (0.5V): `bool clockPulse = (prevClockValue < 0.5f) && (currentClockValue >= 0.5f);`
4. Measure inter-pulse timing → derive BPM → compute LFO rate.

**Implementation plan:**
1. Add `kParamClockInput` as a `NT_PARAMETER_CV_INPUT`.
2. Add to DTC: `float prevClockValue`, `uint32_t samplesSinceLastClock`, `float derivedClockHz`.
3. In `step()`, scan the clock input buffer, detect edges, measure period.
4. When a clock input is assigned and edges are detected, override LFO speed for LFOs in sync mode.
5. Could share the same "Sync Mode" enum from 3b — if hardware clock is present, it takes priority over MIDI clock.

**Bonus**: The clock input could also reset LFO phase on each pulse for tight rhythmic sync.

---

## 4. Stereo Output — ✅ DONE

### Current State
The `step()` function supports dual-mono output when a Right Output bus is configured. Per-voice constant-power panning is now implemented via a `Pan Spread` parameter.

### Implementation Details

**Voice-level pan:**
- Each voice stores `panL` and `panR` coefficients (constant-power: `panL = cos(π/2·p)`, `panR = sin(π/2·p)`)
- `setPan(float p)` method on `PolyLofiVoice` computes coefficients from pan position p ∈ [0,1]

**Pan Spread parameter (0–1000):**
- Distributes 8 voices evenly across the stereo field
- Formula: `p_v = 0.5 + spread × (v/(N-1) - 0.5)` where v is voice index
- spread=0 → all voices at center (panL=panR=0.707, constant-power)
- spread=1 → voice 0 full left, voice 7 full right, others distributed evenly

**Stereo summing in step():**
- When Right Output bus is configured: `outL[i] += voiceBuf[i] * panL`, `outR[i] += voiceBuf[i] * panR`
- When mono (no Right Output): `outL[i] += voiceBuf[i]` (no pan multiplication, zero cost)
- Delay stays mono per-voice — pan applied after delay, so echoes sit at the same stereo position as the dry voice

**Test:** `test_stereo_chord_progression_wav` — I-V-vi-IV × 2 cycles in C major, PolyBLEP saw with ±7¢ detuning, 375ms delay, 75% pan spread. Writes stereo WAV, verifies L≠R.

### New Param
- `Pan Spread` (0–1000, default 0) on Output page

---

## 5. Oscillator Detune / Unison

### Current State
Each oscillator has semitone and fine-tune offsets, but there's no per-voice unison spread or super-saw style detuning built into the architecture.

### Implementation Difficulty: **Medium-Hard**

Two approaches:

**A. Simple voice-level detune:**
- Add a "Detune" param that applies a small random or systematic pitch offset per voice (like ±5 cents spread across the 8 voices).
- Very cheap: just modify the frequency calculation in `noteOn()`.

**B. Sub-oscillator unison (within each voice):**
- Each oscillator renders N (2–4) slightly detuned copies and sums them.
- CPU cost: multiplies oscillator cost by N. With 8 voices × 3 oscs × N=4 unison = 96 oscillators. May be too heavy for Cortex-M7.
- A middle ground: offer unison only on one oscillator, controlled by a parameter.

---

## 6. Additional Mod Matrix Destinations — ✅ DONE (all 10 destinations implemented)

### All Targets — ✅ Implemented

All 10 planned modulation destinations are now implemented and wired:

| Destination | Use Case | Status |
|---|---|---|
| `kDestPitch` (global) | Vibrato, pitch envelope | ✅ Done |
| `kDestOsc1Pitch` / `kDestOsc2Pitch` / `kDestOsc3Pitch` | Independent pitch mod per oscillator | ✅ Done |
| `kDestPulseWidth` | Classic PWM | ✅ Done |
| `kDestDelayTime` | Chorus/flanger effects | ✅ Done |
| `kDestDelayFeedback` | Dynamic echo control | ✅ Done |
| `kDestDelayMix` | Swell/ducking effects | ✅ Done |
| `kDestDrive` | Dynamic saturation | ✅ Done |
| `kDestFilterEnvAmount` | Envelope depth modulation | ✅ Done |
| `kDestOsc1Level` / `kDestOsc2Level` / `kDestOsc3Level` | Oscillator crossfade/tremolo | ✅ Done |
| `kDestLfoSpeed` | LFO speed modulation (meta-modulation) | ✅ Done |

---

## 7. Performance Optimizations

### 7a. Filter Bypass When Cutoff Is Fully Open — ✅ DONE
**Current state**: The filter always runs `processBlock()` even when cutoff = 20 kHz and resonance = 0.
**Fix**: Skip filter processing when `modulatedCutoff >= 19500.0f && resonance < 0.01f`. The `ZDFAggressiveFilter` already has `BYPASS` mode — but it's only selected by the user enum, not automatically.

### 7b. Voice Culling / Early Exit
**Current state**: All 8 voices are checked every block, and `processBlock` does work even in the IDLE/release tail.
**Possible improvement**: Track a `lastActiveVoice` high-water mark to avoid iterating over voices that have never been triggered. Minor saving.

### 7c. Delay Bypass When Mix = 0 — ✅ DONE
**Current state**: `DecimatedDelay::process()` runs every sample even when `delayMix = 0.0f`.
**Fix**: Skip the delay entirely when mix is 0: `if (delayMix < 0.001f) { outL[i] += voiceBuffer[i] * currentAmp * stealFade; }` etc.

### 7d. LFO Block Processing — ✅ DONE
**Current state**: LFO runs at block rate (one `getNextValue()` call per block), which is already efficient. However, `setShapeMorph(0.0f)` is called on every `setLfoFrequency()` call, which is redundant after init.
**Fix**: Remove the `setShapeMorph(0.0f)` from `setLfoFrequency()` — morph is set separately via `setLfoMorph()`.

### 7e. Envelope Shape Cost — ✅ DONE
**Current state**: `applyShape()` uses a division per call: `(x * (1.0f + shape)) / (1.0f + shape * x)`. This is called for both `getCurrentLevelShaped()` and `getTargetLevelShaped()` every block.
**Possible optimization**: Pre-compute `1.0f / (1.0f + shape)` and `1.0f + shape` when `setShape()` is called, reducing the per-block cost to a multiply+multiply+multiply.

### 7f. `static float tempL/tempR` in `step()` — ✅ DONE
**Current state**: `static float tempL[128]` and `tempR[128]` are file-scope statics. This works because `step()` is single-threaded, but it's 1 KB of BSS that persists. Consider stack allocation (the Cortex-M7 stack should handle 256 floats easily at this call depth).

### 7g. `powf` Calls in Envelope Modulation — ✅ DONE
**Current state**: `processBlock()` calls `powf(2.0f, modOffsets[...] * 4.0f)` for 6 envelope time modulations per block. `powf` is expensive on Cortex-M7.
**Fix**: Replace with `fast_powf()` from `CheapMaths.h` — the LUT-based approximation is already available and accurate enough for envelope time scaling.

---

## 8. Additional Waveforms

Beyond PolyBLEP, other waveform ideas that fit the lo-fi character:

| Waveform | Description | Difficulty |
|---|---|---|
| **Noise** | White noise (xorshift16 already exists) or filtered noise | Easy — new WaveformType, use `xorshift16()` |
| **Wavetable (Q15 lo-fi)** | Fixed-point wavetable playback — see §12 below | Medium-Hard — memory + interpolation |
| **Sub Oscillator** | -1 or -2 octave square under the main osc | Easy — halve/quarter the phase increment |
| **Ring Mod output** | Multiply two osc outputs together | Easy — already have separate buffers in FM path |

---

## 9. MIDI Enhancements

### 9a. Pitch Bend — ✅ DONE
**Current state**: Not handled in `midiMessage()`.
**Implementation**: Detect `0xE0` (pitch bend), combine data1/data2 into 14-bit value, convert to semitone offset, apply to all active voices' frequencies. Standard range ±2 semitones (configurable).

### 9b. Mod Wheel (CC1) — ✅ DONE
**Current state**: Not handled.
**Implementation**: Detect CC1 in `midiMessage()`, store as a float 0–1, expose as a new ModSource (`kSourceModWheel`). The mod matrix already supports arbitrary sources.

### 9c. Aftertouch — ✅ DONE
**Current state**: Not handled.
**Implementation**: Channel aftertouch (`0xD0`) → new ModSource `kSourceAftertouch`. Per-note (poly) aftertouch (`0xA0`) would require per-voice tracking.  My MPC One support polyafter touch, so we should support it.

### 9d. CC-to-Parameter Mapping — ❌ REMOVED FROM SCOPE
The disting NT platform provides native CC-to-parameter mapping out of the box, so a plugin-level MIDI learn system is unnecessary.

---

## 10. Glide / Portamento — ✅ DONE

### Current State
`noteOn()` sets frequency instantly. No legato or glide behavior.

### Implementation Difficulty: **Easy-Medium**

- Add a `glideTime` parameter (ms).
- In `noteOn()`, if a voice is already active (legato), store the target frequency and interpolate toward it over the glide time.
- Requires per-voice `currentFreq[NUM_OSC]` and `targetFreq[NUM_OSC]` fields.
- In `processBlock()`, interpolate freq per block (or use `prepareVOctBlock` with a glide ramp).
- **New params**: `Glide Time` (0–3000 ms), `Glide Mode` enum: "Off", "Always", "Legato Only".

---

## 11. Effects Chain Improvements

### 11a. Chorus / Flanger
**Current state**: Only `DecimatedDelay` with fixed time. Short delay times (1–20ms) with LFO modulation = chorus/flanger.
**Implementation**: Already possible by modulating delay time via mod matrix (if `kDestDelayTime` is added), but a dedicated short-delay mode with built-in modulation would be cleaner.

### 11b. Bit Crusher / Sample Rate Reducer — ✅ DONE
Fits the lo-fi theme perfectly. Reduce bit depth (right-shift Q15 samples) and/or skip samples (decimate).
**Difficulty**: Easy — a few lines of code in the voice output path.

### 11c. Per-Voice Delay — A Design Feature
**Current state**: Each voice has its own `DecimatedDelay` with 32 KB buffer (256 KB total for 8 voices). **This is intentional and should be preserved.**

**Design inspiration**: The Dave Smith Instruments Poly Evolver had per-voice analog delay, making each note in a chord ripple and echo independently. This is extremely rare in polysynths and gives PolyLofi a distinctive character that most soft synths and even most hardware can't match.

**Per-voice delay advantages:**
- Each note gets its own echo tail — chords shimmer instead of washing into mush.
- Delay feedback is per-voice, so staccato notes can ring out independently.
- Voice stealing naturally fades out the delay tail of the stolen voice.
- Combined with different FM/sync settings per-voice (via velocity, etc.), this creates organic, evolving textures.

**Future enhancements for the per-voice delay:**
- **Ping-pong mode**: Alternate L/R output per voice delay tap (requires stereo output first).
- **Delay time modulation**: Add `kDestDelayTime` to mod matrix for per-voice chorus/flanger effects.
- **Delay filter feedback**: Add a simple LP/HP in the delay feedback path for tape-style darkening or thinning. — see **§11d** below.
- **Tempo sync**: Snap delay time to note divisions when MIDI clock or hardware clock is available. — ✅ **DONE** (`kParamDelaySyncMode` enum: Free/4bar/2bar/1bar/1-2/1-4/1-8/1-16/1-4T/1-8T/1-4./1-8. — reuses `kSyncMultipliers`, auto-updates on BPM change, manual Delay Time knob ignored when synced.)

**RAM note (updated)**: Buffer halved from `DELAY_SIZE = 131072` to `65536` (~1.48s at 44.1 kHz). See **§11e** below. This saves 2 MB of DRAM (8 × 65536 × 4 = 2 MB, down from 4 MB). The 1000ms delay time parameter still fits comfortably. Sync modes that exceed buffer length are clamped automatically — at typical tempos (80–160 BPM), modes up to "1 bar" fit; "2 bar" and "4 bar" clamp at slow tempos but work fine at faster ones. No sync enum changes needed.

### 11d. Delay Feedback Filter — ✅ DONE

**Goal**: Add a one-pole LP/HP filter inside the delay feedback path for tape-style darkening (LP) or thinning (HP). Each repeat passes through the filter, so echoes progressively lose highs or lows — exactly like analog tape delay or a bucket-brigade device.

**Implementation Difficulty: Easy**

**Signal flow change in `DecimatedDelay::process()`:**
```
Current:  delayed → diffuser → * feedback → + input → buffer
New:      delayed → diffuser → one-pole filter → * feedback → + input → buffer
```

The filter sits between the diffuser output and the feedback multiply, so it processes only the recirculating signal — the dry delayed tap used for the wet mix is unfiltered.

**One-pole filter (6 dB/oct):**
```cpp
// In DecimatedDelay:
float _fbFilterState = 0.0f;
float _fbFilterCoeff = 0.0f;  // 0 = bypass, >0 = LP, <0 = HP

// LP mode:  y[n] = y[n-1] + coeff * (x[n] - y[n-1])
// HP mode:  y[n] = x[n] - LP(x[n])   (complement of LP)
```

**Coefficient from frequency:**
```cpp
void setFeedbackFilter(float freqHz, bool isHighpass, float sampleRate) {
    float fc = freqHz / sampleRate;
    float c = 1.0f - expf(-2.0f * 3.14159265f * fc);  // one-pole coeff
    _fbFilterCoeff = isHighpass ? -c : c;
}
```

At `_fbFilterCoeff == 0` (or very small magnitude), the filter is effectively bypassed — no CPU branch needed, just a multiply-accumulate that does nothing.

**New parameters (2):**

| Parameter | Type | Range | Default | Notes |
|---|---|---|---|---|
| Delay FB Filter | enum | Off / LP / HP | Off | `kNT_unitEnum`, `enumStringsDelayFBFilter` |
| Delay FB Freq | Hz | 100–15000 | 3000 | `kNT_scaling1000`, only active when filter ≠ Off |

**Per-voice**: Each voice's `DecimatedDelay` has its own filter state, so stereo-spread voices with different delay times each accumulate their own independent filtering — maintaining the per-voice delay character from §11c.

**UI**: Add to the existing Delay parameter page.

**Integration points:**
1. Add `_fbFilterState` and `_fbFilterCoeff` members to `DecimatedDelay`.
2. Add `setFeedbackFilter(freqHz, isHighpass, sampleRate)` method.
3. In `process()`, apply one-pole filter to `diffused` before the feedback multiply.
4. Add `kParamDelayFBFilter` (enum) and `kParamDelayFBFreq` (int) to param enum.
5. Wire in `parameterChanged()` — call `setFeedbackFilter()` on all 8 voices.
6. Add `enumStringsDelayFBFilter[] = {"Off", "LP", "HP"}`.

**Sonic character:**
- **LP @ 2 kHz**: Classic tape delay — each repeat gets duller and warmer. Works beautifully with the allpass diffusion for a washed-out ambient tail.
- **LP @ 500 Hz**: Extreme dub delay — repeats become deep rumbles.
- **HP @ 200 Hz**: Removes bass buildup in feedback — keeps echoes crisp and thin. Useful for preventing muddy low-end resonance at high feedback.
- **HP @ 1 kHz**: Aggressive thinning — echoes become tinny and lo-fi.

### 11e. Halve Delay Buffer + Per-Sample Delay Time Smoothing — ✅ DONE

**Goal**: (1) Reduce `DELAY_SIZE` from 131072 to 65536, saving 2 MB DRAM. (2) Add per-sample linear interpolation of the delay read position to eliminate harsh artifacts when modulating longer delay times.

**Implementation Difficulty: Easy**

#### Part 1 — Buffer halving

```cpp
// DecimatedDelay.h
#define DELAY_SIZE 65536   // ~1.48s at 44.1kHz, ~0.68s at 96kHz
#define DELAY_MASK (DELAY_SIZE - 1)
```

**Impact:**
- DRAM: 8 × 131072 × 4 = 4 MB → 8 × 65536 × 4 = **2 MB** (saves 2 MB)
- Max delay at 44.1 kHz: ~1485 ms (the 0–1000 ms param range still fits)
- Max delay at 96 kHz: ~682 ms (add runtime clamp: `delayTimeMs = min(delayTimeMs, maxMs)` )
- Sync modes: clamping already handles overflow. "4 bar" and "2 bar" will clamp at slow tempos, work at faster tempos. **No sync enum change needed.**
- `calculateRequirements()` DRAM shrinks automatically via `DELAY_SIZE`

#### Part 2 — Fixed Absolute Delay Modulation + Per-Sample Smoothing

**Problem**: Modulating delay time (via LFO → `kDestDelayTime`) works great at short times (<30ms, chorus/flanger), but at 500ms the read pointer jumps thousands of samples between 128-sample blocks → harsh digital noise.

**Two root causes:**
1. **Proportional modulation is wrong**: Current formula `delaySamples + modOffset * delaySamples` means at 500ms, a ±1.0 offset = ±500ms swing (±22,050 samples). That's extreme pitch-shifting, not a useful musical effect. At 5ms it's ±5ms = nice chorus. The problem is the range scales with the delay time.
2. **Block-rate discontinuity**: `modulatedDelaySamples` is computed once per 128-sample block. Even a moderate jump creates a click at the block boundary.

**What synthesis effects actually need:**
- **Chorus/flanger**: ±1–10ms sweep, always small
- **Tape wobble**: ±2–5ms drift, always small
- **Karplus-Strong vibrato**: fraction of a ms
- **Dub delay drift**: ±1–2ms subtle movement

No musically useful delay effect requires hundreds of ms of swing. Large delay jumps produce harsh digital noise, not musical pitch-shifting.

**Fix Part A — Fixed absolute modulation range (±10ms / ±441 samples at 44.1kHz):**

Change the modulation formula from proportional to absolute:

```cpp
// Old (proportional — broken at long delays):
float modulatedDelaySamples = delaySamples + modOffsets[kDestDelayTime] * delaySamples;

// New (fixed absolute — always musical):
static constexpr float kDelayModMaxMs = 10.0f;  // ±10ms max swing
float delayModSamples = modOffsets[kDestDelayTime] * (kDelayModMaxMs * 0.001f * sampleRate);
float modulatedDelaySamples = delaySamples + delayModSamples;
modulatedDelaySamples = std::clamp(modulatedDelaySamples, 1.0f, (float)(DELAY_SIZE - 1));
```

This gives:
- At 2ms delay: LFO ±1.0 → ±10ms swing = aggressive flanger ✓
- At 500ms delay: LFO ±1.0 → ±10ms swing = gentle tape wobble ✓
- At any delay: modulation range is bounded and musically useful
- LFO amount knob (mod matrix) still scales the depth: amount 0.1 → ±1ms, amount 0.5 → ±5ms

**Fix Part B — Per-sample smoothing:**

Even with ±10ms max, a 441-sample jump at a block boundary would click. Add `_smoothedDelay` to ramp per-sample:

```cpp
// In DecimatedDelay:
float _smoothedDelay = 0.0f;

// Per-sample in the delay loop:
float smoothCoeff = 1.0f / (float)numSamples;  // converge over one block
_smoothedDelay += smoothCoeff * (modulatedDelaySamples - _smoothedDelay);
float wet = process(input, _smoothedDelay, fb, mix);
```

**Cost**: One multiply + one add per sample. Negligible.

**No new parameters** — both changes are always on.

**Integration points:**
1. Change modulation formula in both delay paths (active + tail-only) of `processBlock`.
2. Add `float _smoothedDelay = 0.0f;` to `DecimatedDelay` (lives with the delay, not the voice).
3. Initialize `_smoothedDelay = delaySamples` in `noteOn()` (via a setter or direct assignment).
4. Per-sample ramp inside the delay loop instead of holding constant.

---

### 11f. Pitch-Tracked Comb Delay (Sarajevo Mode) — ✅ DONE

**Goal**: When enabled, set each voice's delay time to track the played note's pitch. With feedback, this creates a resonant comb filter tuned to the note — producing strong octave-like harmonics, exactly like the **Xaoc Sarajevo** BBD delay clocked at audio rate.

**Implementation Difficulty: Easy** (just changes how `delaySamples` is computed — no new DSP)

**How it works:**
- A comb filter with delay time $T$ has resonant peaks at $f = 1/T, 2/T, 3/T, \ldots$
- When $T = 1/f_{\text{note}}$ (one period of the played note), the comb reinforces the fundamental and all harmonics → strong, musical resonance
- With the feedback filter (§11d) set to LP, higher harmonics decay faster → warm, string-like timbre (Karplus-Strong effect)
- With the allpass diffuser, the resonance gains spatial depth

**Example** — Middle C (261.6 Hz) at 44.1 kHz:

| Mode | Delay time | Comb fundamental | Character |
|------|-----------|------------------|-----------|
| Unison | 168.6 samples (3.82 ms) | 261.6 Hz | Reinforces played pitch |
| Oct -1 | 337.2 samples (7.64 ms) | 130.8 Hz | Sub-octave resonance |
| Oct +1 | 84.3 samples (1.91 ms) | 523.2 Hz | Octave-up shimmer |
| Fifth | 112.4 samples (2.55 ms) | 392.4 Hz | Perfect fifth above |

**New parameter (1):**

| Parameter | Type | Range | Default | Notes |
|---|---|---|---|---|
| Delay Pitch Track | enum | Off / Unison / Oct -1 / Oct +1 / Fifth | Off | `kNT_unitEnum` |

**Pitch-track multipliers:**
```cpp
static const float kPitchTrackMultipliers[] = {
    0.0f,   // 0: Off
    1.0f,   // 1: Unison  (delay = 1 period)
    0.5f,   // 2: Oct -1  (delay = 2 periods)
    2.0f,   // 3: Oct +1  (delay = 0.5 period)
    1.5f,   // 4: Fifth   (delay = 2/3 period)
};
```

**Implementation:**

1. Add `kParamDelayPitchTrack` to param enum + `enumStringsDelayPitchTrack[] = {"Off", "Unison", "Oct -1", "Oct +1", "Fifth"}`.
2. Add `int delayPitchTrack = 0;` to DTC and `int delayPitchTrackMode = 0;` to `PolyLofiVoice`.
3. In `noteOn()`, when pitch track > 0:
   ```cpp
   if (delayPitchTrackMode > 0) {
       float period = sampleRate / noteFrequency;
       delaySamples = period / kPitchTrackMultipliers[delayPitchTrackMode];
   }
   ```
4. During glide (if glide active + pitch tracking), update `delaySamples` per block from the current interpolated frequency.
5. When pitch track == 0 (Off), delay time comes from the normal `kParamDelayTime` / sync as before.
6. `kDestDelayTime` mod matrix still works as a fixed ±10ms offset on top (§11e) — enables vibrato-like delay time wobble around the pitch-tracked point. This is especially effective for Sarajevo-style wobble at audio-rate comb times.

**Interaction with other delay features:**
- **Delay Sync Mode**: Ignored when pitch track is active (pitch track takes priority). **Grayed out on UI.**
- **Delay Time knob**: Ignored when pitch track is active (delay is derived from note pitch). **Grayed out on UI.**
- **Delay Feedback**: Works normally — higher feedback = stronger comb resonance = more harmonics.
- **Delay FB Filter (§11d)**: Shapes the harmonic spectrum of the comb — LP gives warm Karplus-Strong strings, HP gives metallic plucks.
- **Delay Diffusion**: Adds spatial smear to the comb resonance — turns a tight comb into a shimmer.
- **Per-sample smoothing (§11e)**: Ensures glide + pitch tracking produces smooth pitch-following instead of glitchy jumps.

**Parameter graying (NT API):** The disting NT API provides `NT_setParameterGrayedOut(algorithmIndex, parameter, gray)`. When `Delay Pitch Track ≠ Off`, call this to gray out `kParamDelayTime` and `kParamDelaySyncMode`. When switched back to Off, un-gray them. Call from `parameterChanged()` on `kParamDelayPitchTrack`:

```cpp
case kParamDelayPitchTrack: {
    dtc->delayPitchTrack = pThis->v[p];
    bool tracked = (dtc->delayPitchTrack > 0);
    uint32_t algIdx = NT_algorithmIndex(pThis);
    uint32_t pOff = NT_parameterOffset();
    NT_setParameterGrayedOut(algIdx, kParamDelayTime + pOff, tracked);
    NT_setParameterGrayedOut(algIdx, kParamDelaySyncMode + pOff, tracked);
    for (int i = 0; i < NUM_VOICES; ++i)
        dtc->voices[i]->delayPitchTrackMode = dtc->delayPitchTrack;
    break;
}
```

**Sonic character:**
- **Unison + high feedback + LP filter**: Karplus-Strong plucked string / Sarajevo drone
- **Oct -1 + medium feedback**: Adds a sub-octave rumble underneath each note
- **Oct +1 + low feedback**: Shimmering octave-up doubling (12-string guitar effect)
- **Fifth + medium feedback**: Power-chord-like harmonics (note + fifth)
- **Any mode + diffusion**: Transitions from tight comb into reverb-like resonance

**Cost**: Zero additional DSP — just changes which value `delaySamples` gets. The comb filtering is an emergent property of the existing delay + feedback.

---

## 12. Lo-Fi Wavetable Oscillator (Microwave XT Inspiration) — ✅ DONE (SD card loading)

### Design Philosophy
The goal is **not** a modern high-fidelity wavetable engine (Serum, Vital). Instead, aim for the gritty, characterful sound of classic wavetable synths like the **Waldorf Microwave XT** and **PPG Wave** — 8-bit/12-bit tables, limited interpolation, audible stepping between waves. The Q15 fixed-point signal path already enforces 16-bit resolution, which is perfect for this aesthetic.

### Architecture

**Wavetable format:**
- Each wavetable = array of N single-cycle waveforms ("waves"), each with S samples.
- Classic size: N=64 waves × S=256 samples = 16 K × `int16_t` = **32 KB per table** (same as one delay buffer).
- Smaller option: N=64 × S=128 = 16 KB per table.
- Microwave XT used 128 waves × 128 samples, stored as 8-bit — we can do 16-bit Q15 for slightly better quality while keeping the character.

**Playback engine:**
- Reuse the existing `_phase` accumulator (Q4.28) from `OscillatorFixedPoint` for intra-wave scanning (sample position within a wave).
- **Morph parameter reuse**: When `WAVETABLE` waveform is selected, the `Osc1 Morph` / `Osc2 Morph` / `Osc3 Morph` parameters control the **wave position** (0.0 = first wave, 0.5 = middle of table, 1.0 = last wave).
- **Interpolation modes** (user-selectable for lo-fi control):
  - `None` — nearest-neighbor wave selection, hard stepping (PPG style, maximum lo-fi).
  - `Linear` — crossfade between adjacent waves (Microwave XT default).
  - `Linear + Sample` — also interpolate between samples within a wave (smoother but less gritty).
- Wave position can be modulated via the existing morph modulation system (`prepareMorphBlock`, `modOffsets[kDestOsc1Morph]`, etc.).

**Integration into OscillatorFixedPoint:**
- New `WaveformType`: `WAVETABLE`.
- New members: `const int16_t* _wavetableData`, `uint16_t _numWaves`, `uint16_t _waveSamples`, `uint8_t _wtInterpMode`.
- New method: `setWavetable(const int16_t* data, uint16_t numWaves, uint16_t samplesPerWave)`.
- `getWavetableWave()` reads from the table using `_phase` for within-wave position and `_shapeMorph` (reused) for cross-wave position.

**Memory budget:**
- With per-voice delay at 256 KB, we need to be careful. Options:
  - Store wavetables in DRAM alongside delay buffers (shared pool).
  - Store a **single shared wavetable** (not per-voice) — all voices read from the same table. 32 KB for one table is very manageable.
  - Support 2–4 loadable tables (64–128 KB) selectable per oscillator.

**Wavetable sources:**
- **Built-in ROM tables**: Hardcoded classic waveforms (additive harmonics, PWM sweep, formant vowels, organ, etc.) — no file I/O needed, simplest to implement first.
- **User tables from SD card** (future): The NT API likely supports file access. Load `.wav` files where each cycle is one wave. Standard format used by many synths.

### Lo-Fi Character Tricks
- **Bit-reduce the table** on load: right-shift samples by 4–8 bits then left-shift back → quantization noise like 8-bit/12-bit PPG.
- **Reduce table size**: Use 64- or 128-sample waves instead of 2048 — audible spectral folding at high pitches, exactly like the originals.
- **No bandlimiting**: Let the wavetable alias freely. The Microwave XT's character comes partly from aliasing artifacts at high notes.

### Morph Parameter Sharing (Design Consolidation)
**Design goal**: Simplify the UI by reusing the existing per-oscillator **morph parameter** (range 0–1000, already exposed on every oscillator) across multiple advanced features instead of adding dedicated parameters for each.

**Shared uses of the morph parameter:**
1. **MORPHED waveform** (existing): Blends Sine → Triangle → Square → Saw.
2. **SQUARE waveform (PWM)** (§2): Pulse width control (0 = narrow, 0.5 = 50%, 1.0 = wide).
3. **WAVETABLE waveform** (§12): Wave position scanning (0 = first wave, 1 = last wave).

**Note on PolyBLEP (§1)**: `SAW_BLEP` and `SQUARE_BLEP` are separate waveform types with no parameter consumption. They are parameter-free — select the waveform and get 100% PolyBLEP correction. The morph parameter is unused when these waveforms are active.

**Trade-off: Mutual exclusivity per oscillator (only for morph-consuming features)**
Because the morph parameter is shared between three features, an oscillator can only use **one** of these at a time:
- Cannot use `MORPHED` waveform + `SQUARE` (PWM) on the same oscillator.
- Cannot use `MORPHED` waveform + `WAVETABLE` on the same oscillator.
- Cannot use `SQUARE` (PWM) + `WAVETABLE` on the same oscillator.

**Features that do NOT conflict**:
- `SAW_BLEP` or `SQUARE_BLEP` can coexist with any other waveform type (they use no parameters).

**Strategic examples for full feature utilization**:

*Example 1* (PolyBLEP + morphing):
- Osc1: `SAW_BLEP` (parameter-free, clean anti-aliasing)
- Osc2: `MORPHED` (sine→saw morph via morph param)
- Osc3: `MORPHED` (different harmonic coloration)

*Example 2* (All waveform types used):
- Osc1: `SQUARE` with PWM (morph controls pulse width, can modulate via LFO)
- Osc2: `SAW_BLEP` (clean high-frequency tone, no morph param needed)
- Osc3: SINE (stable reference, FM source)

*Example 3* (Wavetable scanning):
- Osc1: `WAVETABLE` (morph = wave position, swept by mod env)
- Osc2: `SAW_BLEP` (anti-aliased bass foundation)
- Osc3: `SQUARE` with PWM (pulse modulation for movement)

**Benefits of this approach:**
- PolyBLEP is "free" — zero parameter overhead.
- Morph parameter only conflicts with other morph-using waveform types.
- No UI clutter — no new parameters to display.
- Users consciously choose between `MORPHED`, PWM, and wavetable features on each oscillator.
- Encourages creative layering across the 3-oscillator architecture.
- Keeps total parameter count manageable for the disting NT's limited UI.

**Alternative (not chosen)**: Add dedicated parameters (`Pulse Width`, `Wave Position`) for each feature. This would require 2 more parameters per oscillator (6 total for 3 oscs) and would bloat the UI significantly. Rejected because PolyLofi is lo-fi by design, not a feature-complete desktop synth.

### Implementation Phases
1. **Phase 1**: Hardcoded ROM tables — ⏭️ **SKIPPED** (went directly to SD card loading per user decision).
2. **Phase 2**: SD card wavetable loading — ✅ **DONE**.
   - `WAVETABLE` waveform type (enum value 7) added to `LofiMorphOscillator`.
   - 3 independent wavetable slots (one per oscillator), shared across all 8 voices.
   - Async SD card loading via `NT_readWavetable()` with callback.
   - DRAM allocation: 3 × 512 K frames (`WT_BUFFER_FRAMES = 256*2048`) ≈ 3 MB.
   - Mipmap support: when `usingMipMaps`, reads from full-size mipmap offset.
   - Lo-fi character: nearest-neighbor sample lookup (no interpolation within wave), linear crossfade between adjacent waves only.
   - Morph parameter reuse: `_shapeMorph` controls wave position scanning.
   - SD card hot-mount detection: auto-loads wavetables when card is inserted.
   - `polyLofi_injectWavetable()` extern "C" test helper for headless testing.
   - Test: `test_wavetable_morph_wav()` generates 16-wave sine→saw table, golden hash verified.
3. **Phase 3**: Multiple selectable ROM tables. Per-oscillator table selection param.

### Remaining Wavetable Work
- **~~`kDestWavePosition` mod destination~~**: Removed from scope — the existing morph parameter + `kDestOsc1Morph` / `kDestOsc2Morph` / `kDestOsc3Morph` mod destinations already enable LFO/envelope → wave position scanning. A dedicated destination would be redundant.
- **Wavetable name display**: Use `NT_getWavetableInfo()` to show table name on screen.
- **Waveform visualization**: Use mipmap data for `draw()` waveform display.

---

## 13. Piano Sustain Pedal (CC64) — ✅ DONE

### Current State
The `midiMessage()` handler only processes Note On (`0x90`) and Note Off (`0x80`). MIDI CC messages (`0xB0`) are completely ignored. There is no sustain pedal behavior — releasing a key always triggers the release phase immediately.

### How Piano Sustain Works
MIDI CC64 (Sustain Pedal / Damper) with value ≥ 64 = pedal down, < 64 = pedal up.

**Behavior:**
1. **Pedal down + key release**: The voice does NOT enter Release. It holds at the Sustain level of the amp envelope as if the key is still held.
2. **Pedal up**: All voices that were "sustained" (key released while pedal was down) immediately enter Release.
3. **Pedal down + new note**: Normal note-on. If the voice is later released while pedal is still down, it sustains.
4. **Key held + pedal toggled**: No effect — the voice is already gating normally.

### Implementation Difficulty: **Easy-Medium**

**DTC additions:**
```
bool sustainPedalDown = false;
```

**Per-voice additions (PolyLofiVoice):**
```
bool sustained = false;   // true if key was released while pedal was down
```

**Logic changes:**

1. **In `midiMessage()`** — add CC handling:
   ```cpp
   if ((status & 0xF0) == 0xB0) {  // Control Change
       if (data1 == 64) {  // Sustain pedal
           bool pedalDown = (data2 >= 64);
           dtc->sustainPedalDown = pedalDown;
           if (!pedalDown) {
               // Pedal released: release all sustained voices
               for (int i = 0; i < NUM_VOICES; ++i) {
                   if (dtc->voices[i]->sustained) {
                       dtc->voices[i]->sustained = false;
                       dtc->voices[i]->noteOff();
                   }
               }
           }
       }
   }
   ```

2. **In Note Off handling** — check pedal state:
   ```cpp
   // Instead of immediately calling noteOff():
   if (dtc->sustainPedalDown) {
       dtc->voices[i]->sustained = true;  // Mark as sustained, don't release
   } else {
       dtc->voices[i]->noteOff();
   }
   ```

3. **Voice stealing** — sustained voices should be stealable with lower priority than releasing voices but higher priority than actively held voices.

### Edge Cases
- **Pedal down before any notes**: Just set the flag. No effect until notes are played and released.
- **Re-pressing a sustained note**: Should retrigger (current `noteOn` already handles same-note retrigger).
- **All notes off (CC123)**: Should release everything regardless of pedal state. Worth adding CC123 handling at the same time.
- **Sostenuto (CC66)**: More complex — only sustains notes that are already held when the pedal goes down. Could be a future addition but not essential for v1.

### New Params
None required — sustain pedal is a standard MIDI behavior, not a user-configurable setting. Optionally, a "Sustain Pedal" enable/disable toggle if some users want to ignore it.

---

## Priority Ranking (Suggested)

| Priority | Feature | Impact | Effort | Status |
|---|---|---|---|---|
| 🔴 High | Sustain Pedal (CC64) | High (essential playability) | Easy-Medium | ✅ Done |
| 🔴 High | Pulse Width Modulation | High (classic synth essential) | Easy | ✅ Done |
| 🔴 High | LFO Exponential Scaling | High (usability) | Easy | ✅ Done |
| 🔴 High | Pitch Bend MIDI | High (playability) | Easy | ✅ Done |
| 🟠 Medium | PolyBLEP Waveforms | High (sound quality option) | Medium | ✅ Done |
| 🟠 Medium | Hardware Clock Sync | High (eurorack integration) | Medium | ✅ Done |
| 🟠 Medium | MIDI Clock Sync | Medium (DAW integration) | Medium | ✅ Done |
| 🟠 Medium | Glide / Portamento | Medium (expressiveness) | Easy-Medium | ✅ Done |
| 🟠 Medium | Mod Wheel as Mod Source | Medium (expressiveness) | Easy | ✅ Done |
| 🟠 Medium | Replace `powf` with `fast_powf` | Medium (CPU saving) | Easy | ✅ Done |
| 🟠 Medium | Lo-Fi Wavetable (SD card loading) | High (signature sound) | Medium | ✅ Done |
| 🟡 Low | Stereo Output | Medium (spatial) | Easy-Medium | ✅ Done |
| 🟡 Low | Filter/Delay Bypass Optimizations | Low-Medium (CPU saving) | Easy | ✅ Done |
| 🟡 Low | Additional Waveforms (Noise, Sub) | Low-Medium (variety) | Easy | ⚠️ Noise done, Sub not done |
| 🟡 Low | Additional Mod Destinations | Low (flexibility) | Easy each | ✅ Done (all 10) |
| 🟡 Low | Voice Detune / Unison | Medium (fatness) | Medium-Hard | ❌ |
| 🟡 Low | Per-Voice Delay Enhancements | Medium (Evolver character) | Easy-Medium | ✅ Done (FB filter, diffusion, pitch-track, smoothing) |
| 🟠 Medium | Delay Feedback Filter (§11d) | High (tape/dub character) | Easy | ✅ Done |
| 🟠 Medium | Halve Delay Buffer (§11e) | Medium (saves 2 MB DRAM) | Easy | ✅ Done |
| 🟠 Medium | Delay Time Smoothing (§11e) | High (fixes harsh mod artifacts) | Easy | ✅ Done |
| 🟠 Medium | Pitch-Tracked Comb Delay (§11f) | High (Sarajevo harmonics) | Easy | ✅ Done |
| 🟡 Low | Bit Crusher | Low (lo-fi flavor) | Easy | ✅ Done |
| ⚪ Future | Wavetable name display / visualization | Low (UX polish) | Easy | ✅ Name done, visualization ❌ |
| 🟠 Medium | Live Audio → Wavetable Capture (§14) | High (unique feature) | Medium | ❌ |

---

# Code Compartmentalization Refactoring

## Current State (April 2026)

The codebase has grown organically to ~7,400 lines across 4 main files. All 85 tests pass deterministically with golden SHA-256 hashes, making this a safe time to refactor — any behavioral change will be caught immediately.

| File | Lines | Problem |
|------|------:|---------|
| `tests/test_integration.cpp` | ~4,400 | All 85 tests in one file; ~93% is test bodies with heavy boilerplate duplication |
| `PolyLofi.cpp` | ~1,370 | `parameterChanged()` alone is ~485 lines of copy-paste switch/case |
| `LofiMorphOscillator.h` | ~804 | Oscillator + LFO in one header (minor) |
| `PolyLofiVoice.h` | ~843 | 245-line `processBlock` monolith; everything inline in header; ~50 public fields |

---

## R1. Split Test File into Focused Files ✅

**Goal**: Break `test_integration.cpp` (~4,400 lines, 85 tests) into ~6 focused files + shared helpers.

**Current problems:**
- Impossible to navigate — need to scroll through ~4,400 lines to find a test.
- ~45 WAV tests repeat identical 4-line plugin+WAV setup boilerplate.
- ~35 tests repeat the "solo osc1, open filter, no delay" setup (5–7 identical lines).
- Shared helpers (`blocksFor`, `renderToWav`, `renderToWavWithClock`) are buried mid-file.

**New file structure:**

| File | Contents | ~Lines |
|------|----------|-------:|
| `tests/test_helpers.h` | `createPlugin()`, `blocksFor()`, `renderToWav()`, `renderToWavWithClock()`, param/waveform/mod enums, `OUTPUT_BUS`/`BLOCK_SIZE` constants, `initSoloOsc1()` helper | ~120 |
| `tests/test_core.cpp` | 8 lifecycle tests: loads, silence, note-on, note-off, polyphony, voice stealing, MIDI channel filter, parameter sweep | ~200 |
| `tests/test_captures.cpp` | 2 basic WAV captures: single note + chord | ~110 |
| `tests/test_oscillators.cpp` | Waveform types, morph sweep, PolyBLEP (saw, square, sync sweep), wavetable morph | ~550 |
| `tests/test_modulation.cpp` | LFO→cutoff, LFO→morph, velocity→cutoff, modEnv→FM, filter env, multi-LFO, LFO morph persist, LFO exp speed, aftertouch, mod wheel, pitch bend | ~600 |
| `tests/test_midi.cpp` | Sustain pedal, MIDI clock sync, MIDI channel filter (already in core — or move here) | ~250 |
| `tests/test_effects.cpp` | Filter modes, delay, delay bypass, chorus, flanger, ducking, vel→feedback, per-voice delay (pentatonic, echo cascade, comb, slapback), bit crusher, drive | ~700 |
| `tests/test_voice.cpp` | 3-osc detune, amp envelope shapes, FM+sync, hard sync, sync sweep, pulse width, glide | ~450 |
| `tests/test_golden.cpp` | SHA-256 golden hash regression test | ~70 |
| `tests/test_main.cpp` | `main()` — includes all test headers, registers + runs all tests | ~80 |

**Shared helper: `initSoloOsc1()`** — eliminates ~7 lines per test:
```cpp
static void initSoloOsc1(PluginInstance& plugin, int cutoff = 10000) {
    plugin.setParameter(kP_Osc1Level, 1000);
    plugin.setParameter(kP_Osc2Level, 0);
    plugin.setParameter(kP_Osc3Level, 0);
    plugin.setParameter(kP_BaseCutoff, cutoff);
    plugin.setParameter(kP_FilterEnvAmount, 0);
    plugin.setParameter(kP_DelayMix, 0);
}
```

**Build change:** The Makefile `test-run` target currently compiles a single `.cpp`. Update to compile all `tests/test_*.cpp` files and link them together (each test file includes `test_helpers.h` which has the shared infrastructure).

**Verification:** All 85 tests still pass with identical golden hashes. Zero behavioral change.

---

## R2. Extract `processBlock` Sub-Methods

**Goal**: Break `PolyLofiVoice::processBlock()` (245 lines) into ~5 focused private methods.

**Current problems:**
- 7 distinct DSP stages crammed into one function — hard to read, hard to profile, hard to test.
- The delay-tail-only early return path (top of function) has completely different semantics from normal rendering but shares the same method.
- A 13-line `renderOsc` lambda inside the method body would be better as a proper private method.

**Extraction plan:**

| New private method | Lines extracted | What it does |
|--------------------|:--------------:|--------------|
| `processDelayTailOnly()` | ~20 | Early-return path: drains delay buffer when voice envelope has died, kills voice when energy < threshold |
| `applyModMatrix()` | ~35 | Reads `modSlots[]`, evaluates mod sources (LFO, env, velocity, mod wheel, aftertouch), populates `modOffsets[]`, applies exp2-scaled envelope time modulation |
| `renderOscillators()` | ~85 | Contains both fast-path (no FM/sync) and FM/sync dependency-order rendering. Internally calls the existing `renderOsc` lambda (promoted to a private method) |
| `applyFilterAndEffects()` | ~30 | ZDF filter (with auto-bypass), bit crusher, sample reducer |
| `applyAmpEnvelopeAndDelay()` | ~35 | Amp envelope × steal fade ramp, per-voice delay processing |

**After refactoring, `processBlock()` becomes a ~20-line orchestrator:**
```cpp
void processBlock(float* outL, float* outR, uint32_t n) {
    if (state == IDLE) return;
    if (processDelayTailOnly(outL, outR, n)) return;

    applyModMatrix(n);
    renderOscillators(n);
    applyFilterAndEffects(n);
    applyAmpEnvelopeAndDelay(outL, outR, n);

    ampEnv.finalizeBlock();
    filterEnv.finalizeBlock();
    modEnv.finalizeBlock();
}
```

**Also fix:** Frequency calculation is duplicated between `noteOn()` and `stealVoice()`. Extract a `calcFrequencyHz(note, semitone, fine)` private helper.

**Verification:** Golden hashes unchanged — pure method extraction, no behavioral change.

---

## R3. Data-Driven `parameterChanged()`

**Goal**: Replace the ~485-line switch/case in `parameterChanged()` with array-indexed helpers.

**Current problems:**
- The 3 oscillator blocks (waveform, semitone, fine, morph, level, PW, wavetable × 3) are near-identical copy-paste — ~100 lines that should be ~15.
- The 3 LFO blocks (speed, shape, unipolar, morph × 3) are another ~60 lines of copy-paste.
- The 3 envelope blocks (ADSR + shape × 3) are ~75 lines of copy-paste.
- The 3 FM depth and 3 sync enable cases are 6 near-identical cases.
- Adding a 4th oscillator or LFO would require copy-pasting the same block again.

**Approach — helper functions + offset tables:**

```cpp
// Oscillator params are laid out as: [Waveform, Semitone, Fine, Morph, Level] × 3
static const int kOscParamBase[] = { kParamOsc1Waveform, kParamOsc2Waveform, kParamOsc3Waveform };
static const int kOscParamCount = 5; // params per oscillator

void applyOscParam(DTC* dtc, int oscIdx, int paramOffset, int value);
void applyLfoParam(DTC* dtc, int lfoIdx, int paramOffset, int value);
void applyEnvParam(DTC* dtc, int envIdx, int paramOffset, int value);
```

**In `parameterChanged()`:**
```cpp
for (int osc = 0; osc < 3; ++osc) {
    int base = kOscParamBase[osc];
    if (p >= base && p < base + kOscParamCount) {
        applyOscParam(dtc, osc, p - base, v);
        return;
    }
}
// Similar for LFOs, envelopes, FM, sync, mod slots
```

**Expected line reduction:** ~485 lines → ~150 lines (70% reduction).

**Also applies to `construct()`**: The voice initialization loop that sets all parameters on each voice repeats the same per-parameter logic. A shared `applyAllDefaults(dtc)` that calls the same helpers would keep it in sync automatically.

**Verification:** Golden hashes unchanged — same parameter dispatching, just deduplicated.

---

## R4. Move Voice Bodies to `.cpp`

**Goal**: Move `PolyLofiVoice` method implementations from the header to a new `PolyLofiVoice.cpp`.

**Current problems:**
- All 666 lines are inline in a `.h` file. Every translation unit that includes it recompiles all DSP code.
- The header mixes API declaration with implementation details (stack buffer sizes, filter constants, delay threshold magic numbers).
- ~50 public member variables with no encapsulation — the plugin mutates fields directly.

**Plan:**

**`PolyLofiVoice.h` (after — ~120 lines):**
- Class declaration with all public methods (signatures only)
- Private method declarations (the new R2 extractions)
- Member variable declarations
- Inline trivial getters/setters only (1-liners like `getCurrentAmplitudeLevel()`)
- `ModSlot` struct + `ModDest`/`ModSource` enums stay in header (shared with tests)

**`PolyLofiVoice.cpp` (new — ~550 lines):**
- All method bodies: `processBlock()`, `noteOn()`, `noteOff()`, `stealVoice()`, all the `set*()` methods
- The R2 extracted sub-methods
- Static helper functions (frequency calculation, etc.)

**Build change:** Add `PolyLofiVoice.cpp` to the Makefile compilation list.

**Encapsulation improvement (optional, lower priority):** Group the ~50 public fields into a `VoiceParams` struct that gets passed into `processBlock`. This would make the data-flow boundary explicit. Can be done as a follow-up.

**Verification:** Golden hashes unchanged — moving code between files is purely organizational.

---

## R5. Extract `WavetableManager`

**Goal**: Consolidate all wavetable-related code from `PolyLofi.cpp` into a self-contained `WavetableManager` struct.

**Current problems:**
- Wavetable code is scattered across 4 functions: `construct()` (buffer setup + callback), `parameterChanged()` (trigger load), `step()` (mount detection + push to voices), `polyLofi_injectWavetable()` (test helper).
- DTC holds 5 wavetable-related arrays (`wtRequest[3]`, `wtCbData[3]`, `wtAwaitingCallback[3]`, `wtNeedsPush[3]`, `oscWavetableIndex[3]`) plus `cardMounted`.
- The async callback lambda in `construct()` captures a `WtCallbackData*` — this coupling would be cleaner as a method.

**New structure:**

```cpp
// WavetableManager.h
struct WavetableManager {
    static constexpr int NUM_SLOTS = 3;
    static constexpr int WT_BUFFER_FRAMES = 256 * 2048;

    // State
    _NT_wavetableRequest request[NUM_SLOTS] = {};
    bool awaitingCallback[NUM_SLOTS] = {};
    bool needsPush[NUM_SLOTS] = {};
    int  wavetableIndex[NUM_SLOTS] = {};
    bool cardMounted = false;

    // Setup: allocate DRAM buffers, configure callbacks
    void init(int16_t* dramPool);

    // Called from parameterChanged when wavetable index changes
    void loadWavetable(int slot, int index);

    // Called from step() — checks SD card mount, pushes loaded tables to voices
    void update(PolyLofiVoice* voices[], int numVoices);

    // Test helper — inject wavetable data directly (bypasses SD card)
    void inject(int slot, const int16_t* data, uint32_t numWaves, uint32_t waveLength,
                PolyLofiVoice* voices[], int numVoices);

    // DRAM requirement for calculateRequirements
    static uint32_t dramBytes() { return NUM_SLOTS * WT_BUFFER_FRAMES * sizeof(int16_t); }
};
```

**Impact on DTC:** Replace 5 arrays + `cardMounted` with a single `WavetableManager wtManager;` member.

**Impact on `PolyLofi.cpp`:**
- `construct()`: `dtc->wtManager.init(dramPtr);` — one line replaces ~30
- `parameterChanged()`: `dtc->wtManager.loadWavetable(oscIdx, value);` — one line replaces ~10
- `step()`: `dtc->wtManager.update(dtc->voices, NUM_VOICES);` — one line replaces ~25
- `polyLofi_injectWavetable()`: delegates to `dtc->wtManager.inject()`

**Expected line reduction in PolyLofi.cpp:** ~80 lines removed, replaced by ~5 one-line calls.

**Verification:** Golden hashes unchanged — identical wavetable loading and push logic, just relocated.

---

## Refactoring Implementation Order

| Step | Task | Risk | Depends on |
|------|------|------|------------|
| **R1** | Split test files | Zero (no production code changes) | — |
| **R2** | Extract `processBlock` sub-methods | Very low (method extraction) | — |
| **R3** | Data-driven `parameterChanged` | Low (same dispatch logic) | — |
| **R4** | Move voice bodies to `.cpp` | Very low (file reorganization) | R2 (extract first, then move) |
| **R5** | Extract `WavetableManager` | Low (struct extraction) | — |

All steps are verified by the existing 85 golden-hash tests. If any hash changes, the refactoring introduced a bug.

---

## R6. Lower-Priority Refactoring (Worthwhile Follow-ups)

These are smaller, independent improvements that can be done at any time after R1–R5. Each stands alone.

### R6a. Extract MIDI Clock Tracker (~40 lines) — ✅ DONE

**Goal**: Pull `midiRealtime()` logic + `calculateSyncedLfoHz()` into a reusable `MidiClockTracker` class.

**Current problem:** MIDI clock state (`currentBPM`, `lastClockSampleCount`, `clockPulseCounter`, `clockRunning`) lives in DTC alongside unrelated synth state. The BPM derivation + sync multiplier logic is specific enough to be its own unit.

**Result:** Created `LofiParts/MidiClockTracker.h` (65 lines) with `advance()`, `onRealtimeByte()`, `getBPM()`, `isActive()`, `quarterNoteHz()`. Replaced 5 DTC fields with single `MidiClockTracker clockTracker;`. `midiRealtimeCb()` reduced from 25 lines to 4. All `clockRunning && currentBPM` guards replaced with `clockTracker.isActive()`. 54/54 tests pass, 0 REGEN.

---

### R6b. Extract Voice Allocator (~30 lines) — ✅ DONE

**Goal**: Pull the find-existing / find-free / steal-quietest logic out of `midiMessage()` into a dedicated `VoiceAllocator`.

**Current problem:** Voice allocation is embedded in a 94-line MIDI handler interleaved with pitch bend, CC, and aftertouch handling. The allocation policy (round-robin fallback + quietest-voice stealing) is a distinct concern.

**Result:** Created `LofiParts/VoiceAllocator.h` (50 lines) with template `allocate()` returning `{index, stolen}`. Note-on handling in `midiMessage()` reduced from 20 lines to 4. 54/54 tests pass, 0 REGEN.

---

### R6c. Move Enum String Tables to Shared Header — ✅ DONE

**Goal**: Extract `enumStringsWaveform[]`, `enumStringsMidiChannel[]`, `enumStringsFilterMode[]`, `enumStringsModSource[]`, `enumStringsModDest[]`, `enumStringsLfoShape[]`, `enumStringsGlideMode[]`, `enumStringsLfoSyncMode[]` from `PolyLofi.cpp` into a `PolyLofiParams.h`.

**Current problem:** ~50 lines of string tables clutter the main plugin file. Tests mirror some of these enums separately (risk of going out of sync). Other files (e.g., a future custom UI) may also need access to enum names.

**Result:** Created `PolyLofi/PolyLofiParams.h` (160 lines) with canonical `kParam*` enum, all 8 string tables, `kSyncMultipliers[]`, and `kWaveform*` enum. Removed ~150 lines from `PolyLofi.cpp`. Test parameter enum replaced with aliases derived from canonical enum (`kP_Osc1Waveform = kParamOsc1Waveform`). 54/54 tests pass, 0 REGEN.

---

### R6d. Split `LofiMorphOscillator.h` — Separate LFO Class — ✅ DONE

**Goal**: Move the `LFO` class (last ~80 lines of `LofiMorphOscillator.h`) into its own `LFO.h`.

**Current problem:** `LofiMorphOscillator.h` (794 lines) defines two distinct concerns: the `OscillatorFixedPoint` DSP engine and the `LFO` subclass. The LFO has its own shape enum, sample-and-hold behavior, and unipolar mode — these are modulation concerns, not oscillator concerns.

**Benefit:** Cleaner include semantics — files that only need LFO don't pull in all the PolyBLEP / wavetable / FM code. Makes each file shorter and easier to navigate.

**Result:** `LofiMorphOscillator.h` reduced from 795 → 702 lines. New `LFO.h` = 99 lines. `PolyLofiVoice.h` include changed to `LFO.h` (transitively includes oscillator). 54/54 tests pass, 0 REGEN.

---

### R6e. Encapsulate `PolyLofiVoice` Public Fields

**Goal**: Group the ~50 public member variables into a `VoiceParams` struct and pass it to `processBlock()`.

**Current problem:** The plugin directly mutates voice fields (`filterCutoff`, `filterResonance`, `delayTime`, `delayFeedback`, `delayMix`, `bitCrushBits`, `sampleReduceFactor`, oscillator arrays, etc.) from `parameterChanged()`. There's no boundary between "configuration" and "runtime state". A bug in the plugin could silently corrupt voice state without detection.

```cpp
struct VoiceParams {
    float filterCutoff, filterResonance, filterEnvAmount, drive;
    float delayTime, delayFeedback, delayMix;
    int   bitCrushBits, sampleReduceFactor;
    // ... etc
};

void processBlock(const VoiceParams& params, float* outL, float* outR, uint32_t n);
```

**Benefit:** Clear data-flow boundary: plugin writes params, voice reads params. Easier to reason about what state belongs where. Could enable lock-free parameter updates in the future.

**Note:** This is the most invasive of the R6 items — it touches both `PolyLofi.cpp` and `PolyLofiVoice`. Best done after R2/R3/R4 are settled.

---

### R6f. Hardware Debug Infrastructure

**Goal**: Add runtime diagnostics for debugging on real disting NT hardware.

**Important**: `NT_log()` is **NOT part of the official disting NT API**. It does not appear in `distingnt/api.h`, is not used in any example plugin, and only exists as a test harness stub. Do not rely on it for hardware debugging.

**The only real-time debug output on NT hardware is the screen**, using `NT_drawText()` inside the `draw()` callback. This is exactly the pattern used by Traffic.cpp — store values in DTC, render them in `draw()`.

#### Phase 1 — Debug Display Overlay (pre-hardware bringup) — ✅ DONE

Added 5 debug fields to DTC struct (`dbgActiveVoices`, `dbgPeakAmp`, `dbgLastNote`, `dbgFrameCounter`, `dbgNanCount`, `dbgBusErrors`), all guarded by `#if POLYLOFI_DEBUG`. Written in `step()` and `midiMessage()` with zero-cost float stores. Rendered in `draw()` at screen refresh rate using `NT_intToString`/`NT_floatToString` + `NT_drawText` — the same pattern as Traffic.cpp.

On-screen layout:
- Row 1 (y=48): `V:` (voice count) + `Pk:` (peak amplitude, 3 decimal places)
- Row 2 (y=56): `N:` (last MIDI note) + `F:` (frame counter mod 65536) + `NaN:` (if >0) + `BUS!` (if bus errors)

`draw()` returns `true` when debug is enabled (requests screen redraw) and `false` when disabled (no CPU cost).

#### Phase 2 — NaN/Inf Guard in Audio Output — ✅ DONE

Added `std::isfinite()` check on every output sample after voice summing, guarded by `#if POLYLOFI_DEBUG`. On Cortex-M7 this compiles to a single `VCMP` instruction per sample. Clamped samples are replaced with `0.f` and `dbgNanCount` is incremented. The count is displayed on screen in `draw()` only when non-zero — so in normal operation it costs nothing visible.

Covers both `outL` and `outR` (when stereo) buses.

#### Phase 3 — Bus Index Validation — ✅ DONE

Added bounds check on both `leftBus` and `rightBus` before pointer arithmetic, guarded by `#if POLYLOFI_DEBUG`. Invalid left bus → early return (skip entire output, increment `dbgBusErrors`). Invalid right bus → silently disable stereo for that block. `BUS!` indicator appears on screen in `draw()` if any errors occurred.

#### Phase 4 — Wire Up Oscillator Debug Pointers

`OscillatorFixedPoint` already has `setdebugValuePointers(float*, float*, float*, float*)` (used by Traffic) but PolyLofi never wires it up. Connect voice 0's oscillator debug pointers to the DTC debug slots to see real-time frequency, phase increment, etc. on screen.

**Effort**: ~5 lines in `construct()` or `step()`.

#### Phase 5 — Conditional Compilation for Release Builds — ✅ DONE

All debug code is wrapped in `#if POLYLOFI_DEBUG` / `#endif`. The macro defaults to `0` in `PolyLofi.cpp` (via `#ifndef POLYLOFI_DEBUG`) so **release builds have zero debug overhead by default**.

To enable for hardware bringup, pass `-DPOLYLOFI_DEBUG=1` in the Makefile's embedded target:

```makefile
# In Makefile, embedded target:
TARGET_CPPFLAGS += -DPOLYLOFI_DEBUG=1   # bringup
# Remove the flag for release
```

Verified: both `POLYLOFI_DEBUG=0` and `POLYLOFI_DEBUG=1` compile cleanly with zero warnings, and all 57 tests pass with identical golden hashes in both modes.

#### Implementation Order

| Phase | When | What | Status |
|-------|------|------|--------|
| 1 | Before first power-on | Debug display overlay | ✅ Done |
| 2 | Before first power-on | NaN/Inf guard | ✅ Done |
| 3 | Before first power-on | Bus index validation | ✅ Done |
| 4 | After first successful boot | Oscillator debug pointers | ❌ |
| 5 | After stabilization | Conditional compilation | ✅ Done |

---

### R6 Priority Summary

| Task | Impact | Effort | Depends on |
|------|--------|--------|------------|
| R6a MIDI Clock Tracker | Reusability | Easy (~40 lines) | — | ✅ Done |
| R6b Voice Allocator | Testability | Easy (~30 lines) | — | ✅ Done |
| R6c Shared Enum Header | Maintainability | Trivial (~50 lines moved) | — | ✅ Done |
| R6d Split LFO class | File clarity | Easy (~80 lines moved) | — | ✅ Done |
| R6e VoiceParams struct | Architecture | Medium (touches many call sites) | R2, R4 |
| R6f Debug Infrastructure | Hardware bringup | Easy-Medium | — | ✅ Phases 1-3+5 Done |

---

## 14. Live Audio Input → Wavetable Capture ("Snap Table")

### Concept

Capture a chunk of live audio from a disting NT input bus, slice it into single-cycle waveforms, and load it as a wavetable — all at runtime, no SD card required. A trigger (parameter button or CV gate) "snaps" the audio. The captured table can be saved/restored inside presets via the `serialise`/`deserialise` JSON API.

### Feasibility: **Medium** — all building blocks already exist

| Requirement | Available? | How |
|---|---|---|
| Audio input access | ✅ | `NT_PARAMETER_AUDIO_INPUT` → `busFrames` pointer in `step()` |
| Wavetable buffer | ✅ | Existing DRAM buffers (`wtRequest[i].table`, 524 K frames each) |
| Push table to voices | ✅ | `setOscWavetable(slot, base, numWaves, waveLength)` already works |
| Trigger mechanism | ✅ | `NT_PARAMETER_SELECTOR` (button on screen) or CV gate input |
| Preset serialization | ✅ | `_NT_jsonStream` / `_NT_jsonParse` API (write/read numbers, arrays) |
| File write to SD | ❌ | Plugins are read-only on SD card — **no WAV export** |

### Architecture

#### Signal Flow

```
Audio Input Bus ──▶ Ring Buffer (capture window)
                         │
                    [SNAP trigger]
                         │
                         ▼
              Slice into N single-cycle waves
                         │
                         ▼
              Write into WT DRAM buffer (int16_t Q1.15)
                         │
                         ▼
              Push to all 8 voices via setOscWavetable()
                         │
                         ▼
              Mark as "captured" → serialise on preset save
```

#### Capture Sizing

The user selects a **wave length** (samples per cycle) and a **wave count** (how many consecutive cycles to capture). The total capture = `waveLength × waveCount` samples.

| Wave Length | Wave Count | Total Samples | Total Bytes (Q15) | Capture Time @44.1k | Character |
|-------------|-----------|---------------|-------------------|--------------------|-----------|
| 256 | 32 | 8,192 | 16 KB | 186 ms | Classic lo-fi (PPG size) |
| 256 | 64 | 16,384 | 32 KB | 371 ms | Standard table |
| 512 | 32 | 16,384 | 32 KB | 371 ms | Higher fidelity per wave |
| 2048 | 16 | 32,768 | 64 KB | 743 ms | Serum-class fidelity |

**Recommended default**: 256 samples × 64 waves = 16,384 samples (32 KB). This fits the lo-fi aesthetic (nearest-neighbor lookup in 256-sample waves creates audible spectral folding), captures ~370 ms of evolving audio, and serializes compactly.

**Maximum**: Constrained by the existing `WT_BUFFER_FRAMES = 524,288`. Even at 2048 × 256 = 524,288 samples that fits exactly. But for JSON serialization, smaller is better.

#### Ring Buffer Strategy

Instead of "press button, wait, then capture finishes", use a **pre-roll ring buffer**:

1. `step()` continuously writes audio input into a ring buffer (size = max capture length).
2. When SNAP is triggered, the ring buffer already contains the audio — the capture is **instant**.
3. The captured data is copied from the ring buffer into the WT DRAM buffer, converted to `int16_t` Q1.15.
4. No latency, no "recording" state needed.

**Memory cost**: Ring buffer = `float[32768]` = 128 KB in DRAM (one extra allocation). This could be shared with an unused WT slot if the user is only capturing to one oscillator.

**Alternative — post-trigger capture**: Simpler (no ring buffer, just fill a buffer after trigger), but adds latency equal to the capture time. Acceptable at 256×64 = 370 ms, but less immediate-feeling. Could offer both: a "pre" mode (ring buffer, instant) and a "post" mode (record-after-trigger, no extra memory).

### New Parameters (4–5)

| Parameter | Type | Range | Default | Notes |
|---|---|---|---|---|
| WT Capture Input | audio input | Bus selector | 0 (None) | `NT_PARAMETER_AUDIO_INPUT` — which bus to capture from |
| WT Capture Target | enum | Osc1 / Osc2 / Osc3 | Osc1 | Which wavetable slot receives the capture |
| WT Capture Length | enum | 256×32 / 256×64 / 512×32 / 2048×16 | 256×64 | Presets for wave length × count |
| WT Capture Snap | selector | button | — | `NT_PARAMETER_SELECTOR` — triggers capture |
| WT Capture Mode | enum | Pre (ring) / Post (record) | Pre | Optional — start with Post if simpler |

### Implementation Plan

#### Phase 1 — Post-Trigger Capture (Simplest MVP)

1. **Add `kParamWTCaptureInput`** (`NT_PARAMETER_AUDIO_INPUT`) — selects which bus to record from.
2. **Add `kParamWTCaptureSnap`** (`NT_PARAMETER_SELECTOR`) — button trigger.
3. **Add capture state to DTC**:
   ```cpp
   enum CaptureState { kCaptureIdle, kCaptureRecording, kCaptureDone };
   CaptureState captureState = kCaptureIdle;
   int captureTarget = 0;           // which osc slot (0–2)
   int captureWaveLength = 256;
   int captureWaveCount = 64;
   int captureSamplesWritten = 0;
   float* captureBuffer = nullptr;  // temp float buffer in DRAM
   ```
4. **In `step()`**: When `captureState == kCaptureRecording` and capture input bus is valid:
   - Copy audio input samples into `captureBuffer` (float).
   - Increment `captureSamplesWritten`.
   - When `captureSamplesWritten >= captureWaveLength * captureWaveCount`:
     - Convert `captureBuffer` float → `int16_t` Q1.15 into the target WT DRAM buffer.
     - Push to all 8 voices via `setOscWavetable()`.
     - Set `captureState = kCaptureDone`.
     - Mark `capturedWT[target] = true` for serialization.
5. **In `parameterChanged(kParamWTCaptureSnap)`**: Set `captureState = kCaptureRecording`, `captureSamplesWritten = 0`.

**Memory**: Reuse the existing WT DRAM buffer for the target oscillator. The float capture buffer needs `captureWaveLength * captureWaveCount * sizeof(float)` = 64 KB at default settings. Allocate from DRAM alongside the WT buffers.

**Cost per block during capture**: One `memcpy` of `numFrames * sizeof(float)` per block. Negligible.

#### Phase 2 — Pre-Roll Ring Buffer (Instant Snap)

1. Replace the post-trigger linear buffer with a ring buffer that runs continuously when a capture input bus is selected.
2. On SNAP, copy the last N samples from the ring buffer instead of waiting.
3. **Memory**: `float[32768]` ring buffer = 128 KB DRAM.
4. **Cost during idle**: One `memcpy` per block even when not capturing — trivial.

#### Phase 3 — Zero-Crossing Alignment

Raw audio slicing at arbitrary boundaries causes clicks between waves during morph scanning. Add optional zero-crossing alignment:

1. After capture, scan each `waveLength` boundary for the nearest zero crossing (±16 samples).
2. Shift each wave slice to start at the zero crossing.
3. Apply a tiny crossfade (4–8 samples) at each boundary.

This is a one-time post-processing pass on the captured buffer, not a per-sample cost.

#### Phase 4 — Pitch-Locked Capture

For melodically useful wavetables, optionally detect the pitch of the input and set `waveLength` to one period of that pitch:

- Run a simple autocorrelation pitch detector on the first ~2048 samples of the capture buffer.
- Detected period → `waveLength`. E.g., A4 (440 Hz) at 44.1 kHz → period ~100 samples.
- Remaining buffer / period = `waveCount`.
- This creates a wavetable where each wave is one clean cycle, and morph scanning sweeps through the timbral evolution of the input.

**Difficulty**: Medium — autocorrelation is ~50 lines, but edge cases (noise, polyphonic input, sub-harmonics) require care.

### JSON Preset Serialization

The disting NT's `serialise()` / `deserialise()` callbacks write plugin state into the preset JSON file. This is the **only** way to persist custom data — plugins cannot write to SD card.

#### What Gets Serialized

```json
{
  "capturedWT": [
    {
      "target": 0,
      "waveLength": 256,
      "waveCount": 64,
      "data": [1234, -5678, 9012, ...]
    }
  ]
}
```

#### Size Considerations

| Config | Samples | JSON (int16 as numbers) | Base64 (binary) |
|--------|---------|------------------------|-----------------|
| 256×32 | 8,192 | ~50 KB | ~22 KB |
| 256×64 | 16,384 | ~100 KB | ~44 KB |
| 512×32 | 16,384 | ~100 KB | ~44 KB |
| 2048×16 | 32,768 | ~200 KB | ~88 KB |

**JSON `addNumber(int)` approach**: Each sample is a separate `addNumber()` call inside a JSON array. 16,384 calls for the default config. The NT's JSON writer handles this — it's a preset save, not a real-time operation.

**Concern**: 100 KB of JSON per captured wavetable is large for a preset file. The disting NT preset format is designed for parameter values (a few hundred bytes), not raw audio data.

**Mitigation options** (in order of preference):
1. **Use smallest practical table**: 256×32 = 8,192 samples → ~50 KB JSON. Still large but potentially acceptable.
2. **Delta encoding**: Store the difference from one sample to the next. Wavetable data has high sample-to-sample correlation, so deltas are small numbers (1–3 digits instead of 5). Could halve the JSON size.
3. **Base64 string**: Pack `int16_t[]` into a binary blob, base64-encode it, store as a single `addString()`. 16,384 × 2 bytes = 32 KB → ~44 KB base64 string. More compact than per-number JSON, but requires a base64 codec (~30 lines).
4. **Don't serialize**: Just re-capture after loading a preset. Simplest, but loses the captured wavetable on power cycle.

**Recommendation**: Start with option 1 (smallest table + raw `addNumber` per sample) for the MVP. Profile the actual preset save/load time on hardware. If it's unacceptable, add base64 encoding in a follow-up.

#### Serialization Implementation

```cpp
// In serialise():
void polyLofi_serialise(_NT_algorithm* self, _NT_jsonStream& stream) {
    stream.openObject();
    stream.addMemberName("capturedWT");
    stream.openArray();
    for (int i = 0; i < 3; ++i) {
        if (dtc->capturedWT[i]) {
            stream.openObject();
            stream.addMemberName("target");
            stream.addNumber(i);
            stream.addMemberName("waveLength");
            stream.addNumber(dtc->captureWaveLength);
            stream.addMemberName("waveCount");
            stream.addNumber(dtc->captureWaveCount);
            stream.addMemberName("data");
            stream.openArray();
            int total = dtc->captureWaveLength * dtc->captureWaveCount;
            const int16_t* buf = dtc->wtRequest[i].table;
            for (int s = 0; s < total; ++s)
                stream.addNumber((int)buf[s]);
            stream.closeArray();
            stream.closeObject();
        }
    }
    stream.closeArray();
    stream.closeObject();
}
```

```cpp
// In deserialise():
bool polyLofi_deserialise(_NT_algorithm* self, _NT_jsonParse& parse) {
    int numMembers;
    if (!parse.numberOfObjectMembers(numMembers)) return false;
    if (parse.matchName("capturedWT")) {
        int numSlots;
        if (parse.numberOfArrayElements(numSlots)) {
            for (int i = 0; i < numSlots; ++i) {
                // parse target, waveLength, waveCount, data array
                // write into wtRequest[target].table
                // push to voices
            }
        }
    }
    return true;
}
```

### Interaction With Existing Wavetable System

| Scenario | Behavior |
|---|---|
| Osc has SD wavetable loaded, user snaps capture to same osc | Capture **overwrites** the WT buffer. SD table is gone until re-selected. |
| User selects a different SD wavetable index after capturing | Normal SD load replaces the captured data. `capturedWT[slot] = false`. |
| Preset save with both SD tables and captured table | Only captured slots are serialized. SD-loaded slots just store the index parameter (re-loaded from SD on preset recall). |
| Preset load with captured table but no matching SD card | Captured table deserializes from JSON — works without SD card. |
| Power cycle without saving preset | Captured table is lost (DRAM is volatile). |

### Risks & Mitigations

| Risk | Severity | Mitigation |
|---|---|---|
| JSON preset size too large (100+ KB) | Medium | Start with 256×32 (50 KB). Add base64 or delta encoding if needed. |
| Capture buffer competes with WT DRAM | Low | Reuse target WT slot's existing buffer. Only the float staging buffer is extra (~64 KB). |
| Audio input may be silent or noisy at snap time | Low | User responsibility — they choose when to snap. A level indicator in `draw()` could help. |
| Zero-crossing misalignment causes clicks during morph sweep | Medium | Phase 3 adds alignment. For MVP, accept minor artifacts (fits lo-fi aesthetic). |
| Preset recall on different sample rate stretches table | Low | Store sample rate in JSON. On load, if mismatched, optionally resample (or just accept the pitch shift — lo-fi!). |
| `serialise`/`deserialise` callbacks not yet wired in PolyLofi | Low | Need to add them to the factory struct (currently `NULL`). One-time wiring. |

### Implementation Order

| Phase | Difficulty | What | Depends on |
|-------|-----------|------|------------|
| **Phase 1** | Easy-Medium | Post-trigger capture (record after snap) + push to voices | — |
| **Phase 2** | Easy | Pre-roll ring buffer (instant snap) | Phase 1 |
| **Phase 3** | Easy | Zero-crossing alignment post-processing | Phase 1 |
| **Phase 4** | Medium | Pitch-locked capture (autocorrelation) | Phase 1 |
| **JSON Save** | Easy-Medium | `serialise()` / `deserialise()` with `addNumber` per sample | Phase 1 |
| **JSON Optimize** | Low priority | Base64 or delta encoding if preset size is a problem | JSON Save |

### Priority Assessment

| Priority | Rationale |
|---|---|
| 🟠 **Medium** | Unique feature — most hardware synths can't do this. Builds on existing WT infrastructure. But requires 4–5 new params + serialization wiring, and the JSON size question needs hardware testing. Not blocking any other feature. |