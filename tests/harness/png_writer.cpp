#include "png_writer.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace test_png {

void write(const std::string &path, const std::vector<unsigned char> &rgba, uint32_t w, uint32_t h) {
  auto crc32 = [](const std::vector<unsigned char> &d) {
    uint32_t c = 0xffffffffu;
    for (unsigned char b : d) {
      c ^= b;
      for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
    }
    return c ^ 0xffffffffu;
  };
  auto be32 = [](std::vector<unsigned char> &v, uint32_t x) {
    for (int i = 3; i >= 0; i--) v.push_back((x >> (8 * i)) & 0xff);
  };
  std::vector<unsigned char> out = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
  auto chunk = [&](const char *type, const std::vector<unsigned char> &data) {
    be32(out, data.size());
    std::vector<unsigned char> td(type, type + 4);
    td.insert(td.end(), data.begin(), data.end());
    out.insert(out.end(), td.begin(), td.end());
    be32(out, crc32(td));
  };
  std::vector<unsigned char> ihdr;
  be32(ihdr, w);
  be32(ihdr, h);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});
  chunk("IHDR", ihdr);

  std::vector<unsigned char> raw;
  for (uint32_t y = 0; y < h; y++) {
    raw.push_back(0);  // filter: none
    raw.insert(raw.end(), rgba.begin() + y * w * 4, rgba.begin() + (y + 1) * w * 4);
  }
  std::vector<unsigned char> z = {0x78, 0x01};
  for (size_t pos = 0; pos < raw.size() || pos == 0; pos += 65535) {
    size_t n = std::min<size_t>(65535, raw.size() - pos);
    z.push_back(pos + n >= raw.size() ? 1 : 0);
    z.insert(z.end(), {(unsigned char)(n & 0xff), (unsigned char)(n >> 8),
                       (unsigned char)(~n & 0xff), (unsigned char)((~n >> 8) & 0xff)});
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    if (pos + n >= raw.size()) break;
  }
  uint32_t a = 1, b = 0;
  for (unsigned char c : raw) { a = (a + c) % 65521; b = (b + a) % 65521; }
  be32(z, (b << 16) | a);
  chunk("IDAT", z);
  chunk("IEND", {});

  FILE *f = fopen(path.c_str(), "wb");
  if (f == NULL) throw std::runtime_error("cannot write " + path);
  fwrite(out.data(), 1, out.size(), f);
  fclose(f);
}

}  // namespace test_png
