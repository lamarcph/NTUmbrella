# NTUmbrella
NTUmbrella is a project to port the Befaco Oneiroi to the Disting NT.

## What Oneiroi does
Oneiroi is a single‑voice creative audio processor combining two oscillators (sine / supersaw / wavetable), a looper (sound‑on‑sound / resampling), filter, resonator, echo and ambience. It accepts audio and CV inputs and outputs stereo audio to the host channels you choose.

## Signal flow (simple)
Inputs → Input Level → Oscillators & Looper → Filter → Resonator → Echo → Ambience → Limiter → Output Level → Outputs

## I/O summary
- Left / Right Audio Inputs — route external audio into the processing chain.
  - Important: BOTH Left AND Right inputs must be assigned (non‑zero) for external input to be used. If either input is not set, the internal chain (oscillators/looper/etc.) is used instead.
- Left / Right Audio Outputs — choose host output channels; each output can be set to Replace (overwrite) or Add (mix).
- Clock Input (CV / audio) — external tempo pulses (rising edge detection recommended).
- Pitch Input (CV) — volt/octave pitch control for oscillators.
- Key controls (grouped by page)
- Oscillators: Semi, Fine, Volt/Octave, Detune, Unison, Sine Vol, SS/WT Vol, SS/WT Switch (SuperSaw / WaveTable)
- Filter: Mode (Low/Band/High/Comb), Cutoff, Cutoff Mod, Resonance, Res Mod, Position (routing), Vol
- Looper: Vol, Sound‑on‑Sound, DJ Filter, Speed, Start, Length, Recording, Resampling
- Resonator / Echo / Ambience: volumes and behaviour (tune/density/decay)
- Modulation: Type, Speed, Level
- Routing: Input/Output selection, Clock/Pitch input assignment, Input Level, Output Level

## Defaults:

- Input Level: 70% (700)
- Output Level: 70% (700)
- Sine / SS Vol: 75% (750)

## Important behavior notes
Muted sections do not consume CPU: if a major section (oscillators, looper, resonator, echo, ambience, filter) is effectively muted/off, Oneiroi avoids running that processing—this reduces CPU use. Muting is detected from volumes/flags; use these controls to save CPU on resource‑limited systems.

Inputs require both channels: external audio is only read and forwarded when both Left AND Right inputs are set. This avoids partial/mono routing mistakes and simplifies processing.

DJ filter behaviour: the DJ filter center is at 0.550 (dry). Values below 0.550 progressively apply a low‑pass effect; values above 0.550 progressively apply a high‑pass effect.

## Quick start
1. Route outputs to host channels and set Replace/Add mode.
1. Choose sound source:
- External audio: set BOTH Left and Right Input assignments and set Input Level.
- Internal: enable oscillators (Sine or SS/WT) and set their volumes.
1. Tempo / sync:
- For external sync patch a clock pulse to Clock Input.
- For pitch CV patch a volt/octave source to Pitch Input.
1. Record loops: enable Recording on the Looper page; use Sound‑on‑Sound or Resampling for overdub workflows.
1. Shape tone: use Filter (Mode, Cutoff, Resonance) and position to place the filter in the chain.

## Clock & Pitch CV tips
- Clock Input expects pulse/gate signals. Prefer clean pulses (logic gates, triggers) or precondition the signal with a Schmitt trigger. Short pulses shorter than the audio block should be widened or use block‑scan edge detection in the host patch.
- Pitch Input is volt/octave: use Semi/Fine/Volt‑Octave controls for offset and calibration.

## Looper basics
- Recording ON captures incoming audio into the loop buffer.
- Sound‑on‑Sound enables overdubbing without erasing the loop.
- Resampling mode lets you resample and layer creative textures.
- Use Speed / Start / Length to control loop playback and position.

## Troubleshooting
### No audio:
- Verify Left/Right Output assignments and Output Mode.-
- If using external input, ensure BOTH Left and Right inputs are assigned.
- Check Input Level, Output Level, and oscillator volumes.

### Clock not syncing:
- Make sure Clock Input is assigned and pulse amplitude crosses threshold.
- Use wider or cleaner pulses or pre‑condition the signal.

### DJ filter sounds odd:
  DJ Filter : 0.550 = dry. Move lower for LPF, higher for HPF.
### Recipes
- Ambient pad: SS/WT = Wavetable, raise Ambience & Echo, low filter cutoff, gentle modulation.
- Rhythmic loopbed: feed percussion to inputs, enable Looper Recording, sync Clock Input, use resampling for variation.
- Metallic resonances: boost Resonator Vol and tune, add echo density.
