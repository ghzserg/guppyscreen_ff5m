#include "png_decoder.h"

#include "spdlog/spdlog.h"

#ifndef LV_DRAW_IMG_INDEXED_8BIT_DECODED
// Without the patch LVGL draws an indexed buffer as 4 bytes per pixel and
// reads past its end.
#error "LVGL must be patched with patches/0006-lvgl-draw-indexed-8bit-decoded-images.patch"
#endif

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

// From LVGL's lodepng (lvgl/src/extra/libs/png), compiled as C. Its header
// doesn't give the C functions C linkage when included from C++.
extern "C" {
unsigned lodepng_load_file(unsigned char **out, size_t *outsize, const char *filename);
unsigned lodepng_decode32(unsigned char **out, unsigned *w, unsigned *h, const unsigned char *in, size_t insize);
const char *lodepng_error_text(unsigned code);
}

namespace PngDecoder {

// Below this size LVGL's own decoder keeps the image (ARGB8888). Indexed
// saves 3 bytes per pixel but quantizing can need a ~390 KB temporary
// histogram, so it only pays off for big images (measured on the AD5M), and
// thumbnails must keep their colours. 256k pixels: the 800x480 input-shaper
// graphs (384k) are indexed; slicer thumbnails (140x110 on zMod, up to
// ~400x400 or 480x320 elsewhere) stay true colour.
const uint32_t MIN_PIXELS = 256 * 1024;

namespace {

constexpr uint32_t PALETTE_BYTES = 256 * sizeof(lv_color32_t);

inline uint32_t key_of(const uint8_t *p) {
  // ARGB; all fully transparent pixels are the same colour.
  return p[3] == 0 ? 0 : ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
}

inline lv_color32_t color_of(uint32_t key) {
  lv_color32_t c;
  c.ch.alpha = key >> 24;
  c.ch.red = (key >> 16) & 0xff;
  c.ch.green = (key >> 8) & 0xff;
  c.ch.blue = key & 0xff;
  return c;
}

inline uint32_t hash(uint32_t k) {
  k ^= k >> 16;
  k *= 0x7feb352dU;
  k ^= k >> 15;
  return k;
}

// Exact palette if there are at most 256 distinct colours.
bool exact_palette(const uint8_t *rgba, uint32_t n, lv_color32_t *palette, uint8_t *indices, uint32_t &count) {
  constexpr uint32_t SLOTS = 1024;  // > 256, power of two
  std::vector<uint32_t> keys(SLOTS);
  std::vector<int16_t> slot_idx(SLOTS, -1);
  count = 0;
  for (uint32_t i = 0; i < n; i++) {
    uint32_t k = key_of(rgba + 4 * i);
    uint32_t s = hash(k) & (SLOTS - 1);
    while (slot_idx[s] >= 0 && keys[s] != k) s = (s + 1) & (SLOTS - 1);
    if (slot_idx[s] < 0) {
      if (count == 256) return false;
      keys[s] = k;
      slot_idx[s] = (int16_t)count;
      palette[count++] = color_of(k);
    }
    indices[i] = (uint8_t)slot_idx[s];
  }
  return true;
}

struct Bin {
  uint32_t n, r, g, b, a;
  uint8_t mean[4];  // r, g, b, a
};

// Median cut over a histogram of 4 alpha classes x RGB444 bins, with exact
// per-bin colour sums; then each pixel gets its nearest palette colour.
uint32_t median_cut(const uint8_t *rgba, uint32_t n, lv_color32_t *palette, uint8_t *indices) {
  auto bin_of = [](const uint8_t *p) {
    uint32_t a = p[3];
    uint32_t cls = a == 0 ? 0 : a < 128 ? 1 : a < 255 ? 2 : 3;
    if (cls == 0) return 0u;
    return (cls << 12) | ((uint32_t)(p[0] >> 4) << 8) | ((uint32_t)(p[1] >> 4) << 4) | (p[2] >> 4);
  };

  std::vector<Bin> hist(4 << 12);
  for (uint32_t i = 0; i < n; i++) {
    const uint8_t *p = rgba + 4 * i;
    Bin &b = hist[bin_of(p)];
    b.n++;
    if (p[3]) {
      b.r += p[0];
      b.g += p[1];
      b.b += p[2];
      b.a += p[3];
    }
  }

  std::vector<Bin> bins;
  for (auto &b : hist) {
    if (b.n == 0) continue;
    b.mean[0] = b.r / b.n;
    b.mean[1] = b.g / b.n;
    b.mean[2] = b.b / b.n;
    b.mean[3] = b.a / b.n;
    bins.push_back(b);
  }
  std::vector<Bin>().swap(hist);

  struct Box { uint32_t begin, end; };
  std::vector<Box> boxes = {{0, (uint32_t)bins.size()}};

  auto box_score = [&](const Box &bx, int &channel) {
    uint8_t lo[4] = {255, 255, 255, 255}, hi[4] = {0, 0, 0, 0};
    uint64_t pop = 0;
    for (uint32_t i = bx.begin; i < bx.end; i++) {
      for (int c = 0; c < 4; c++) {
        lo[c] = std::min(lo[c], bins[i].mean[c]);
        hi[c] = std::max(hi[c], bins[i].mean[c]);
      }
      pop += bins[i].n;
    }
    int range = -1;
    for (int c = 0; c < 4; c++) {
      if (hi[c] - lo[c] > range) { range = hi[c] - lo[c]; channel = c; }
    }
    return bx.end - bx.begin < 2 ? 0 : (uint64_t)range * pop;
  };

  while (boxes.size() < 256) {
    uint64_t best = 0;
    size_t best_i = 0;
    int best_c = 0;
    for (size_t i = 0; i < boxes.size(); i++) {
      int c;
      uint64_t s = box_score(boxes[i], c);
      if (s > best) { best = s; best_i = i; best_c = c; }
    }
    if (best == 0) break;

    Box bx = boxes[best_i];
    std::sort(bins.begin() + bx.begin, bins.begin() + bx.end,
              [best_c](const Bin &x, const Bin &y) { return x.mean[best_c] < y.mean[best_c]; });
    uint64_t total = 0, acc = 0;
    for (uint32_t i = bx.begin; i < bx.end; i++) total += bins[i].n;
    uint32_t split = bx.begin + 1;
    for (uint32_t i = bx.begin; i < bx.end - 1; i++) {
      acc += bins[i].n;
      split = i + 1;
      if (acc * 2 >= total) break;
    }
    boxes[best_i] = {bx.begin, split};
    boxes.push_back({split, bx.end});
  }

  uint32_t count = boxes.size();
  for (uint32_t k = 0; k < count; k++) {
    uint64_t sn = 0, sa = 0, sr = 0, sg = 0, sb = 0, nn = 0;
    for (uint32_t i = boxes[k].begin; i < boxes[k].end; i++) {
      const Bin &b = bins[i];
      nn += b.n;
      sr += b.r; sg += b.g; sb += b.b; sa += b.a;
      if (b.a) sn += b.n;  // pixels that are not fully transparent
    }
    lv_color32_t c;
    c.ch.red = sn ? sr / sn : 0;
    c.ch.green = sn ? sg / sn : 0;
    c.ch.blue = sn ? sb / sn : 0;
    c.ch.alpha = nn ? sa / nn : 0;
    palette[k] = c;
  }

  // Nearest palette colour per pixel, cached by exact colour.
  constexpr uint32_t CACHE = 4096;
  std::vector<uint32_t> cache_key(CACHE);
  std::vector<int16_t> cache_idx(CACHE, -1);
  for (uint32_t i = 0; i < n; i++) {
    const uint8_t *p = rgba + 4 * i;
    uint32_t k = key_of(p);
    uint32_t s = hash(k) & (CACHE - 1);
    if (cache_idx[s] >= 0 && cache_key[s] == k) {
      indices[i] = (uint8_t)cache_idx[s];
      continue;
    }
    int r = p[0], g = p[1], b = p[2], a = p[3];
    if (a == 0) r = g = b = 0;
    uint32_t best = UINT32_MAX;
    uint32_t best_k = 0;
    for (uint32_t j = 0; j < count; j++) {
      int dr = r - palette[j].ch.red, dg = g - palette[j].ch.green;
      int db = b - palette[j].ch.blue, da = a - palette[j].ch.alpha;
      uint32_t d = dr * dr + dg * dg + db * db + 2 * da * da;
      if (d < best) { best = d; best_k = j; }
    }
    cache_key[s] = k;
    cache_idx[s] = (int16_t)best_k;
    indices[i] = (uint8_t)best_k;
  }
  return count;
}

bool is_png_file(const void *src) {
  return lv_img_src_get_type(src) == LV_IMG_SRC_FILE
    && strcmp(lv_fs_get_ext((const char *)src), "png") == 0;
}

lv_res_t decoder_info(lv_img_decoder_t *, const void *src, lv_img_header_t *header) {
  if (!is_png_file(src)) return LV_RES_INV;

  // Signature (8), IHDR length and type (8), then width and height, big endian.
  uint8_t buf[24];
  lv_fs_file_t f;
  if (lv_fs_open(&f, (const char *)src, LV_FS_MODE_RD) != LV_FS_RES_OK) return LV_RES_INV;
  uint32_t rn = 0;
  lv_fs_read(&f, buf, sizeof(buf), &rn);
  lv_fs_close(&f);
  if (rn != sizeof(buf) || memcmp(buf + 1, "PNG", 3) != 0) return LV_RES_INV;

  uint32_t w = (buf[16] << 24) | (buf[17] << 16) | (buf[18] << 8) | buf[19];
  uint32_t h = (buf[20] << 24) | (buf[21] << 16) | (buf[22] << 8) | buf[23];
  if (w == 0 || h == 0 || w > 2047 || h > 2047) return LV_RES_INV;  // lv_img_header_t limits
  if (w * h < MIN_PIXELS) return LV_RES_INV;  // LVGL's decoder: true colour

  header->always_zero = 0;
  header->cf = LV_IMG_CF_INDEXED_8BIT;
  header->w = w;
  header->h = h;
  return LV_RES_OK;
}

lv_res_t decoder_open(lv_img_decoder_t *, lv_img_decoder_dsc_t *dsc) {
  if (!is_png_file(dsc->src)) return LV_RES_INV;
  auto t0 = std::chrono::steady_clock::now();

  unsigned char *png = NULL;
  size_t png_size = 0;
  unsigned error = lodepng_load_file(&png, &png_size, (const char *)dsc->src);
  if (error) {
    // lodepng_load_file allocates before reading, e.g. a file still being written.
    if (png) lv_mem_free(png);
    spdlog::warn("png: cannot load {}: {}", (const char *)dsc->src, lodepng_error_text(error));
    return LV_RES_INV;
  }

  unsigned char *rgba = NULL;
  unsigned w = 0, h = 0;
  error = lodepng_decode32(&rgba, &w, &h, png, png_size);
  lv_mem_free(png);
  if (error) {
    if (rgba) lv_mem_free(rgba);
    spdlog::warn("png: cannot decode {}: {}", (const char *)dsc->src, lodepng_error_text(error));
    return LV_RES_INV;
  }

  uint32_t px = w * h;
  uint8_t *out = (uint8_t *)lv_mem_alloc(PALETTE_BYTES + px);
  if (out == NULL) {
    lv_mem_free(rgba);
    return LV_RES_INV;
  }
  lv_color32_t *palette = (lv_color32_t *)out;
  memset(palette, 0, PALETTE_BYTES);
  uint32_t colors = quantize(rgba, px, palette, out + PALETTE_BYTES);
  lv_mem_free(rgba);

  dsc->header.cf = LV_IMG_CF_INDEXED_8BIT;
  dsc->header.w = w;
  dsc->header.h = h;
  dsc->img_data = out;

  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  spdlog::info("png: {} {}x{} -> {} colours, {} bytes, {} ms", (const char *)dsc->src, w, h, colors,
                PALETTE_BYTES + px, ms);
  return LV_RES_OK;
}

void decoder_close(lv_img_decoder_t *, lv_img_decoder_dsc_t *dsc) {
  if (dsc->img_data) {
    lv_mem_free((void *)dsc->img_data);
    dsc->img_data = NULL;
  }
}

}  // namespace

uint32_t quantize(const uint8_t *rgba, uint32_t px_count, lv_color32_t *palette, uint8_t *indices) {
  uint32_t count;
  if (exact_palette(rgba, px_count, palette, indices, count)) return count;
  return median_cut(rgba, px_count, palette, indices);
}

void init() {
  lv_img_decoder_t *dec = lv_img_decoder_create();
  lv_img_decoder_set_info_cb(dec, decoder_info);
  lv_img_decoder_set_open_cb(dec, decoder_open);
  lv_img_decoder_set_close_cb(dec, decoder_close);
}

}  // namespace PngDecoder
