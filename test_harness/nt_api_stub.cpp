// =============================================================================
// nt_api_stub.cpp — Minimal headless stub for disting NT API symbols
// =============================================================================
// Provides all extern "C" symbols that the NT API header declares so that
// plugins can be compiled and linked natively (g++ / clang++) without the
// real disting NT firmware.
//
// Intended for headless integration testing — no GUI, no audio I/O.
// Reusable across every plugin in the NTUmbrella monorepo.
// =============================================================================

#include <cstddef>
#include <distingnt/api.h>
#include <distingnt/wav.h>
#include <distingnt/microtuning.h>
#define _DISTINGNT_SERIALISATION_INTERNAL
#include <distingnt/serialisation.h>
#undef _DISTINGNT_SERIALISATION_INTERNAL
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// NT_globals — the one piece of global state every plugin reads
// ---------------------------------------------------------------------------

static float s_workBuffer[4096];

// The API header declares:  extern const _NT_globals NT_globals;
// The compiler will place a plain `const` global in .rodata, making it
// immutable at runtime.  To allow tests to reconfigure sample rate we use
// a mutable backing struct, and expose the const reference via a pointer
// alias.  The linker resolves NT_globals to our mutable storage because
// we use __attribute__((section)) to keep it in writable memory.
//
// On platforms without section attributes (MSVC), the simpler const_cast
// approach works because MSVC puts extern const in .data by default.

#if defined(__GNUC__) || defined(__clang__)
  // Place in .data (writable) instead of .rodata
  extern "C" __attribute__((section(".data")))
  const _NT_globals NT_globals = {
      96000, 128, s_workBuffer, sizeof(s_workBuffer), 0, 0
  };
#else
  extern "C"
  const _NT_globals NT_globals = {
      96000, 128, s_workBuffer, sizeof(s_workBuffer), 0, 0
  };
#endif

namespace NtTestHarness {
    // Safe to mutate because the section attribute guarantees writable memory.
    static _NT_globals& mutableGlobals() { return const_cast<_NT_globals&>(NT_globals); }

    void setSampleRate(uint32_t sr)     { mutableGlobals().sampleRate = sr; }
    void setMaxFrames(uint32_t frames)  { mutableGlobals().maxFramesPerStep = frames; }
    // Use volatile read to defeat const-folding — setSampleRate() mutates
    // the .data copy at runtime but the compiler may cache the initial value.
    uint32_t getSampleRate()            { return (*(volatile const uint32_t*)&NT_globals.sampleRate); }
    uint32_t getMaxFrames()             { return (*(volatile const uint32_t*)&NT_globals.maxFramesPerStep); }

    static void (*s_setParameterCallback)(uint32_t algIdx, uint32_t param, int16_t value) = nullptr;
    void setSetParameterCallback(void (*cb)(uint32_t, uint32_t, int16_t)) {
        s_setParameterCallback = cb;
    }
}

// ---------------------------------------------------------------------------
// Screen buffer
// ---------------------------------------------------------------------------
extern "C" {
    uint8_t NT_screen[128 * 64] = {};
}

// ---------------------------------------------------------------------------
// Drawing — software renderer for screen snapshot testing
// ---------------------------------------------------------------------------

// PixelMix 5×7 proportional bitmap font (ASCII 32–126, LSB-first bit order)
static const unsigned char s_pixelMixFont[95][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // SP
    {0x04,0x04,0x04,0x04,0x00,0x04,0x00}, // !
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, // "
    {0x0A,0x1F,0x0A,0x1F,0x0A,0x00,0x00}, // #
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, // $
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, // %
    {0x0C,0x12,0x14,0x08,0x15,0x12,0x0D}, // &
    {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, // '
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, // (
    {0x08,0x04,0x02,0x02,0x02,0x04,0x08}, // )
    {0x00,0x04,0x15,0x0E,0x15,0x04,0x00}, // *
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // +
    {0x00,0x00,0x00,0x00,0x04,0x04,0x08}, // ,
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // -
    {0x00,0x00,0x00,0x00,0x00,0x04,0x00}, // .
    {0x00,0x01,0x02,0x04,0x08,0x10,0x00}, // /
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, // 0
    {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E}, // 1
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, // 2
    {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E}, // 3
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, // 4
    {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E}, // 5
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, // 6
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x08}, // 7
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, // 8
    {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C}, // 9
    {0x00,0x00,0x04,0x00,0x00,0x04,0x00}, // :
    {0x00,0x00,0x04,0x00,0x04,0x04,0x08}, // ;
    {0x02,0x04,0x08,0x10,0x08,0x04,0x02}, // <
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // =
    {0x08,0x04,0x02,0x01,0x02,0x04,0x08}, // >
    {0x0E,0x11,0x01,0x02,0x04,0x00,0x04}, // ?
    {0x0E,0x11,0x01,0x0D,0x15,0x15,0x0E}, // @
    {0x0E,0x11,0x11,0x11,0x1F,0x11,0x11}, // A
    {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E}, // B
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, // C
    {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C}, // D
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, // E
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10}, // F
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, // G
    {0x11,0x11,0x11,0x1F,0x11,0x11,0x11}, // H
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, // I
    {0x07,0x02,0x02,0x02,0x02,0x12,0x0C}, // J
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
    {0x10,0x10,0x10,0x10,0x10,0x10,0x1F}, // L
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, // M
    {0x11,0x11,0x19,0x15,0x13,0x11,0x11}, // N
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, // O
    {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10}, // P
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, // Q
    {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11}, // R
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, // S
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x04}, // T
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, // U
    {0x11,0x11,0x11,0x11,0x11,0x0A,0x04}, // V
    {0x11,0x11,0x11,0x15,0x15,0x1B,0x11}, // W
    {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11}, // X
    {0x11,0x11,0x11,0x0A,0x04,0x04,0x04}, // Y
    {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F}, // Z
    {0x0E,0x08,0x08,0x08,0x08,0x08,0x0E}, // [
    {0x00,0x10,0x08,0x04,0x02,0x01,0x00}, // backslash
    {0x0E,0x02,0x02,0x02,0x02,0x02,0x0E}, // ]
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, // ^
    {0x00,0x00,0x00,0x00,0x00,0x00,0x1F}, // _
    {0x08,0x04,0x02,0x00,0x00,0x00,0x00}, // `
    {0x00,0x00,0x0E,0x01,0x0F,0x11,0x0F}, // a
    {0x10,0x10,0x16,0x19,0x11,0x11,0x1E}, // b
    {0x00,0x00,0x0E,0x10,0x10,0x11,0x0E}, // c
    {0x01,0x01,0x0D,0x13,0x11,0x11,0x0F}, // d
    {0x00,0x00,0x0E,0x11,0x1F,0x10,0x0E}, // e
    {0x06,0x09,0x08,0x1C,0x08,0x08,0x08}, // f
    {0x00,0x00,0x0F,0x11,0x0F,0x01,0x0E}, // g
    {0x10,0x10,0x16,0x19,0x11,0x11,0x11}, // h
    {0x04,0x00,0x0C,0x04,0x04,0x04,0x0E}, // i
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, // j
    {0x10,0x10,0x12,0x14,0x18,0x14,0x12}, // k
    {0x0C,0x04,0x04,0x04,0x04,0x04,0x0E}, // l
    {0x00,0x00,0x1A,0x15,0x15,0x11,0x11}, // m
    {0x00,0x00,0x16,0x19,0x11,0x11,0x11}, // n
    {0x00,0x00,0x0E,0x11,0x11,0x11,0x0E}, // o
    {0x00,0x00,0x1E,0x11,0x1E,0x10,0x10}, // p
    {0x00,0x00,0x0D,0x13,0x0F,0x01,0x01}, // q
    {0x00,0x00,0x16,0x19,0x10,0x10,0x10}, // r
    {0x00,0x00,0x0E,0x10,0x0E,0x01,0x1E}, // s
    {0x08,0x08,0x1C,0x08,0x08,0x09,0x06}, // t
    {0x00,0x00,0x11,0x11,0x11,0x13,0x0D}, // u
    {0x00,0x00,0x11,0x11,0x11,0x0A,0x04}, // v
    {0x00,0x00,0x11,0x11,0x15,0x15,0x0A}, // w
    {0x00,0x00,0x11,0x0A,0x04,0x0A,0x11}, // x
    {0x00,0x00,0x11,0x11,0x0F,0x01,0x0E}, // y
    {0x00,0x00,0x1F,0x02,0x04,0x08,0x1F}, // z
    {0x02,0x04,0x04,0x08,0x04,0x04,0x02}, // {
    {0x04,0x04,0x04,0x04,0x04,0x04,0x04}, // |
    {0x08,0x04,0x04,0x02,0x04,0x04,0x08}, // }
    {0x00,0x00,0x08,0x15,0x02,0x00,0x00}, // ~
};
static const unsigned char s_pixelMixWidths[95] = {
    // Glyph data for several narrow chars is encoded centered in 5 columns
    // (bits 1..3), so they must be rendered at width 5 even though they
    // would otherwise be drawn proportionally narrower. Affected: 1, I, [, ].
    3,1,3,5,5,5,5,1,3,3,5,5,2,5,1,5,
    5,5,5,5,5,5,5,5,5,5,1,2,3,5,3,5,
    5,5,5,5,5,5,5,5,5,5,4,5,5,5,5,5,
    5,5,5,5,5,5,5,5,5,5,5,3,5,3,5,5,
    3,5,5,5,5,5,5,5,5,3,3,5,3,5,5,5,
    5,5,4,5,4,5,5,5,5,5,5,3,1,3,5
};

// Internal helpers
static inline void s_setPixel(int x, int y, int colour) {
    if (x < 0 || x >= 256 || y < 0 || y >= 64) return;
    uint8_t c = (uint8_t)(colour & 0x0F);
    int idx = y * 128 + x / 2;
    if (x & 1)
        NT_screen[idx] = (NT_screen[idx] & 0xF0) | c;
    else
        NT_screen[idx] = (NT_screen[idx] & 0x0F) | (uint8_t)(c << 4);
}

static void s_drawLine(int x0, int y0, int x1, int y1, int colour) {
    int dx = abs(x1 - x0), dy = abs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy;
    while (true) {
        s_setPixel(x0, y0, colour);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 <  dx) { err += dx; y0 += sy; }
    }
}

extern "C" {
    void NT_drawShapeI(_NT_shape shape, int x0, int y0, int x1, int y1, int colour) {
        switch (shape) {
            case kNT_point:
                s_setPixel(x0, y0, colour);
                break;
            case kNT_line:
                s_drawLine(x0, y0, x1, y1, colour);
                break;
            case kNT_box:
                s_drawLine(x0, y0, x1, y0, colour);
                s_drawLine(x1, y0, x1, y1, colour);
                s_drawLine(x1, y1, x0, y1, colour);
                s_drawLine(x0, y1, x0, y0, colour);
                break;
            case kNT_rectangle: {
                int mnX = x0 < x1 ? x0 : x1, mxX = x0 > x1 ? x0 : x1;
                int mnY = y0 < y1 ? y0 : y1, mxY = y0 > y1 ? y0 : y1;
                for (int y = mnY; y <= mxY; ++y)
                    for (int x = mnX; x <= mxX; ++x)
                        s_setPixel(x, y, colour);
                break;
            }
            case kNT_circle: {
                int cx = (x0+x1)/2, cy = (y0+y1)/2;
                int rx = abs(x1-x0)/2, ry = abs(y1-y0)/2;
                int r = rx < ry ? rx : ry;
                int px = r, py = 0, e = 0;
                while (px >= py) {
                    s_setPixel(cx+px,cy+py,colour); s_setPixel(cx+py,cy+px,colour);
                    s_setPixel(cx-py,cy+px,colour); s_setPixel(cx-px,cy+py,colour);
                    s_setPixel(cx-px,cy-py,colour); s_setPixel(cx-py,cy-px,colour);
                    s_setPixel(cx+py,cy-px,colour); s_setPixel(cx+px,cy-py,colour);
                    if (e <= 0) { py++; e += 2*py+1; }
                    if (e > 0)  { px--; e -= 2*px+1; }
                }
                break;
            }
            default: s_setPixel(x0, y0, colour); break;
        }
    }

    void NT_drawShapeF(_NT_shape shape, float x0, float y0, float x1, float y1, float colour) {
        NT_drawShapeI(shape, (int)x0, (int)y0, (int)x1, (int)y1, (int)colour);
    }

    void NT_drawText(int x, int y, const char* str, int colour, _NT_textAlignment align, _NT_textSize) {
        if (!str) return;
        // Measure width for alignment
        int tw = 0;
        for (const char* p = str; *p; ++p) {
            int ci = *p - 32;
            if (ci < 0 || ci >= 95) { tw += 4; continue; }
            tw += s_pixelMixWidths[ci];
            if (*(p+1)) tw += 1; // spacing
        }
        int sx = x;
        if (align == kNT_textCentre) sx = x - tw/2;
        else if (align == kNT_textRight) sx = x - tw;
        // Draw glyphs
        int cx = sx;
        for (const char* p = str; *p; ++p) {
            int ci = *p - 32;
            if (ci >= 0 && ci < 95) {
                int cw = s_pixelMixWidths[ci];
                const unsigned char* glyph = s_pixelMixFont[ci];
                for (int row = 0; row < 7; ++row) {
                    unsigned char bits = glyph[row];
                    for (int col = 0; col < cw; ++col) {
                        if (bits & (1 << (cw - 1 - col)))
                            s_setPixel(cx + col, y - 7 + row, colour);
                    }
                }
                cx += cw;
            } else {
                cx += 4;
            }
            if (*(p+1)) cx += 1; // inter-character spacing
        }
    }

    void NT_getDisplayDimensions(unsigned int* w, unsigned int* h) { if (w) *w = 256; if (h) *h = 64; }

    int NT_getTextWidthUTF8(const char* text, _NT_textSize) {
        if (!text) return 0;
        int tw = 0;
        for (const char* p = text; *p; ++p) {
            int ci = *p - 32;
            if (ci < 0 || ci >= 95) { tw += 4; continue; }
            tw += s_pixelMixWidths[ci];
            if (*(p+1)) tw += 1;
        }
        return tw;
    }
}

// ---------------------------------------------------------------------------
// String formatting — simple implementations
// ---------------------------------------------------------------------------
extern "C" {
    int NT_intToString(char* buffer, int32_t value) {
        return sprintf(buffer, "%d", value);
    }
    int NT_floatToString(char* buffer, float value, int dp) {
        return sprintf(buffer, "%.*f", dp, (double)value);
    }
    int NT_strlenUTF8(const char* text) {
        return (int)strlen(text);
    }
}

// ---------------------------------------------------------------------------
// MIDI send — captured into a global buffer for tests to inspect.
// Each call appends one MidiEvent (status + 0..2 data bytes + destination).
// Tests call ntstub_midi_clear() before stimulating, then read ntstub_midi_log().
// ---------------------------------------------------------------------------
struct NtStubMidiEvent {
    uint32_t dest;
    uint8_t  status;
    uint8_t  data1;
    uint8_t  data2;
    uint8_t  len;   // 1, 2 or 3 — number of meaningful bytes (status counted)
};
static std::vector<NtStubMidiEvent> g_midiLog;

extern "C" {
    void NT_sendMidiByte(uint32_t dest, uint8_t b) {
        g_midiLog.push_back({dest, b, 0, 0, 1});
    }
    void NT_sendMidi2ByteMessage(uint32_t dest, uint8_t s, uint8_t d1) {
        g_midiLog.push_back({dest, s, d1, 0, 2});
    }
    void NT_sendMidi3ByteMessage(uint32_t dest, uint8_t s, uint8_t d1, uint8_t d2) {
        g_midiLog.push_back({dest, s, d1, d2, 3});
    }
    void NT_sendMidiSysEx(uint32_t, const uint8_t*, uint32_t, bool) {}
}

// Test-side accessors (declared in test_harness/plugin_harness.h)
void ntstub_midi_clear() { g_midiLog.clear(); }
const std::vector<NtStubMidiEvent>& ntstub_midi_log() { return g_midiLog; }

// ---------------------------------------------------------------------------
// Parameter management — stubs
// ---------------------------------------------------------------------------
extern "C" {
    void    NT_parameterChanged(unsigned int) {}
    float   NT_getParameterValueMapped(unsigned int) { return 0.0f; }
    float   NT_getParameterValueMappedNormalised(unsigned int) { return 0.0f; }
    void    NT_setParameterValueMapped(unsigned int, float) {}
    void    NT_setParameterValueMappedNormalised(unsigned int, float) {}
    void    NT_lockParameter(unsigned int) {}
    void    NT_unlockParameter(unsigned int) {}
    int     NT_parameterIsLocked(unsigned int) { return 0; }

    void    NT_setParameterRange(_NT_parameter* ptr, float init, float min, float max, float step) {
        // Minimal implementation matching the real one
        if (step >= 1.0f) {
            ptr->scaling = kNT_scalingNone;
            ptr->min = (int16_t)min;
            ptr->max = (int16_t)max;
            ptr->def = (int16_t)init;
        } else if (step >= 0.1f) {
            ptr->scaling = kNT_scaling10;
            ptr->min = (int16_t)(min * 10.0f);
            ptr->max = (int16_t)(max * 10.0f);
            ptr->def = (int16_t)(init * 10.0f);
        } else if (step >= 0.01f) {
            ptr->scaling = kNT_scaling100;
            ptr->min = (int16_t)(min * 100.0f);
            ptr->max = (int16_t)(max * 100.0f);
            ptr->def = (int16_t)(init * 100.0f);
        } else {
            ptr->scaling = kNT_scaling1000;
            ptr->min = (int16_t)(min * 1000.0f);
            ptr->max = (int16_t)(max * 1000.0f);
            ptr->def = (int16_t)(init * 1000.0f);
        }
    }

    void    NT_setParameterFromAudio(uint32_t algIdx, uint32_t param, int16_t value) {
        if (NtTestHarness::s_setParameterCallback)
            NtTestHarness::s_setParameterCallback(algIdx, param, value);
    }
    void    NT_setParameterFromUi(uint32_t algIdx, uint32_t param, int16_t value) {
        if (NtTestHarness::s_setParameterCallback)
            NtTestHarness::s_setParameterCallback(algIdx, param, value);
    }
    void    NT_setParameterGrayedOut(uint32_t, uint32_t, bool) {}
    uint32_t NT_parameterOffset(void) { return 0; }
    void    NT_updateParameterDefinition(uint32_t, uint32_t) {}
}

// ---------------------------------------------------------------------------
// Slot / algorithm query — stubs
// ---------------------------------------------------------------------------
extern "C" {
    int32_t  NT_algorithmIndex(const _NT_algorithm*) { return 0; }
    uint32_t NT_algorithmCount(void) { return 1; }
    bool     NT_getSlot(class _NT_slot&, uint32_t) { return false; }
}

// ---------------------------------------------------------------------------
// Misc
// ---------------------------------------------------------------------------
extern "C" {
    uint32_t NT_getCpuCycleCount(void) { return 0; }
    float    NT_getTemperatureC(void) { return 25.0f; }
    void     NT_copyFromFlash(void* dst, const void* src, unsigned int len) { memcpy(dst, src, len); }
    void     NT_log(const char* text) { fprintf(stderr, "[NT_log] %s\n", text); }
    float    NT_getSampleRate(void) { return (float)NT_globals.sampleRate; }
    unsigned int NT_getSamplesPerBlock(void) { return NT_globals.maxFramesPerStep; }
    unsigned int NT_random(unsigned int max) { return max ? ((unsigned int)rand() % max) : 0; }
    float    NT_randomF(void) { return (float)rand() / (float)RAND_MAX; }
}

// ---------------------------------------------------------------------------
// SD card / Wavetable / Sample — stubs for headless testing
// ---------------------------------------------------------------------------
extern "C" {
    bool     NT_isSdCardMounted(void) { return false; }
    uint32_t NT_getNumSampleFolders(void) { return 0; }
    void     NT_getSampleFolderInfo(uint32_t, _NT_wavFolderInfo& info) { info.name = ""; info.numSampleFiles = 0; }
    void     NT_getSampleFileInfo(uint32_t, uint32_t, _NT_wavInfo& info) { info.name = ""; info.numFrames = 0; info.sampleRate = 44100; info.channels = kNT_WavMono; info.bits = kNT_WavBits16; }
    bool     NT_readSampleFrames(const _NT_wavRequest&) { return false; }
    uint32_t NT_getNumWavetables(void) { return 0; }
    void     NT_getWavetableInfo(uint32_t, _NT_wavetableInfo& info) { info.name = ""; }
    bool     NT_readWavetable(_NT_wavetableRequest&) { return false; }
    float    NT_evaluateWavetable(const _NT_wavetableRequest&, _NT_wavetableEvaluation&) { return 0.0f; }
    bool     NT_streamOpen(_NT_stream, const _NT_streamOpenData&) { return false; }
    uint32_t NT_streamRender(_NT_stream, _NT_frame*, uint32_t, float) { return 0; }

    // SCL microtuning — no SD card in headless tests
    uint32_t NT_getNumScl(void) { return 0; }
    void     NT_getSclInfo(uint32_t, _NT_sclInfo& info) { info.name = ""; }
    bool     NT_readScl(_NT_sclRequest& request) {
        request.error = true;
        request.numNotes = 0;
        return false;
    }
}

// ---------------------------------------------------------------------------
// Serialisation — in-memory JSON DOM implementation
// ---------------------------------------------------------------------------
// Both _NT_jsonStream and _NT_jsonParse use the opaque `refCon` slot to point
// at a private context allocated by the harness helpers in json_dom. The
// ABI-public methods forward into that context.

#include "json_dom.h"
using NtTestHarness::JsonValue;
using NtTestHarness::JsonValuePtr;
using NtTestHarness::JsonMember;

namespace {

// ---------- Write context -------------------------------------------------
// stack[back()] is the container currently being written into. pendingName
// is set by addMemberName() and consumed by the next added value.
struct WriteCtx {
    JsonValuePtr               root;
    std::vector<JsonValuePtr>  stack;
    std::string                pendingName;

    void addValue(JsonValuePtr v) {
        if (stack.empty()) return;
        auto& c = *stack.back();
        if (c.kind == JsonValue::K_OBJECT) {
            JsonMember m;
            m.name  = pendingName;
            m.value = v;
            c.obj.push_back(std::move(m));
            pendingName.clear();
        } else if (c.kind == JsonValue::K_ARRAY) {
            c.arr.push_back(v);
        }
    }
};

// ---------- Read context --------------------------------------------------
// `cursor` is the value that will be consumed by the next primitive read or
// container-enter. When null, primitive readers pull from the current array
// frame. Frames auto-pop when exhausted so the parent's iteration resumes
// transparently (matches the example contract in distingNT_API).
struct ReadCtx {
    struct Frame {
        JsonValuePtr container;
        size_t       pos;
    };
    JsonValuePtr        root;
    std::vector<Frame>  stack;
    JsonValuePtr        cursor;

    void autoPop() {
        while (!stack.empty()) {
            const auto& f = stack.back();
            size_t end = (f.container->kind == JsonValue::K_ARRAY)
                       ? f.container->arr.size()
                       : f.container->obj.size();
            if (f.pos >= end && !cursor) stack.pop_back();
            else break;
        }
    }

    // Pull the next value to consume. Cursor takes precedence; otherwise pop
    // the next array element. Returns null if no value is available.
    JsonValuePtr take() {
        autoPop();
        if (cursor) {
            auto v = cursor;
            cursor.reset();
            return v;
        }
        if (!stack.empty()) {
            auto& f = stack.back();
            if (f.container->kind == JsonValue::K_ARRAY
                && f.pos < f.container->arr.size()) {
                return f.container->arr[f.pos++];
            }
        }
        return nullptr;
    }
};

} // anonymous

// ---------- _NT_jsonStream ABI --------------------------------------------
_NT_jsonStream::_NT_jsonStream(void* p) : refCon(p) {}
_NT_jsonStream::~_NT_jsonStream() {}

void _NT_jsonStream::openArray() {
    auto* ctx = static_cast<WriteCtx*>(refCon);
    auto v = JsonValue::makeArray();
    ctx->addValue(v);
    ctx->stack.push_back(v);
}
void _NT_jsonStream::closeArray() {
    auto* ctx = static_cast<WriteCtx*>(refCon);
    if (!ctx->stack.empty()) ctx->stack.pop_back();
}
void _NT_jsonStream::openObject() {
    auto* ctx = static_cast<WriteCtx*>(refCon);
    auto v = JsonValue::makeObject();
    ctx->addValue(v);
    ctx->stack.push_back(v);
}
void _NT_jsonStream::closeObject() {
    auto* ctx = static_cast<WriteCtx*>(refCon);
    if (!ctx->stack.empty()) ctx->stack.pop_back();
}
void _NT_jsonStream::addMemberName(const char* str) {
    auto* ctx = static_cast<WriteCtx*>(refCon);
    ctx->pendingName = str ? str : "";
}
void _NT_jsonStream::addNumber(int v) {
    auto* ctx = static_cast<WriteCtx*>(refCon);
    auto val = std::make_shared<JsonValue>();
    val->kind = JsonValue::K_INT;
    val->i    = v;
    ctx->addValue(val);
}
void _NT_jsonStream::addNumber(float v) {
    auto* ctx = static_cast<WriteCtx*>(refCon);
    auto val = std::make_shared<JsonValue>();
    val->kind = JsonValue::K_FLOAT;
    val->f    = v;
    ctx->addValue(val);
}
void _NT_jsonStream::addString(const char* str) {
    auto* ctx = static_cast<WriteCtx*>(refCon);
    auto val = std::make_shared<JsonValue>();
    val->kind = JsonValue::K_STRING;
    val->s    = str ? str : "";
    ctx->addValue(val);
}
void _NT_jsonStream::addFourCC(uint32_t v) {
    char buf[5] = { (char)((v>>24)&0xFF), (char)((v>>16)&0xFF),
                    (char)((v>>8)&0xFF),  (char)(v&0xFF), 0 };
    addString(buf);
}
void _NT_jsonStream::addBoolean(bool v) {
    auto* ctx = static_cast<WriteCtx*>(refCon);
    auto val = std::make_shared<JsonValue>();
    val->kind = JsonValue::K_BOOL;
    val->b    = v;
    ctx->addValue(val);
}
void _NT_jsonStream::addNull() {
    auto* ctx = static_cast<WriteCtx*>(refCon);
    auto val = std::make_shared<JsonValue>();
    val->kind = JsonValue::K_NULL;
    ctx->addValue(val);
}

// ---------- _NT_jsonParse ABI ---------------------------------------------
_NT_jsonParse::_NT_jsonParse(void* p, int) : refCon(p), i(0) {}
_NT_jsonParse::~_NT_jsonParse() {}

bool _NT_jsonParse::numberOfArrayElements(int& num) {
    auto* ctx = static_cast<ReadCtx*>(refCon);
    ctx->autoPop();
    JsonValuePtr v;
    if (ctx->cursor) { v = ctx->cursor; ctx->cursor.reset(); }
    else if (!ctx->stack.empty()) {
        auto& f = ctx->stack.back();
        if (f.container->kind == JsonValue::K_ARRAY
            && f.pos < f.container->arr.size())
            v = f.container->arr[f.pos++];
    }
    if (!v || v->kind != JsonValue::K_ARRAY) return false;
    num = (int)v->arr.size();
    ctx->stack.push_back({v, 0});
    return true;
}

bool _NT_jsonParse::numberOfObjectMembers(int& num) {
    auto* ctx = static_cast<ReadCtx*>(refCon);
    ctx->autoPop();
    JsonValuePtr v;
    if (ctx->cursor) { v = ctx->cursor; ctx->cursor.reset(); }
    else if (!ctx->stack.empty()) {
        auto& f = ctx->stack.back();
        if (f.container->kind == JsonValue::K_ARRAY
            && f.pos < f.container->arr.size())
            v = f.container->arr[f.pos++];
    }
    if (!v || v->kind != JsonValue::K_OBJECT) return false;
    num = (int)v->obj.size();
    ctx->stack.push_back({v, 0});
    return true;
}

bool _NT_jsonParse::matchName(const char* name) {
    auto* ctx = static_cast<ReadCtx*>(refCon);
    ctx->autoPop();
    if (ctx->stack.empty()) return false;
    auto& f = ctx->stack.back();
    if (f.container->kind != JsonValue::K_OBJECT) return false;
    if (f.pos >= f.container->obj.size()) return false;
    const auto& m = f.container->obj[f.pos];
    if (m.name != (name ? name : "")) return false;
    ctx->cursor = m.value;
    f.pos++;
    return true;
}

bool _NT_jsonParse::skipMember() {
    auto* ctx = static_cast<ReadCtx*>(refCon);
    ctx->autoPop();
    if (ctx->stack.empty()) return false;
    auto& f = ctx->stack.back();
    if (f.container->kind != JsonValue::K_OBJECT) return false;
    if (f.pos >= f.container->obj.size()) return false;
    f.pos++;
    ctx->cursor.reset();
    return true;
}

bool _NT_jsonParse::number(int& v) {
    auto* ctx = static_cast<ReadCtx*>(refCon);
    auto val = ctx->take();
    if (!val) return false;
    if (val->kind == JsonValue::K_INT)        { v = val->i; return true; }
    if (val->kind == JsonValue::K_FLOAT)      { v = (int)val->f; return true; }
    if (val->kind == JsonValue::K_BOOL)       { v = val->b ? 1 : 0; return true; }
    return false;
}
bool _NT_jsonParse::number(float& v) {
    auto* ctx = static_cast<ReadCtx*>(refCon);
    auto val = ctx->take();
    if (!val) return false;
    if (val->kind == JsonValue::K_FLOAT) { v = val->f; return true; }
    if (val->kind == JsonValue::K_INT)   { v = (float)val->i; return true; }
    return false;
}
bool _NT_jsonParse::string(const char*& str) {
    auto* ctx = static_cast<ReadCtx*>(refCon);
    auto val = ctx->take();
    if (!val) return false;
    if (val->kind != JsonValue::K_STRING) return false;
    str = val->s.c_str();
    return true;
}
bool _NT_jsonParse::boolean(bool& v) {
    auto* ctx = static_cast<ReadCtx*>(refCon);
    auto val = ctx->take();
    if (!val) return false;
    if (val->kind != JsonValue::K_BOOL) return false;
    v = val->b;
    return true;
}
bool _NT_jsonParse::null() {
    auto* ctx = static_cast<ReadCtx*>(refCon);
    auto val = ctx->take();
    if (!val) return false;
    return val->kind == JsonValue::K_NULL;
}

// ---------- Harness helpers (declared in json_dom.h) ----------------------
namespace NtTestHarness {

JsonValuePtr serialiseToDom(const _NT_factory* factory, _NT_algorithm* alg) {
    if (!factory || !factory->serialise || !alg) return nullptr;
    WriteCtx ctx;
    ctx.root = JsonValue::makeObject();
    ctx.stack.push_back(ctx.root);
    _NT_jsonStream stream(&ctx);
    factory->serialise(alg, stream);
    return ctx.root;
}

bool deserialiseFromDom(const _NT_factory* factory, _NT_algorithm* alg,
                        JsonValuePtr root) {
    if (!factory || !factory->deserialise || !alg || !root) return false;
    if (root->kind != JsonValue::K_OBJECT) return false;
    ReadCtx ctx;
    ctx.root   = root;
    ctx.cursor = root;   // first call (numberOfObjectMembers) consumes root
    _NT_jsonParse parse(&ctx, 0);
    return factory->deserialise(alg, parse);
}

} // namespace NtTestHarness
