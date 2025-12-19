# Lofi Oscillator — README_LOFI_OSC

## Overview
- What it is: A 9 detunable lo-fi oscillators plugin that supports Sine/Square/Triangle/Saw/Morph waveforms, per-oscillator detune models (instrument-like partials), V/Oct input, linear/TZ FM input, morph modulation and harmonics control.

## Quick Start
- Load: Insert the algorithm in your host (platform-specific).
- Default sound: Middle C (261.63 Hz) with default single unison behavior.
- Try first: set `Waveform = Sine`, raise `Detune` a bit, pick a `Tuning Model`, and increase `Harmonics` to hear tonal changes.

## Controls & Parameters
(Names match the parameter list in `LofimplexOsc.cpp`.)

- `Output`: Audio output selection / output mode.
- `Output Mode`: Replace or Add (mix mode).
- `Lin/TZFM FM input`: Audio input slot for linear / through-zero FM.
- `V/8 input`: Audio input slot for V/Oct control (volt per octave).
- `Morph input`: Audio input for shape morph modulation.
- `Harmonics input`: Audio input for per-sample harmonics control.
- `Waveform` (enum 0..4): `Sine`, `Square`, `Triangle`, `Sawtooth`, `Morph`.
- `Semi`: -48..48 semitones (coarse transpose).
- `Fine`: -50..50 cents (fine tune).
- `Volt/Octave`: -5000..5000 (scaled with `kNT_scaling1000` — host uses mV units).
- `Morph`: 0..1000 (shape morph baseline).
- `Detune`: 0..10000 (amount of detune / spread). Value 5000 center of model tuning. 
- `Tuning Model`: 0..7 enum — pick detune/partial model (see Detune Models).
- `Gain`: 0..20000 (output gain; default ~5000).
- `FM depth`: 0..22000 Hz (linear FM depth).
- `Morph mod depth`: 0..1000 (scales incoming morph input).
- `Harmonics`: 0..1000 (controls partial/harmonics mix; internal mapping: `currentHarmonicMod = 1 - value/1000`).

## Audio Inputs / Outputs
- Inputs are mapped by the host:
  - `Lin/TZFM FM input`: scaled to Q1.15 internally (code divides by `/5.0f` before conversion).
  - `V/8 input`: float audio used as V/Oct (smoothed then converted to frequency).
  - `Morph input`: per-sample morph amount (converted to Q1.15).
  - `Harmonics input`: per-sample harmonic modulation.
- Output: mixed signal of all oscillators, divided by `NUM_OSC` and scaled by `Gain`.

## Detune Models (Tuning Model enum)
- `0`: Tri bell
- `1`: TR-808 partial set
- `2`: Hammond drawbar-style partials
- `3`: Piano-like partials
- `4`: Bell
- `5`: Marimba
- `6`: Bass drum
- `7`: Harmonics series
- How they work: Each model has a table of `ratio`, `amplitude`, `decayTime` for up to 9 partials. `Detune` scales partial ratios around the fundamental.

## Harmonics Control
- `Harmonics` parameter is combined with optional `Harmonics input`.
- Effect: changes per-oscillator amplitude/partial weighting; useful to morph between tonal and inharmonic textures.

## Implementation Notes (affects user behaviour)
- Polarity & scaling:
  - Morph and FM inputs are scaled by `/5.0f` and converted to Q1.15.
  - FM depth is applied in a Q16 fixed-point code path for block processing.
- Amplitude smoothing:
  - The algorithm applies a one-pole smoother to per-oscillator amplitudes to avoid clicks.

## Performance & Behavior Tips
- CPU: Morphing and fixed-point block math are lightweight. Avoid extreme FM depths with heavy detune to reduce aliasing.
- Anti-click: If clicks occur (e.g. near certain harmonic values), lower `ampSmoothAlpha` (smoother) or low-pass the `Harmonics input`.
- V/Oct stability: V/Oct path uses a 1-pole smoother; reduce abrupt CV steps for a stable pitch response.
- TZFM: Through-zero FM is supported; negative instantaneous frequencies invert waveform sign (handled per sample).


## Example Presets
- Vintage Organ: `Waveform = Morph`, `Tuning Model = Hammond`, `Detune = 2000`, `Morph = 300`, `Gain = 0.6`, `Harmonics = 800`.
- Bell-ish Pluck: `Waveform = Sine`, `Tuning Model = Bell`, `Detune = 6000`, `FM Depth = 1200`, `Harmonics = 300`.
- Marimba: `Waveform = Triangle`, `Tuning Model = Marimba`, `Detune = 4000`, `Morph = 100`, `Harmonics = 600`.
