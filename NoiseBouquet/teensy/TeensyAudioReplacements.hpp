// Umbrella include used by NoisePlethoraPlugin.hpp.
//
// In the original VCV port this pulled in every Teensy unit. For the NT port
// each milestone enables only the units it needs; the rest stay commented out
// until they're vendored.
//
// M1: WhiteNoise only.
// M2a: + synth_waveform (TestPlugin, Bank-4 completion),
//      + filter_variable (TeensyAlt, resonoise, etc.).
// M2b: + mixer (Bank-2 cluster algos).
// M3: + effect_bitcrusher / effect_combine / effect_multiply / effect_wavefolder.
// M4: + effect_freeverb / synth_pwm / synth_pinknoise for Bank 3.
// M5: + effect_flange / effect_granular for Bank 1.
#pragma once

#include <rack.hpp>            // -> NoiseBouquet/include/rack.hpp -> nt_rack_shim.hpp

#include "audio_core.hpp"
#include "synth_dc.hpp"
#include "synth_pinknoise.hpp"
#include "synth_sine.hpp"
#include "synth_whitenoise.hpp"
#include "synth_pwm.hpp"
#include "synth_waveform.hpp"
#include "filter_variable.hpp"
#include "mixer.hpp"
#include "effect_bitcrusher.h"
#include "effect_combine.hpp"
#include "effect_flange.h"
#include "effect_freeverb.hpp"
#include "effect_granular.hpp"
#include "effect_multiply.h"
#include "effect_wavefolder.hpp"

// --- M3/M4/M5 (uncomment as units are vendored) ---