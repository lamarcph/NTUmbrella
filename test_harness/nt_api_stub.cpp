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

#include <distingnt/api.h>
#include <distingnt/wav.h>
#include <distingnt/serialisation.h>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cmath>

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
// Drawing — no-ops in headless mode
// ---------------------------------------------------------------------------
extern "C" {
    void NT_drawText(int, int, const char*, int, _NT_textAlignment, _NT_textSize) {}
    void NT_drawShapeI(_NT_shape, int, int, int, int, int) {}
    void NT_drawShapeF(_NT_shape, float, float, float, float, float) {}
    void NT_getDisplayDimensions(unsigned int* w, unsigned int* h) { if (w) *w = 256; if (h) *h = 64; }
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
    int NT_getTextWidthUTF8(const char* text, _NT_textSize) {
        return (int)strlen(text) * 8;   // rough approximation
    }
}

// ---------------------------------------------------------------------------
// MIDI send — no-ops (tests can hook these if needed)
// ---------------------------------------------------------------------------
extern "C" {
    void NT_sendMidiByte(uint32_t, uint8_t) {}
    void NT_sendMidi2ByteMessage(uint32_t, uint8_t, uint8_t) {}
    void NT_sendMidi3ByteMessage(uint32_t, uint8_t, uint8_t, uint8_t) {}
    void NT_sendMidiSysEx(uint32_t, const uint8_t*, uint32_t, bool) {}
}

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
}

// ---------------------------------------------------------------------------
// Serialisation stubs — no-ops for headless testing
// ---------------------------------------------------------------------------
_NT_jsonStream::_NT_jsonStream(void*) : refCon(nullptr) {}
_NT_jsonStream::~_NT_jsonStream() {}
void _NT_jsonStream::openArray() {}
void _NT_jsonStream::closeArray() {}
void _NT_jsonStream::openObject() {}
void _NT_jsonStream::closeObject() {}
void _NT_jsonStream::addMemberName(const char*) {}
void _NT_jsonStream::addNumber(int) {}
void _NT_jsonStream::addNumber(float) {}
void _NT_jsonStream::addString(const char*) {}
void _NT_jsonStream::addFourCC(uint32_t) {}
void _NT_jsonStream::addBoolean(bool) {}
void _NT_jsonStream::addNull() {}

_NT_jsonParse::_NT_jsonParse(void*, int) : refCon(nullptr), i(0) {}
_NT_jsonParse::~_NT_jsonParse() {}
bool _NT_jsonParse::numberOfArrayElements(int&) { return false; }
bool _NT_jsonParse::numberOfObjectMembers(int&) { return false; }
bool _NT_jsonParse::matchName(const char*) { return false; }
bool _NT_jsonParse::skipMember() { return false; }
bool _NT_jsonParse::number(int&) { return false; }
bool _NT_jsonParse::number(float&) { return false; }
bool _NT_jsonParse::string(const char*&) { return false; }
bool _NT_jsonParse::boolean(bool&) { return false; }
bool _NT_jsonParse::null() { return false; }
