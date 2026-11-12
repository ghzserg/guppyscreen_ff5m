#ifndef __PNG_DECODER_H__
#define __PNG_DECODER_H__

#include "lvgl/lvgl.h"

#include <cstdint>

// PNG decoder that keeps images as 8-bit indexed (at most 256 colours):
// LV_IMG_CF_INDEXED_8BIT, a 256-entry lv_color32_t palette followed by one
// index byte per pixel. LVGL's own PNG decoder keeps 4 bytes per pixel
// (an 800x480 input-shaper graph: 1.5 MB instead of 385 KB).
//
// Images with at most 256 distinct colours keep them exactly; others are
// reduced with a median cut. Drawing needs patches/0006 (LVGL's software
// renderer draws decoded indexed images, also zoomed).
namespace PngDecoder {

// Register the decoder. LVGL tries the most recently registered decoder
// first, so call this after lv_png_init(); LVGL's decoder remains the
// fallback if this one fails.
void init();

// Reduce RGBA pixels (r, g, b, a bytes) to at most 256 colours: fills
// palette[0..n) and one index per pixel into `indices` (px_count bytes),
// returns n. Fully transparent pixels share one entry. Exposed for tests.
uint32_t quantize(const uint8_t *rgba, uint32_t px_count, lv_color32_t *palette, uint8_t *indices);

}  // namespace PngDecoder

#endif  // __PNG_DECODER_H__
