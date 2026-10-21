#include "image_io.h"

#include <cstdio>

namespace {
uint32_t crcTable[256];
bool crcInit = false;
void initCrc() {
  for (uint32_t n = 0; n < 256; ++n) {
    uint32_t c = n;
    for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    crcTable[n] = c;
  }
  crcInit = true;
}
uint32_t crc(const uint8_t* b, size_t n, uint32_t c = 0xFFFFFFFFu) {
  for (size_t i = 0; i < n; ++i) c = crcTable[(c ^ b[i]) & 0xFF] ^ (c >> 8);
  return c;
}
void put32(std::vector<uint8_t>& v, uint32_t x) {
  v.push_back((uint8_t)(x >> 24));
  v.push_back((uint8_t)(x >> 16));
  v.push_back((uint8_t)(x >> 8));
  v.push_back((uint8_t)x);
}
void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data) {
  std::vector<uint8_t> buf;
  put32(buf, (uint32_t)data.size());
  buf.insert(buf.end(), type, type + 4);
  buf.insert(buf.end(), data.begin(), data.end());
  uint32_t c = crc(buf.data() + 4, buf.size() - 4) ^ 0xFFFFFFFFu;
  put32(buf, c);
  fwrite(buf.data(), 1, buf.size(), f);
}
}  // namespace

bool writePNG(const std::string& path, int w, int h, int ch, const uint8_t* data, bool flipY) {
  if (!crcInit) initCrc();
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  fwrite(sig, 1, 8, f);
  std::vector<uint8_t> ihdr;
  put32(ihdr, (uint32_t)w);
  put32(ihdr, (uint32_t)h);
  ihdr.push_back(8);
  ihdr.push_back(ch == 4 ? 6 : 2);
  ihdr.push_back(0);
  ihdr.push_back(0);
  ihdr.push_back(0);
  chunk(f, "IHDR", ihdr);
  // raw scanlines with filter byte 0
  std::vector<uint8_t> raw;
  size_t stride = (size_t)w * ch;
  raw.reserve((stride + 1) * h);
  for (int y = 0; y < h; ++y) {
    int sy = flipY ? (h - 1 - y) : y;
    raw.push_back(0);
    raw.insert(raw.end(), data + sy * stride, data + sy * stride + stride);
  }
  // zlib stream with stored blocks
  std::vector<uint8_t> z;
  z.push_back(0x78);
  z.push_back(0x01);
  size_t pos = 0;
  while (pos < raw.size()) {
    size_t n = raw.size() - pos;
    if (n > 65535) n = 65535;
    bool last = pos + n == raw.size();
    z.push_back(last ? 1 : 0);
    z.push_back((uint8_t)(n & 0xFF));
    z.push_back((uint8_t)(n >> 8));
    z.push_back((uint8_t)(~n & 0xFF));
    z.push_back((uint8_t)((~n >> 8) & 0xFF));
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
  }
  uint32_t a = 1, b = 0;
  for (uint8_t v : raw) {
    a = (a + v) % 65521;
    b = (b + a) % 65521;
  }
  put32(z, (b << 16) | a);
  chunk(f, "IDAT", z);
  chunk(f, "IEND", {});
  std::fclose(f);
  return true;
}
