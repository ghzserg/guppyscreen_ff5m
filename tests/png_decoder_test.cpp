// PNGs are kept as 8-bit indexed images (at most 256 colours) and drawn by
// LVGL with patches/0006: unzoomed, zoomed, with and without antialiasing.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_hw.h"
#include "fake_moonraker.h"
#include "png_writer.h"
#include "ui.h"

#include "guppyscreen.h"
#include "png_decoder.h"

#include <cmath>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

#ifdef __SANITIZE_ADDRESS__
extern "C" size_t __sanitizer_get_current_allocated_bytes(void);  // ASan runtime
#endif

namespace {

std::string temp_png_path(const char *name) {
  const char *tmp = getenv("TMPDIR");
  std::string dir = std::string(tmp && *tmp ? tmp : "/tmp") + "/guppy-png-XXXXXX";
  REQUIRE(mkdtemp(dir.data()) != NULL);
  return dir + "/" + name;
}

// 64x48: 8x6 blocks of 8x8 pixels in different colours. The first column of
// blocks is fully transparent, the second half-transparent. 43 colours
// (7 x 6 + transparent): an exact palette.
std::vector<unsigned char> blocks_image(int w, int h) {
  std::vector<unsigned char> px(w * h * 4);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      int bx = x / 8, by = y / 8;
      unsigned char *p = &px[(y * w + x) * 4];
      p[0] = (bx * 37 + by * 11) & 0xff;
      p[1] = (bx * 13 + by * 71) & 0xff;
      p[2] = (bx * 91 + by * 29) & 0xff;
      p[3] = bx == 0 ? 0 : bx == 1 ? 128 : 255;
    }
  }
  return px;
}

// Smooth gradient, ~32k colours.
std::vector<unsigned char> gradient_image(int w, int h) {
  std::vector<unsigned char> px(w * h * 4);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      unsigned char *p = &px[(y * w + x) * 4];
      p[0] = x;
      p[1] = y * 2;
      p[2] = (x + y) / 2;
      p[3] = 255;
    }
  }
  return px;
}

}  // namespace

TEST_CASE("quantize keeps up to 256 colours exactly") {
  const int w = 64, h = 48;
  auto px = blocks_image(w, h);
  std::vector<lv_color32_t> palette(256);
  std::vector<uint8_t> idx(w * h);
  uint32_t n = PngDecoder::quantize(px.data(), w * h, palette.data(), idx.data());
  CHECK(n == 43);
  int wrong = 0;
  for (int i = 0; i < w * h; i++) {
    const unsigned char *p = &px[i * 4];
    lv_color32_t c = palette[idx[i]];
    if (p[3] == 0) {
      wrong += c.ch.alpha != 0;
    } else {
      wrong += c.ch.red != p[0] || c.ch.green != p[1] || c.ch.blue != p[2] || c.ch.alpha != p[3];
    }
  }
  CHECK(wrong == 0);
}

TEST_CASE("quantize reduces a gradient to 256 colours with a small error") {
  const int w = 256, h = 128;
  auto px = gradient_image(w, h);
  std::vector<lv_color32_t> palette(256);
  std::vector<uint8_t> idx(w * h);
  uint32_t n = PngDecoder::quantize(px.data(), w * h, palette.data(), idx.data());
  CHECK(n <= 256);
  double sum = 0;
  int max_err = 0;
  for (int i = 0; i < w * h; i++) {
    const unsigned char *p = &px[i * 4];
    lv_color32_t c = palette[idx[i]];
    int e[3] = {std::abs(c.ch.red - p[0]), std::abs(c.ch.green - p[1]), std::abs(c.ch.blue - p[2])};
    for (int k = 0; k < 3; k++) {
      sum += e[k];
      max_err = std::max(max_err, e[k]);
    }
    CHECK(c.ch.alpha == 255);
  }
  double mean = sum / (w * h * 3);
  MESSAGE("gradient: " << n << " colours, mean error " << mean << ", max " << max_err << " per channel");
  CHECK(mean < 6.0);
  CHECK(max_err < 32);
}

TEST_CASE("PNG is decoded to 8-bit indexed and drawn like the true-colour image") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  const int w = 64, h = 48;
  auto px = blocks_image(w, h);
  std::string path = temp_png_path("blocks.png");
  test_png::write(path, px, w, h);
  std::string src = "A:" + path;

  // Decoded format and size: 1 KB palette + 1 byte per pixel.
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
#ifdef __SANITIZE_ADDRESS__
    size_t before = __sanitizer_get_current_allocated_bytes();
#endif
    lv_img_decoder_dsc_t dsc;
    REQUIRE(lv_img_decoder_open(&dsc, src.c_str(), lv_color_white(), 0) == LV_RES_OK);
    CHECK((int)dsc.header.cf == (int)LV_IMG_CF_INDEXED_8BIT);
    CHECK((int)dsc.header.w == w);
    CHECK((int)dsc.header.h == h);
    REQUIRE(dsc.img_data != nullptr);
#ifdef __SANITIZE_ADDRESS__
    long held = (long)__sanitizer_get_current_allocated_bytes() - (long)before;
    MESSAGE("decoded " << w << "x" << h << " PNG holds " << held << " bytes (ARGB8888 would be " << w * h * 4 << ")");
    CHECK(held >= 1024 + w * h);
    CHECK(held < 1024 + w * h + 512);  // + the copy of the file name
#endif
    lv_img_decoder_close(&dsc);
  }

  // The same pixels as an LVGL true-colour image (B, G, R, A bytes).
  std::vector<uint8_t> argb(w * h * 4);
  for (int i = 0; i < w * h; i++) {
    argb[i * 4 + 0] = px[i * 4 + 2];
    argb[i * 4 + 1] = px[i * 4 + 1];
    argb[i * 4 + 2] = px[i * 4 + 0];
    argb[i * 4 + 3] = px[i * 4 + 3];
  }
  lv_img_dsc_t ref{};
  ref.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
  ref.header.w = w;
  ref.header.h = h;
  ref.data_size = argb.size();
  ref.data = argb.data();

  // Both images over the same opaque background, side by side.
  lv_obj_t *bg, *img_png, *img_ref;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(bg, 800, 480);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, lv_color_make(30, 200, 90), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    img_png = lv_img_create(lv_layer_top());
    lv_img_set_src(img_png, src.c_str());
    lv_obj_set_pos(img_png, 200, 200);
    img_ref = lv_img_create(lv_layer_top());
    lv_img_set_src(img_ref, &ref);
    lv_obj_set_pos(img_ref, 500, 200);
  }

  struct Case { uint16_t zoom; bool aa; };
  for (Case c : {Case{256, false}, Case{95, false}, Case{384, false}, Case{95, true}, Case{384, true}}) {
    CAPTURE(c.zoom);
    CAPTURE(c.aa);
    {
      std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
      for (lv_obj_t *o : {img_png, img_ref}) {
        lv_img_set_zoom(o, c.zoom);
        lv_img_set_antialias(o, c.aa);
      }
      lv_obj_invalidate(lv_layer_top());
    }
    app::pump(100);

    // Compare the area the zoomed image can cover around each object's centre.
    int half_w = w * c.zoom / 256 / 2 + 2, half_h = h * c.zoom / 256 / 2 + 2;
    int diff_px = 0, max_diff = 0;
    double sum = 0;
    for (int dy = -half_h; dy <= half_h; dy++) {
      for (int dx = -half_w; dx <= half_w; dx++) {
        lv_color_t a = fake_hw::pixel(200 + w / 2 + dx, 200 + h / 2 + dy);
        lv_color_t b = fake_hw::pixel(500 + w / 2 + dx, 200 + h / 2 + dy);
        int d = std::max({std::abs(a.ch.red - b.ch.red), std::abs(a.ch.green - b.ch.green),
                          std::abs(a.ch.blue - b.ch.blue)});
        diff_px += d != 0;
        max_diff = std::max(max_diff, d);
        sum += d;
      }
    }
    double mean = sum / ((2 * half_w + 1) * (2 * half_h + 1));
    MESSAGE("zoom " << c.zoom << std::string(c.aa ? " aa" : "") << ": " << diff_px << " pixels differ, max " << max_diff
                    << ", mean " << mean);
    // The images are drawn at all (not just background).
    CHECK(fake_hw::pixel(200 + w / 2, 200 + h / 2).full != lv_color_make(30, 200, 90).full);
    if (!c.aa) {
      CHECK(diff_px == 0);
    } else {
      // LVGL's own ARGB antialiasing and the indexed bilinear sampler blend
      // the block edges differently; elsewhere they agree.
      CHECK(mean < 5.0);
    }
  }
}

TEST_CASE("opaque indexed PNG stays opaque when drawn zoomed with antialiasing") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  const int w = 512, h = 512;
  std::vector<unsigned char> px(w * h * 4);
  for (int i = 0; i < w * h; i++) {
    px[i * 4 + 0] = 200;
    px[i * 4 + 1] = 30;
    px[i * 4 + 2] = 30;
    px[i * 4 + 3] = 255;
  }
  std::string path = temp_png_path("opaque.png");
  test_png::write(path, px, w, h);
  std::string src = "A:" + path;

  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_t *bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(bg, 800, 480);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, lv_color_make(30, 200, 90), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    lv_obj_t *img = lv_img_create(lv_layer_top());
    lv_img_set_src(img, src.c_str());
    lv_img_set_zoom(img, 95);  // as the input-shaper graphs
    lv_img_set_antialias(img, true);
    lv_obj_center(img);
  }
  app::pump(100);

  // Inside the zoomed image (190x190 around the centre), away from its edges.
  int wrong = 0;
  lv_color_t first_wrong{};
  for (int y = 240 - 80; y <= 240 + 80; y++) {
    for (int x = 400 - 80; x <= 400 + 80; x++) {
      lv_color_t c = fake_hw::pixel(x, y);
      if (c.ch.red != 200 || c.ch.green != 30 || c.ch.blue != 30) {
        if (!wrong) first_wrong = c;
        wrong++;
      }
    }
  }
  CHECK_MESSAGE(wrong == 0, wrong << " pixels show background, e.g. " << (int)first_wrong.ch.red << ","
                                  << (int)first_wrong.ch.green << "," << (int)first_wrong.ch.blue);
}
