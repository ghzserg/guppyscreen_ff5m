#ifndef GUPPY_TEST_PNG_WRITER_H
#define GUPPY_TEST_PNG_WRITER_H

// Minimal PNG writer for tests (RGBA 8-bit, uncompressed deflate blocks):
// LVGL's lodepng is built without its encoder.

#include <cstdint>
#include <string>
#include <vector>

namespace test_png {

// `rgba`: w * h pixels, r, g, b, a bytes each.
void write(const std::string &path, const std::vector<unsigned char> &rgba, uint32_t w, uint32_t h);

}  // namespace test_png

#endif  // GUPPY_TEST_PNG_WRITER_H
