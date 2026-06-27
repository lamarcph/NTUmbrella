# Cerberus

Cerberus is a triple-band saturation and texture effect for Disting NT.
It is designed to quickly move from clean glue to aggressive, animated character.

## What It Does

Cerberus splits audio into low, mid, and high bands, processes each band independently, then recombines and optionally filters the result.

Per band, you can shape:
- dynamics (`Press`)
- harmonic texture (`Grit`)
- crush/decimation
- ring modulation
- transient attack/sustain
- noise amount
- stereo width

On top of that, you get:
- a full-band filter with selectable model/mode and oversampling
- envelope follower routing into filter destinations and drive sensitivity
- CV control for filter frequency and ring frequency (with depth)

## Page Layout

1. Drive
2. Filter
3. Env Flwr
4. Crush
5. Ring/Width
6. Transient
7. Routing

## Quick Start

1. Set `In L`/`In R` and `Out L`/`Out R` on the Routing page.
2. Start with `Mix = 1000` and `Output = 1000`.
3. On Drive page, bring `Drive` to 300-600.
4. Add `Press Mid` around 200 for glue.
5. Add `Grit Mid` around 250 for presence.
6. Use Filter page for overall tone shaping.

## Core Controls

### Drive Page

- `Drive`: global saturation amount, mapped as `t × √t` (progressive — subtle below 40%, aggressive above 70%)
- `Press Lo/Mid/Hi`: per-band dynamics at a fixed -18 dBFS threshold
  - Positive: compress peaks (glue, tame)
  - Negative: expand below threshold (more contrast and snap)
- `Grit Lo/Mid/Hi`: per-band wave-fold after saturation
  - **Lo**: wave-fold on the saturated low band → dense sub harmonics
  - **Mid**: wave-fold on the asymmetric mid band → buzzy, aggressive texture
  - **Hi**: LP-filtered noise modulated by hi-band level → grainy shimmer (not a wave-fold)
- `Mix`: dry/wet blend (values between 0 and 1000 may introduce mild phase coloration)
- `Output`: final gain (1000 = unity)

### Filter Page

**Filter Model** — choose the filter topology:

| Model | Character |
|---|---|
| SVF | Clean 2-pole State Variable. Stable resonance. All modes available. |
| Ladder | 4-pole Moog-style. Warmth and self-oscillation. LP4 is the signature mode. |
| MS20 | Korg-style Sallen-Key with feedback saturation. Screams at high resonance. |
| Diode | TB-303 diode ladder. Asymmetric clipping, squelchy acid character. |

**Filter Mode** — output shape:

| Mode | Description |
|---|---|
| LP 2-pole | Low-pass, 12 dB/oct |
| LP 4-pole | Low-pass, 24 dB/oct |
| HP 2-pole | High-pass, 12 dB/oct |
| BP 2-pole | Band-pass, 12 dB/oct |
| Notch | Band-reject |
| HP+LP | High-pass into low-pass (band shell). SVF only; Ladder falls back to LP4. |
| Bypass | No filtering |

- `Filter Cutoff`: exponential response — 20 Hz at 0, 20 kHz at 10000 (raw 5000 ≈ 632 Hz)
- `Filter Res`: resonance, 0..1000
- `Filter Drive`: 1.0x to 10.0x — adds filter saturation character
- `Filter OS`: 1x / 2x / 4x oversampling (reduces aliasing, increases CPU)
- `CV Flt Depth`: CV depth in oct/V (1000 = 1 V/oct); on Filter page

### Env Flwr Page

Source controls (shape the follower that feeds all modulation destinations):
- `Env Sens`: how much transient energy pushes Drive toward maximum
- `Env Attack` (0..500 ms): follower rise time
- `Env Release` (1..2000 ms): follower fall time
- `Env Shape` (-1000..1000): response curve of the follower ADSR

Destination controls:
- `Flt Cutoff Env`: opens filter upward with transients (multiplicative, up to 4 octaves at max)
- `Flt Drive Env`: increases filter drive with transients (+9 range additively)
- `Flt Res Env`: pushes resonance with transients (additive, clamped below 1.0)

### Crush Page

- `Crush Lo/Mid/Hi` (1..16 bits): bit depth reduction. 16 = off. 8 = classic lo-fi. 4–6 = 909 character.
- `Decim Lo/Mid/Hi` (1..64): integer sample-rate reduction. 1 = off. Aliasing foldback increases with higher values.
- `Noise Lo/Mid/Hi` (0..1000): additive noise blended into each band after processing
- `Noise Color`: White (flat), Pink (−3 dB/oct), Lo-Fi (pink through 1 kHz LP → rumble/smear)

### Transient Page

- `Trs Atk Lo/Mid/Hi`: gain during the transient attack spike (positive = more punch, negative = softer)
- `Trs Sus Lo/Mid/Hi`: gain during the sustained body (positive = longer tail, negative = tighter gate)

- `Ring Freq` (20..10000 Hz): carrier frequency shared across all three bands
- `Ring Lo/Mid/Hi` (0..1000): per-band ring mod depth (1000 = full multiplication)
- `CV Ring Depth`: CV depth in oct/V (1000 = 1 V/oct); on this page
- `Width Lo/Mid/Hi` (0..2000): per-band M/S width (1000 = unity, 0 = mono, 2000 = hyper-wide)

### Routing Page

- audio input/output bus selection
- output mode per channel (Add/Replace)
- CV bus selectors:
  - `CV Flt Freq`
  - `CV Ring Freq`

## Example Starting Presets

### Clean Glue

- Drive: 300
- Press Lo/Mid/Hi: 200 / 150 / 100
- Grit Lo/Mid/Hi: 100 / 120 / 80
- Mix: 700
- Filter Mode: Bypass

### Punchy Drums

- Drive: 500
- Press Mid: 250
- Trs Atk Mid: 350
- Trs Sus Mid: -200
- Filter: SVF LP4, cutoff around 6k (raw ~8200)
- Mix: 1000

### Lo-Fi Hats

- Crush Hi: 6..8 bits
- Decim Hi: 2..4
- Noise Hi: 20..80, Color: Pink
- Ring Mid: 150, Ring Freq: 3000
- Width Hi: 1400

### Envelope Filter

- Filter: SVF LP4, Cutoff: 4500 (raw, ≈500 Hz)
- Filter Res: 300
- Env Attack: 0, Env Release: 150
- Flt Cutoff Env: 800
- Mix: 1000

## CV Tips

- Patch a modulation source into `CV Flt Freq` and set `CV Flt Depth` to around 1000 for 1V/oct behavior.
- Patch a slow LFO into `CV Ring Freq` with `CV Ring Depth` around 1500-2500 for metallic movement.
- Negative depth values invert modulation direction.

## Build

From the Cerberus folder:

```bash
make
```

Build tests:

```bash
make test
```

Run tests:

```bash
make test-run
```

## Testing And Golden Hashes

Integration WAV tests live in `tests/test_integration.cpp`.
Golden hashes are in `tests/golden_hashes.txt`.

To regenerate selected hashes, set a hash entry to `*` and run `make test-run`.
