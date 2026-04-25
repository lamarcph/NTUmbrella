# NoiseBouquet

A fork of [Befaco's **Noise Plethora**](https://library.vcvrack.com/Befaco/NoisePlethora)
(VCV Rack edition) ported to the [Expert Sleepers Disting NT](https://www.expert-sleepers.co.uk/distingNT.html)
module format.

This is an unaffiliated, community port. The 33 noise/cluster/effects
algorithms from the original Teensy hardware module — and its VCV Rack
adaptation — are bundled into a single Disting NT plugin with a
Bank/Program selector in the parameter list.

The name was changed from "Noise Plethora" to **NoiseBouquet** to avoid
brand confusion: this plugin is a different product on a different
platform from Befaco's, even though it shares the underlying DSP code.

## Status

Working on Disting NT firmware 1.15. Loads, switches programs without
hangs, runs all algorithms.

## Build

ARM toolchain (`arm-none-eabi-c++` 10+ recommended):

```
make
```

Produces `plugins/NoiseBouquet.o` — copy to the SD card under
`programs/plug-ins/`.

## Tests (host)

```
make test
make test-run
```

Builds with the system `g++`; uses the shared `test_harness/` framework
to exercise the plugin offline against the API stub.

## Layout

```
NoiseBouquet.cpp              — _NT_factory glue (parameters, step, draw)
nt_rack_shim.cpp/.hpp         — VCV Rack API shim (stateless; reads NT_globals)
include/PluginRegistry.hpp    — bank/program → algorithm table
include/rack.hpp              — minimal rack::dsp::RingBuffer
algos/                        — P_*.hpp  one file per algorithm (vendored from Befaco)
algos/NoisePlethoraPlugin.hpp — NT-adapted base class
teensy/                       — vendored Teensy Audio Library blocks
tests/                        — host-side integration tests
PLAN.md                       — design notes / port milestones
```

## Banks

| Bank | Programs | Theme                          |
|------|----------|--------------------------------|
| 1    | 1–10     | Heavy effects, ring modulation |
| 2    | 1–10     | Cluster synthesis              |
| 3    | 1–10     | Effects-using algorithms       |
| 4    | 1–3      | Test / sanity (TestPlugin, WhiteNoise, TeensyAlt) |

Empty slots render as `--` and produce silence.

## Licensing

GPL-3.0. See [LICENSE](LICENSE) and [NOTICE](NOTICE) for full
attribution to upstream authors.

## Acknowledgements

- **Befaco** and **Norman Goldwasser** — Noise Plethora algorithms
  (original Teensy hardware + VCV Rack adaptation).
- **Paul Stoffregen / PJRC** — Teensy Audio Library (MIT).
- **Andrew Belt / VCV** — VCV Rack 2 API design (this project re-implements
  the small subset we use; no VCV code is included).
- **Expert Sleepers** — Disting NT platform and SDK.

"Noise Plethora" is a trademark of Befaco. "Disting" and "Disting NT"
are products of Expert Sleepers Ltd. NoiseBouquet is not endorsed by
or affiliated with either.
