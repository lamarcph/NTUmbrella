// =============================================================================
// wav_writer.h — Minimal single-header 16-bit PCM WAV file writer
// =============================================================================
// Writes float audio buffers to .wav files for offline inspection.
// Supports mono and stereo. No dependencies beyond <cstdio> and <cstdint>.
//
// The disting NT API maps 1.0f = 1V, with full output range ±5V.
// By default the writer normalizes by 5.0 so that ±5V maps to ±1.0 in the WAV,
// matching the hardware's full-scale output without clipping.
//
// Usage:
//   WavWriter wav("output.wav", 96000, 1);  // mono, 96 kHz
//   wav.write(buffer, numSamples);           // float* in [-5, 5] (NT volts)
//   wav.close();                             // finalize header
// =============================================================================

#pragma once

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <algorithm>

// disting NT full-scale voltage: ±5V → ±1.0 in WAV
static constexpr float kNtVoltsToWav = 1.0f / 5.0f;

class WavWriter {
public:
    WavWriter(const char* filename, uint32_t sampleRate, uint16_t numChannels)
        : _file(nullptr), _sampleRate(sampleRate), _numChannels(numChannels), _dataBytes(0)
    {
        _file = fopen(filename, "wb");
        if (_file) {
            // Write placeholder header — will be patched in close()
            uint8_t header[44] = {};
            fwrite(header, 1, 44, _file);
        }
    }

    ~WavWriter() {
        close();
    }

    // Non-copyable
    WavWriter(const WavWriter&) = delete;
    WavWriter& operator=(const WavWriter&) = delete;

    bool isOpen() const { return _file != nullptr; }

    // Write interleaved float samples. count = total samples (frames × channels).
    // Input is in NT volts (±5V full scale); scaled to ±1.0 for 16-bit PCM.
    void write(const float* data, uint32_t count) {
        if (!_file) return;
        for (uint32_t i = 0; i < count; ++i) {
            float s = std::max(-1.0f, std::min(1.0f, data[i] * kNtVoltsToWav));
            int16_t sample = static_cast<int16_t>(s * 32767.0f);
            fwrite(&sample, sizeof(int16_t), 1, _file);
        }
        _dataBytes += count * sizeof(int16_t);
    }

    // Write mono from a single float* buffer (convenience).
    void writeMono(const float* data, uint32_t numFrames) {
        write(data, numFrames);
    }

    // Write stereo by interleaving two mono buffers.
    // Input is in NT volts (±5V full scale); scaled to ±1.0 for 16-bit PCM.
    void writeStereo(const float* left, const float* right, uint32_t numFrames) {
        if (!_file) return;
        for (uint32_t i = 0; i < numFrames; ++i) {
            float sl = std::max(-1.0f, std::min(1.0f, left[i] * kNtVoltsToWav));
            float sr = std::max(-1.0f, std::min(1.0f, right[i] * kNtVoltsToWav));
            int16_t sampleL = static_cast<int16_t>(sl * 32767.0f);
            int16_t sampleR = static_cast<int16_t>(sr * 32767.0f);
            fwrite(&sampleL, sizeof(int16_t), 1, _file);
            fwrite(&sampleR, sizeof(int16_t), 1, _file);
        }
        _dataBytes += numFrames * 2 * sizeof(int16_t);
    }

    void close() {
        if (!_file) return;

        // Patch the WAV header
        uint32_t fileSize = 36 + _dataBytes;
        uint16_t bitsPerSample = 16;
        uint16_t blockAlign = _numChannels * (bitsPerSample / 8);
        uint32_t byteRate = _sampleRate * blockAlign;

        fseek(_file, 0, SEEK_SET);

        // RIFF header
        fwrite("RIFF", 1, 4, _file);
        writeU32(fileSize);
        fwrite("WAVE", 1, 4, _file);

        // fmt chunk
        fwrite("fmt ", 1, 4, _file);
        writeU32(16);                    // chunk size
        writeU16(1);                     // PCM format
        writeU16(_numChannels);
        writeU32(_sampleRate);
        writeU32(byteRate);
        writeU16(blockAlign);
        writeU16(bitsPerSample);

        // data chunk
        fwrite("data", 1, 4, _file);
        writeU32(_dataBytes);

        fclose(_file);
        _file = nullptr;
    }

private:
    FILE* _file;
    uint32_t _sampleRate;
    uint16_t _numChannels;
    uint32_t _dataBytes;

    void writeU16(uint16_t v) { fwrite(&v, sizeof(v), 1, _file); }
    void writeU32(uint32_t v) { fwrite(&v, sizeof(v), 1, _file); }
};
