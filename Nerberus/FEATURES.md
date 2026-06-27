# Nerberus Features

Nerberus is a triple-band character processor for Disting NT, focused on punch, texture, and movement.
This document describes the current implementation in `Nerberus.cpp`.

## High-Level Signal Flow

1. Input routing (`In L`, `In R`)
2. 3-band split (LR4 crossover at `XO Lo Hz` and `XO Hi Hz`)
3. Per-band crush/decimation
4. Core engine (Drive + Press + per-band Grit)
5. Per-band stages: Ring Mod -> Transient -> Noise -> Width
6. Band recombine to stereo wet signal
7. Full-band filter (optional, with oversampling)
8. Dry/Wet mix and output gain
9. Output routing (`Out L/R` with Add/Replace modes)

## Pages And Controls

### Drive Page

- `XO Lo Hz` (20..2000 Hz): low/mid crossover frequency
- `XO Hi Hz` (200..8000 Hz): mid/high crossover frequency
- `Drive` (0..1000): mapped as `t × √t` where `t = v/1000`. Progressive response — subtle below 400, increasingly aggressive above 700.
- `Press Lo/Mid/Hi` (-1000..1000): per-band dynamics tilt, threshold fixed at -18 dBFS
  - Positive → compresses peaks above threshold (ratio up to ~8:1 at +1000)
  - Negative → expands material below threshold (upward expansion)
- `Grit Lo/Mid/Hi` (0..1000): per-band harmonic texture via wave-fold after saturation
  - **Lo Grit**: wave-fold on the saturated low band; fold threshold narrows from 1.0 to 0.4 with increasing grit → dense sub harmonics
  - **Mid Grit**: wave-fold on the asymmetric-saturated mid band; fold threshold narrows from 0.95 to 0.35 → buzzy, dense mid texture
  - **Hi Grit**: LP-filtered noise (LP at ~800 Hz) modulated by the hi-band envelope → grainy shimmer, not wave-fold
- `Mix` (0..1000): dry/wet blend. Values between 0 and 1000 may introduce phase coloration due to IIR crossover phase rotation.
- `Output` (0..2000): final output gain (1000 = unity, 2000 = +6 dB)

### Filter Page

**Filter Model** — topology of the ZDF filter:

| Value | Name | Character |
|---|---|---|
| 0 | SVF | 2-pole TPT State Variable. Clean and stable at all resonance levels. Supports all 7 modes. |
| 1 | Ladder | 4-pole TPT Moog-style. LP4 is the signature mode. Other modes derived from tap mixing. Input saturation. |
| 2 | MS20 | Korg MS-20 style Sallen-Key with saturated resonance feedback. Screaming, harsh at high resonance. |
| 3 | Diode | TB-303 inspired 4-pole diode ladder. Asymmetric per-stage clipping preserves bass. Squelchy acid character. |

**Filter Mode** — topology output shape (availability depends on model):

| Value | Name | Description |
|---|---|---|
| 0 | LP 2-pole | 12 dB/oct low-pass |
| 1 | LP 4-pole | 24 dB/oct low-pass |
| 2 | HP 2-pole | 12 dB/oct high-pass |
| 3 | BP 2-pole | 12 dB/oct band-pass |
| 4 | Notch | Band-reject (notch) |
| 5 | HP+LP | HP into LP (band-pass-like shell, SVF only; falls back to LP4 on Ladder) |
| 6 | Bypass | No filtering — ZDF state is not advanced |

- `Filter Cutoff` (0..10000): exponential mapping `20 × 1000^(raw/10000)` → 20 Hz at 0, 20 kHz at 10000
- `Filter Res` (0..1000): mapped to `0.0..0.999`
- `Filter Drive` (1000..10000): mapped to `1.0..10.0` — at 1.0 the filter is transparent; higher values add warmth and harmonic saturation
- `Filter OS` (1x, 2x, 4x): oversampling reduces aliasing from filter saturation at the cost of CPU
- `CV Flt Depth` (-10000..10000): filter cutoff CV depth in octaves/volt (`v/1000`); default 1.0 = 1 V/oct

### Env Flwr Page

Envelope follower source controls:
- `Env Sens` (0..1000): modulates global Drive amount
- `Env Attack` (0..500 ms)
- `Env Release` (1..2000 ms)
- `Env Shape` (-1000..1000)

Envelope follower destination controls:
- `Flt Cutoff Env` (-1000..1000): modulates filter cutoff around base
- `Flt Drive Env` (-1000..1000): modulates filter drive around base
- `Flt Res Env` (-1000..1000): modulates filter resonance around base

### Crush Page

- `Crush Lo/Mid/Hi` (1..16 bits): bit depth applied before the engine; 16 = bypass, 8 = classic lo-fi, 4–6 = 909/808-era texture
- `Decim Lo/Mid/Hi` (1..64): integer sample-rate reduction (sample-and-hold); 1 = bypass, higher = more aliasing foldback
- `Noise Lo/Mid/Hi` (0..1000): additive noise level after per-band processing, before recombine
- `Noise Color` (White / Pink / Lo-Fi):
  - **White**: flat spectrum PRNG noise
  - **Pink**: white noise shaped through a 3-coefficient Paul Kellet approximation (−3 dB/oct)
  - **Lo-Fi**: pink noise through a 1-pole LP at ~1 kHz, adding rumble and smear

### Ring/Width Page

- `Ring Freq` (20..10000 Hz): carrier frequency for the ring modulator (shared across all three bands)
- `Ring Lo/Mid/Hi` (0..1000): per-band ring mod wet amount. At 1000 the band signal is fully multiplied by the carrier (complete ring mod). At 500, equal blend of dry and ring-modulated signal.
- `CV Ring Depth` (-10000..10000): ring carrier frequency CV depth in octaves/volt (`v/1000`); default 1.0 = 1 V/oct
- `Width Lo/Mid/Hi` (0..2000): per-band M/S width
  - 0 = mono (side component fully removed)
  - 1000 = unity (natural stereo, no change)
  - 2000 = hyper-wide (side component doubled)

### Transient Page

- `Trs Atk Lo/Mid/Hi` (-1000..1000): gain applied during the transient spike (fast env > slow env)
  - Positive → boosts attack punch
  - Negative → softens initial click
- `Trs Sus Lo/Mid/Hi` (-1000..1000): gain applied during the sustained body (fast env < slow env)
  - Positive → lengthens tail
  - Negative → tightens/gates the body

Transient detection uses two `ShapedADSR` followers per band (fast ~0.1 ms / slow ~5 ms). The difference `fast − slow` isolates the attack front; its complement `slow − fast` is the body.

### Routing Page

- `In L`, `In R`
- `Out L`, `Out L Mode`
- `Out R`, `Out R Mode`
- `CV Flt Freq`: bus selector for filter frequency CV
- `CV Ring Freq`: bus selector for ring frequency CV

## Modulation Details

### Envelope Follower

A single `ShapedADSR` follower tracks the peak of the full input signal. Its output `envDrive` is normalised to 0..1.

**Env Sens → Drive** (applied before the engine):
```
effectiveDrive = clamp(baseDrive + envDrive × EnvSens × (1 − baseDrive), 0..1)
```
This pushes Drive toward 1.0 proportionally to transient energy.

**Filter destinations** (applied at the filter stage, after band recombine):

| Destination | Formula | Clamp |
|---|---|---|
| Flt Cutoff Env | `cutoff × 2^(envDrive × amount × 4)` | 20..20000 Hz |
| Flt Drive Env | `baseDrive + envDrive × amount × 9` | 1.0..10.0 |
| Flt Res Env | `baseRes + envDrive × amount` | 0.0..0.999 |

Env follower ballistics (`Env Attack` / `Env Release` / `Env Shape`) are exposed on the Env Flwr page and affect only the `envDriveFollower` ADSR — not the per-band transient followers.

### CV Modulation

CV inputs are block-averaged, then applied in octave domain:

- Filter frequency:
  - `cutoff = cutoffEnvMod * 2^(cvFilt * cvFilterDepth)`
- Ring frequency:
  - `ringFreq = baseRingFreq * 2^(cvRing * cvRingDepth)`

Both are clamped to valid operating ranges.

## Notes On Behavior

- Dry/wet blending is post-processing. Intermediate mix values can create audible phase coloration due to dry-vs-processed phase differences.
- `Filter Cutoff` uses an exponential control law; small knob changes at low values are musically fine-grained.
- `Filter Drive` minimum is 1.0x by design.
- Global `Grit` has been removed; only per-band grit remains.

## Test Coverage Snapshot

Integration tests render WAVs and verify hashes, including:

- Core showcases (dry, full stack, filter sweep, spunk chain, transient, ring, classic)
- CV modulation showcases (filter freq, ring freq)
- Envelope destination showcases:
  - Drive via `Env Sens`
  - `Flt Cutoff Env`
  - `Flt Drive Env`
  - `Flt Res Env`

Hashes are tracked in `tests/golden_hashes.txt`.
