# NTUmbrella

A collection of synthesizer and effect algorithms for the
[Expert Sleepers disting NT](https://www.expert-sleepers.co.uk/distingNT.html)
Eurorack module, built against the
[disting NT C++ API](https://github.com/expertsleepersltd/distingNT_API).

## Projects

| Folder | Description |
|--------|-------------|
| **[PolyLofi](PolyLofi/)** | Polyphonic synthesizer — up to 12 voices, 3 oscillators, 4 filter models, 3 envelopes, 3 LFOs, modulation matrix, per-voice delay, and a built-in preset system. |
| **[Oneiroi](Oneiroi/)** | Port of the Oneiroi ambient texture processor — oscillators, looper, filter, resonator, echo, and ambience in a single algorithm. |
| **[LofiOsc](LofiOsc/)** | 9 detunable lo-fi oscillators with selectable waveforms, instrument-style detune models, V/Oct, FM, and morph modulation. Aliasing is intentional. |
| **[LofiParts](LofiParts/)** | Shared DSP library — ZDF filters, envelopes, LFOs, oscillators, delay, voice allocation, wavetable generation, and math utilities used across the above plugins. |
| **[test_harness](test_harness/)** | Headless test infrastructure — API stubs, plugin harness, test framework, and WAV writer for running automated tests on a host PC without disting NT hardware. |

## Building

### Prerequisites

- **ARM cross-compiler:** `arm-none-eabi-g++` (for disting NT `.o` plugins)
- **Host C++ compiler:** `g++` with C++17 support (for tests)
- The `distingNT_API` submodule (included in this repo)

### Plugin (ARM)

```bash
cd PolyLofi   # or Oneiroi, LofiOsc
make
```

This produces a `.o` file in the `plugins/` subfolder. Copy it to the
disting NT SD card.

### Tests (host)

```bash
cd PolyLofi
make test       # compile
make test-run   # compile and run
```

## License

This project is licensed under the **GNU General Public License v3.0** —
see [LICENSE](LICENSE) for details.

The [disting NT API](distingNT_API/) is copyright Expert Sleepers Ltd and
licensed under the MIT License.
