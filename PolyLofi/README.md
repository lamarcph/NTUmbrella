# PolyLofi — Polyphonic Synthesizer for Expert Sleepers disting NT

PolyLofi is a full-featured polyphonic synthesizer algorithm for the
[Expert Sleepers disting NT](https://www.expert-sleepers.co.uk/distingNT.html)
Eurorack module. It provides up to 12 voices, three oscillators per voice,
four filter models, three envelopes, three LFOs, a flexible modulation matrix,
per-voice delay, and a built-in preset system — all running on the disting NT
hardware.

---

## Table of Contents

1. [Getting Started](#getting-started)
2. [Parameter Pages](#parameter-pages)
   - [Oscs — Oscillators](#oscs--oscillators)
   - [Filter](#filter)
   - [Amp — Amplitude Envelope & Performance](#amp--amplitude-envelope--performance)
   - [LFOs](#lfos)
   - [Mod Matrix](#mod-matrix)
   - [FM/Sync — FM Synthesis & Mod Envelope](#fmsync--fm-synthesis--mod-envelope)
   - [Effects — Delay & Bit Crusher](#effects--delay--bit-crusher)
   - [Setup — Output, MIDI & Presets](#setup--output-midi--presets)
3. [Modulation Matrix Reference](#modulation-matrix-reference)
4. [MIDI Implementation](#midi-implementation)
5. [Voice Allocation & Legato](#voice-allocation--legato)
6. [Preset System](#preset-system)
7. [Factory Presets](#factory-presets)
8. [Signal Flow](#signal-flow)
9. [Tips & Recipes](#tips--recipes)

---

## Getting Started

1. Copy `plugins/PolyLofi.o` to the disting NT SD card plugin folder.
2. Load the algorithm on a slot. You will be prompted to set the **voice count**
   (1–12, default 8). More voices use more CPU; 8 is a good balance.
3. Connect a MIDI source (USB, TRS, or the disting's internal MIDI bus).
4. Play notes — PolyLofi responds immediately with the default init patch
   (three detuned sawtooth oscillators through a low-pass filter).
5. Browse factory presets via **Setup → Load Preset**.

---

## Parameter Pages

PolyLofi organises its controls across eight pages. Use the disting NT encoder
to navigate between pages and edit parameters.

### Oscs — Oscillators

Three independent oscillators, each with the same set of controls.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Waveform** | Sine / Square / Triangle / Sawtooth / Morph / PolyBLEP Saw / PolyBLEP Sqr / Wavetable / Noise | Sawtooth | Oscillator waveform. PolyBLEP variants are band-limited and alias-free at high pitches. |
| **Wavetable** | 0–255 | 0 | Wavetable selection (only used when Waveform is set to Wavetable). |
| **Semitone** | −48 to +48 | 0 | Coarse pitch offset in semitones (±4 octaves). |
| **Fine** | −100 to +100 | 0 | Fine pitch offset in cents. |
| **Morph** | 0–100% | 0% | Waveform morph amount. On Square / PolyBLEP Sqr this controls pulse width. On other waveforms it sweeps between harmonic shapes. |
| **Level** | 0–100% | 33% | Oscillator output level in the mix. |

At the bottom of this page:

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **LFO2 › Vibrato** | 0–100 cents | 0 | Direct LFO2-to-pitch vibrato depth. Applies to all oscillators. |

> **Tip:** Set all three oscillators to PolyBLEP Saw with small Fine
> detuning (+7 / 0 / −7 cents) for a classic supersaw sound.

---

### Filter

A zero-delay-feedback (ZDF) filter with four selectable models and a
dedicated filter envelope.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Cutoff** | 0–10 000 | 5 000 | Base filter cutoff frequency (exponentially mapped). |
| **Resonance** | 0–100% | 10% | Filter resonance / Q. High values on MS-20 and Diode models can self-oscillate. |
| **Filter Env Amt** | 0–10 000 | 5 000 | How much the filter envelope opens the cutoff. |
| **Filter Mode** | LP2 / LP4 / HP2 / BP2 / NOTCH2 / HP2+LP2 / BYPASS | LP2 | Filter response shape. LP4 gives a steeper 24 dB/oct slope. |
| **Filter Model** | SVF / Ladder / MS-20 / Diode | SVF | Filter character. Each model has a different resonance and saturation behaviour. |
| **Drive** | 1.0–10.0 | 1.0 | Filter saturation. Applied inside the filter stages — on multi-stage modes (LP4, HP2+LP2) it acts as inter-stage distortion. Higher values add harmonics and warmth. |
| **Key Tracking** | 0–100% | 0% | How much the filter cutoff follows the MIDI note pitch. At 100% the filter tracks the keyboard 1:1. |
| **LFO1 › Cutoff** | −100% to +100% | 0% | Direct LFO1-to-cutoff modulation depth (±4 octaves at full range). |

**Filter Envelope** (on the same page):

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Attack** | 0–3 000 ms | 50 ms | Time to reach peak. |
| **Decay** | 0–3 000 ms | 100 ms | Time from peak to sustain level. |
| **Sustain** | 0–100% | 80% | Level held while key is pressed. |
| **Release** | 0–3 000 ms | 200 ms | Time from key release to zero. |
| **Shape** | −99% to +99% | 0% | Envelope curve: negative = logarithmic (snappy), positive = exponential (slow rise). |

---

### Amp — Amplitude Envelope & Performance

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Attack** | 0–3 000 ms | 10 ms | Amplitude envelope attack. |
| **Decay** | 0–3 000 ms | 100 ms | Amplitude envelope decay. |
| **Sustain** | 0–100% | 80% | Amplitude envelope sustain level. |
| **Release** | 0–3 000 ms | 500 ms | Amplitude envelope release. |
| **Shape** | −99% to +99% | 0% | Envelope curve shape. |
| **Vel Sens** | 0–100% | 100% | Velocity sensitivity. At 100% quiet notes are quiet; at 0% all notes play at full volume regardless of velocity. |
| **Glide Time** | 0–3 000 ms | 0 ms | Portamento time. |
| **Glide Mode** | Off / Always / Legato | Off | When glide is active. "Legato" only glides when notes overlap. |
| **Legato** | Off / On | Off | Mono legato mode — forces one voice, holds envelopes on overlapping notes. |

---

### LFOs

Three identical LFOs. Each can run freely or sync to MIDI/CV clock.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Speed** | 0–1 000 | 500 | LFO rate (exponentially mapped, ~0.01 Hz to ~2.7 kHz). |
| **Shape** | Sine / Triangle / Square / Saw / Morph / S&H | Sine | LFO waveshape. S&H = sample-and-hold (random). |
| **Unipolar** | Off / On | Off | Off = bipolar (−1 to +1). On = unipolar (0 to +1). |
| **Morph** | 0–100% | 0% | Waveshape morph (same as oscillator morph). |
| **Sync Mode** | Free / 4 bar / 2 bar / 1 bar / 1/2 / 1/4 / 1/8 / 1/16 / 1/4T / 1/8T / 1/4. / 1/8. | Free | Clock sync division. Requires MIDI clock or a CV clock patched to the Clock Input. |
| **Key Sync** | Off / On | Off | When On, the LFO phase resets on each new note-on. |

---

### Mod Matrix

Four modulation routing slots. Each slot connects one source to one
destination with a bipolar amount. These run per-voice where applicable.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Source** | (see table below) | Off | Modulation source signal. |
| **Dest** | (see table below) | Cutoff | Modulation target parameter. |
| **Amount** | −100% to +100% | 0% | Modulation depth. Negative values invert the source. |

---

### FM/Sync — FM Synthesis & Mod Envelope

True-through-zero FM and hard sync between the three oscillators.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **FM 3›2 Depth** | 0–10 000 | 0 | FM modulation intensity: Osc 3 modulates Osc 2. |
| **FM 3›1 Depth** | 0–10 000 | 0 | FM: Osc 3 → Osc 1. |
| **FM 2›1 Depth** | 0–10 000 | 0 | FM: Osc 2 → Osc 1. |
| **Sync 3›2** | Off / On | Off | Hard sync: Osc 3 resets Osc 2 phase. |
| **Sync 3›1** | Off / On | Off | Hard sync: Osc 3 resets Osc 1 phase. |
| **Sync 2›1** | Off / On | Off | Hard sync: Osc 2 resets Osc 1 phase. |

**Mod Envelope** (dedicated ADSR for use as a mod matrix source):

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Attack** | 0–3 000 ms | 10 ms | |
| **Decay** | 0–3 000 ms | 100 ms | |
| **Sustain** | 0–100% | 80% | |
| **Release** | 0–3 000 ms | 200 ms | |
| **Shape** | −99% to +99% | 0% | |

---

### Effects — Delay & Bit Crusher

Per-voice delay and lo-fi effects applied after the filter.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Delay Time** | 0–1 000 ms | 500 ms | Delay time (or synced when Delay Sync is set). |
| **Delay Sync** | Free / 4 bar … 1/8. | Free | Sync delay time to MIDI clock. |
| **Delay Feedback** | 0–100% | 25% | Amount of signal fed back into the delay. |
| **Delay Mix** | 0–100% | 25% | Dry/wet mix. 0% = fully dry. |
| **Delay Diffusion** | 0–100% | 0% | Allpass diffuser amount for a spacious, reverb-like quality. |
| **Delay FB Filter** | Off / LP / HP | Off | Filter applied inside the feedback loop. |
| **Delay FB Freq** | 200–18 000 Hz | 3 000 Hz | Cutoff frequency of the feedback filter. |
| **Delay Pitch Track** | Off / Unison / Oct −1 / Oct +1 / Fifth | Off | Pitch-tracked comb delay — the delay time follows the played note. Great for Karplus-Strong plucked sounds. |
| **Bit Crush** | 1–16 bits | 16 | Bit depth reduction. 16 = no effect. |
| **Sample Reduce** | 1–32× | 1 | Sample rate reduction factor. 1 = no effect. |

---

### Setup — Output, MIDI & Presets

Global routing and preset management.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| **Output** | Bus 1–13 | 1 | Left / mono output bus. |
| **Output Mode** | Replace / Add | Replace | How the output is mixed into the bus. |
| **Right Output** | 0–28 | 0 | Right stereo output bus (0 = mono, same as Output). |
| **Right Output Mode** | Replace / Add | Replace | |
| **Master Volume** | 0–100% | 70% | Final output gain. |
| **Pan Spread** | 0–100% | 0% | Spread voices across the stereo field. Voice 0 stays centre. |
| **MIDI Channel** | All / 1–16 | All | Which MIDI channel to listen on. |
| **Clock Input** | 0–28 | 0 | CV input for external clock (1 pulse-per-quarter-note). |
| **Load Preset** | Slot 0–13 | — | Confirm to load the selected factory/user preset. |
| **Save Slot** | Slot 0–13 | 0 | Select which slot to save the current patch into. |
| **Save** | Off / On | Off | Set to On to save the current patch. Automatically resets to Off so you can save again. |

> **Note:** Output, Right Output, Master Volume, Pan Spread, MIDI Channel,
> and Clock Input are **setup parameters** — they are *not* saved with
> presets, so your routing stays consistent when loading different sounds.

---

## Modulation Matrix Reference

### Sources

| Source | Description |
|--------|-------------|
| Off | No modulation |
| LFO | LFO 1 output |
| LFO2 | LFO 2 output |
| LFO3 | LFO 3 output |
| Amp Env | Amplitude envelope (0 → 1 → sustain → 0) |
| Filter Env | Filter envelope |
| Mod Env | Mod envelope (FM/Sync page) |
| Velocity | MIDI note-on velocity (0–1) |
| Mod Wheel | MIDI CC 1 (0–1) |
| Aftertouch | Channel aftertouch (0–1) |
| Note Random | Random value generated per note (constant for the note's duration) |
| Key Track | Keyboard tracking: (note − 60) / 60 — bipolar, centred on middle C |

### Destinations

| Destination | What it controls |
|-------------|-----------------|
| Cutoff | Filter cutoff frequency |
| Resonance | Filter resonance |
| Amp Attack | Amp envelope attack time |
| Amp Decay | Amp envelope decay time |
| Amp Release | Amp envelope release time |
| Filter Attack | Filter envelope attack time |
| Filter Decay | Filter envelope decay time |
| Filter Release | Filter envelope release time |
| Osc1 Morph | Oscillator 1 morph / pulse width |
| Osc2 Morph | Oscillator 2 morph / pulse width |
| Osc3 Morph | Oscillator 3 morph / pulse width |
| All Morph | All three oscillators' morph simultaneously |
| FM 3›2 | FM depth: Osc 3 → Osc 2 |
| FM 3›1 | FM depth: Osc 3 → Osc 1 |
| FM 2›1 | FM depth: Osc 2 → Osc 1 |
| Delay Time | Delay buffer time |
| Delay Fdbk | Delay feedback amount |
| Delay Mix | Delay dry/wet mix |
| Pitch | Global pitch (all oscillators) |
| Drive | Filter saturation (inter-stage on LP4/HP2+LP2) |
| Flt Env Amt | Filter envelope depth |
| Osc1 Level | Oscillator 1 output level |
| Osc2 Level | Oscillator 2 output level |
| Osc3 Level | Oscillator 3 output level |
| Osc1 Pitch | Oscillator 1 pitch (semitones) |
| Osc2 Pitch | Oscillator 2 pitch (semitones) |
| Osc3 Pitch | Oscillator 3 pitch (semitones) |
| LFO Speed | All LFO speeds |

---

## MIDI Implementation

| Message | Function |
|---------|----------|
| Note On | Triggers a voice. Velocity 0 is treated as Note Off. |
| Note Off | Releases the voice playing that note. |
| Pitch Bend | ±2 semitones (14-bit resolution). |
| Channel Aftertouch | Mapped to "Aftertouch" mod source for all active voices. |
| Polyphonic Aftertouch | Per-note aftertouch to the matching voice. |
| CC 1 (Mod Wheel) | Mapped to "Mod Wheel" mod source. |
| CC 64 (Sustain Pedal) | Holds notes until released. Supports piano-style retriggering. |
| MIDI Clock | 24 PPQN. Used for LFO sync and Delay sync. |

---

## Voice Allocation & Legato

- **Polyphonic** (default): Up to 12 voices (configurable at load time).
  When all voices are active and a new note arrives, the quietest voice
  is stolen with a short (~6 ms) crossfade to avoid clicks.
- **Legato mode** (Amp page → Legato = On): Forces monophonic playback.
  When a new note overlaps the previous one, the pitch changes (with
  optional glide) but the envelopes continue without retriggering —
  ideal for smooth bass lines and lead melodies.
- **Glide** works in both polyphonic and legato modes. In "Always" mode
  every new note glides from the previous pitch. In "Legato" mode glide
  only happens on overlapping notes.

---

## Preset System

PolyLofi has **14 preset slots** (0–13), all pre-loaded with factory sounds.
You can overwrite any slot with your own patches.

**To load a preset:**
Navigate to **Setup → Load Preset**, select the slot number, and confirm.
All synth parameters update immediately. Setup parameters (output routing,
volume, MIDI channel) are *not* affected.

**To save a preset:**
1. Navigate to **Setup → Save Slot** and select the destination slot (0–13).
2. Set **Save** to On. The current patch is written to that slot and Save
   automatically resets to Off.

Presets are persisted in the disting NT's JSON serialisation — they survive
power cycles.

---

## Factory Presets

| Slot | Name | Character |
|------|------|-----------|
| 0 | **Supersaw** | JP-8000 style trance lead — three detuned PolyBLEP saws, LP4 filter, wide stereo spread, subtle delay. |
| 1 | **Acid Bass** | TB-303 acid — single saw through Diode LP4, screaming resonance, short filter envelope, glide. |
| 2 | **Virus Lead** | Detuned aggressive lead — two saws + one square, LP4, driven, LFO vibrato. |
| 3 | **PWM Pad** | Prophet/Juno string pad — three PolyBLEP squares, slow LFO → pulse width, LP2. |
| 4 | **Hoover** | Classic 90s rave — wide-detuned saws across octaves, LP4, driven, always-glide. |
| 5 | **Fizzy Keys** | DX-style electric piano — sine + octave sine + noise, LP2, plucky, velocity-sensitive. |
| 6 | **Rez Sweep** | MS-20 resonant sweep — single saw, MS-20 LP4, near-self-oscillation resonance, huge filter envelope. |
| 7 | **Sync Lead** | Hard sync screamer — Osc 1 synced to Osc 3 (+12 semitones), Mod Env sweeps slave pitch. |
| 8 | **Lofior** | Ambient comb-delay drone — noise + detuned saws, LP4, pitch-tracked delay, HP feedback filter. |
| 9 | **Crushed** | Lo-fi texture — saw + square + noise, 8-bit bit crush, 4× sample reduce, LP2. |
| 10 | **Moog Bass** | Classic Moog — single saw through Ladder LP4, high resonance, key tracking, punchy filter envelope. |
| 11 | **Tape Piano** | TZFM bell + tape echo — sine carrier with FM, mod envelope → FM depth, delay with wobble. |
| 12 | **Scream Lead** | MS-20 screaming — pure saw, MS-20 LP4, aggressive resonance + drive, Mod Env → cutoff. |
| 13 | **303 Acid** | Diode acid squelch — PolyBLEP square, Diode LP4, huge filter envelope, legato glide, drive. |

---

## Signal Flow

```
Osc 1 ─┐
Osc 2 ─┼─► Level Mix ─► FM / Hard Sync ─► Filter (with Drive) ─► Bit Crush ─► Sample Reduce
Osc 3 ─┘                                  (inter-stage sat.)              │
                                                                          ▼
                                                                  Per-voice Delay
                                                                 (with diffusion)
                                                                          │
                                                                          ▼
                                                               Amp Envelope × Velocity
                                                                          │
                                                                          ▼
                                                             Stereo Pan ─► Bus Mix ─► Master Volume ─► Output
```

---

## Tips & Recipes

**Supersaw pad:** Three PolyBLEP Saws, detune Osc 1 to +7 cents and
Osc 3 to −7 cents, set Filter to LP4 with moderate cutoff, add Pan Spread
for width.

**Acid bass:** Single Sawtooth or PolyBLEP Square, Diode or Ladder LP4,
high Resonance, short Filter Env (Attack 0 / Decay 200 / Sustain 0),
high Filter Env Amt, Glide Mode = Always, Glide Time ~80 ms.

**Karplus-Strong pluck:** Set oscillator to Noise, very short Amp Decay
(~30 ms) and zero Sustain, enable Delay Pitch Track = Unison, Feedback
~85%, Mix 100%, Filter to LP2 with moderate cutoff.

**FM bell:** Osc 1 Sine as carrier, Osc 3 Sine as modulator (+12 or +19
semitones), use FM 3›1 Depth with Mod Env → FM 3›1 in the mod matrix for
a decaying metallic bell tone.

**PWM strings:** Three PolyBLEP Squares with slight detuning, route
LFO 1 → All Morph in the mod matrix at ~30% amount with a slow LFO speed
for classic analogue PWM movement.

**Lo-fi textures:** Set Bit Crush to 8 and Sample Reduce to 4, add Delay
with HP feedback filter for a retro, degraded sound.

---

## License

PolyLofi is released under the **GNU General Public License v3.0**.
See [LICENSE](../LICENSE) for the full text.

This software uses the [disting NT API](https://github.com/expertsleepersltd/distingNT_API)
by Expert Sleepers Ltd, which is licensed under the MIT License.

PolyLofi is provided **as-is, with no warranty of any kind**. Use at your
own risk. The author is not responsible for any damage to equipment or
hearing resulting from use of this software.

---

*PolyLofi — a polyphonic lo-fi synthesizer for disting NT.*
