#pragma once
// =============================================================================
// SHA-256 file hashing — delegates to the system's sha256sum command
// =============================================================================
// Usage:   std::string hash = sha256_file("path/to/file");
// Returns: 64-char lowercase hex string, or "" on error.
// =============================================================================

#include <string>
#include <cstdio>
#include <cstring>

static std::string sha256_file(const char* path) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "sha256sum '%s' 2>/dev/null", path);
    FILE* p = popen(cmd, "r");
    if (!p) return "";
    char buf[256];
    if (!fgets(buf, sizeof(buf), p)) { pclose(p); return ""; }
    pclose(p);
    // sha256sum output: "<64hex>  <filename>\n"
    if (std::strlen(buf) < 64) return "";
    return std::string(buf, 64);
}
