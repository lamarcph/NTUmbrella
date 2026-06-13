#pragma once
// =============================================================================
// screen_writer.h — NT_screen → PGM export + SHA-256 golden hash verification
// =============================================================================
// Exports the disting NT 256×64 4-bit grayscale framebuffer (NT_screen) to
// PGM (P5) format for visual inspection and deterministic golden-hash testing.
//
// PGM is headerless-simple, requires no external libraries, produces
// byte-identical output for the same pixel data, and is viewable in any
// image viewer.  For human-friendly PNGs, shell out to ImageMagick:
//   convert screen.pgm -scale 400% screen.png
//
// Usage:
//   ntscreen_to_pgm("bin/screen_step_view.pgm");
//   std::string hash = ntscreen_hash("bin/screen_step_view.pgm");
//   ASSERT_TRUE(hash == "abc123...", "screen matches golden hash");
// =============================================================================

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include "sha256.h"

// The NT API declares:  extern uint8_t NT_screen[128*64];
extern uint8_t NT_screen[128 * 64];

static constexpr int NT_SCREEN_W = 256;
static constexpr int NT_SCREEN_H = 64;
static constexpr int NT_SCREEN_STRIDE = 128;  // bytes per row (2 pixels/byte)

// ---------------------------------------------------------------------------
// ntscreen_to_pgm — write NT_screen to a PGM (P5, binary) file
// ---------------------------------------------------------------------------
// Pixel format: each byte in NT_screen holds two 4-bit pixels.
//   Even x → high nibble, odd x → low nibble.
// PGM pixels are upscaled from 4-bit (0–15) to 8-bit (0–255) via *17.
//
// Returns true on success.
static bool ntscreen_to_pgm(const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;

    // PGM P5 header
    fprintf(f, "P5\n%d %d\n255\n", NT_SCREEN_W, NT_SCREEN_H);

    uint8_t row[NT_SCREEN_W];
    for (int y = 0; y < NT_SCREEN_H; ++y) {
        const uint8_t* src = NT_screen + y * NT_SCREEN_STRIDE;
        for (int x = 0; x < NT_SCREEN_W; x += 2) {
            uint8_t packed = src[x / 2];
            row[x]     = (uint8_t)(((packed >> 4) & 0x0F) * 17);  // high nibble
            row[x + 1] = (uint8_t)((packed & 0x0F) * 17);         // low nibble
        }
        fwrite(row, 1, NT_SCREEN_W, f);
    }

    fclose(f);
    return true;
}

// ---------------------------------------------------------------------------
// ntscreen_hash — write PGM, return SHA-256 hash
// ---------------------------------------------------------------------------
// Writes the PGM to `path`, then hashes it via sha256_file (from sha256.h).
// Returns 64-char lowercase hex string, or "" on error.
static std::string ntscreen_hash(const char* pgm_path) {
    if (!ntscreen_to_pgm(pgm_path)) return "";
    return sha256_file(pgm_path);
}

// ---------------------------------------------------------------------------
// ntscreen_clear — zero the screen buffer (call before draw())
// ---------------------------------------------------------------------------
static void ntscreen_clear() {
    memset(NT_screen, 0, 128 * 64);
}
