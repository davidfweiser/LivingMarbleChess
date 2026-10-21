#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Writes an 8-bit RGB or RGBA PNG (uncompressed deflate, no dependencies).
bool writePNG(const std::string& path, int w, int h, int channels, const uint8_t* data, bool flipY);
