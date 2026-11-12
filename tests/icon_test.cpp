// Built-in icons (assets/material/*.c) are 8-bit indexed (palette + one
// byte per pixel, 5 KB instead of 16 KB of ARGB8888). They must open
// without per-line decoding (patches/0007), and draw exactly like the same
// pixels in true colour: plain, zoomed (sensor icons) and recoloured
// (pressed and disabled buttons).

#include "doctest/doctest.h"

#include "app.h"
#include "fake_hw.h"
#include "fake_moonraker.h"

#include "guppyscreen.h"

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>

LV_IMG_DECLARE(emergency);  // red, 168 colours
LV_IMG_DECLARE(print);      // white glyph, 43 alpha levels
LV_IMG_DECLARE(fan);

namespace {

// The same pixels as LV_IMG_CF_TRUE_COLOR_ALPHA (B, G, R, A bytes).
std::vector<uint8_t> to_argb(const lv_img_dsc_t &icon) {
  const uint8_t *pal = icon.data;
  const uint8_t *idx = icon.data + 1024;
  std::vector<uint8_t> argb(icon.header.w * icon.header.h * 4);
  for (size_t i = 0; i < argb.size() / 4; i++) std::copy(pal + idx[i] * 4, pal + idx[i] * 4 + 4, &argb[i * 4]);
  return argb;
}

}  // namespace

TEST_CASE("indexed icons draw like true colour when zoomed and recoloured") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  lv_obj_t *img;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_t *bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(bg, 800, 480);
    lv_obj_set_pos(bg, 0, 0);
    lv_obj_set_style_bg_color(bg, lv_color_make(40, 43, 48), 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_set_style_border_width(bg, 0, 0);
    img = lv_img_create(lv_layer_top());
  }
  auto render = [&](const void *src, uint16_t zoom, lv_opa_t recolor_opa, bool aa) {
    {
      std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
      lv_img_set_src(img, src);
      lv_img_set_zoom(img, zoom);
      lv_img_set_antialias(img, aa);
      lv_obj_set_style_img_recolor(img, lv_palette_main(LV_PALETTE_BLUE), 0);
      lv_obj_set_style_img_recolor_opa(img, recolor_opa, 0);
      lv_obj_center(img);
      lv_obj_invalidate(lv_layer_top());
    }
    app::redraw();
    std::vector<lv_color_t> screen;
    for (int y = 240 - 80; y < 240 + 80; y++)
      for (int x = 400 - 80; x < 400 + 80; x++) screen.push_back(fake_hw::pixel(x, y));
    return screen;
  };

  for (const lv_img_dsc_t *icon : {&emergency, &print, &fan}) {
    CAPTURE(icon->data_size);
    REQUIRE((int)icon->header.cf == (int)LV_IMG_CF_INDEXED_8BIT);
    REQUIRE(icon->data_size == 1024 + icon->header.w * icon->header.h);

    {
      std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
      lv_img_decoder_dsc_t dsc;
      REQUIRE(lv_img_decoder_open(&dsc, icon, lv_color_white(), 0) == LV_RES_OK);
      CHECK(dsc.img_data == icon->data);  // drawn whole, not line by line
      lv_img_decoder_close(&dsc);
    }

    std::vector<uint8_t> argb = to_argb(*icon);
    lv_img_dsc_t ref{};
    ref.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    ref.header.w = icon->header.w;
    ref.header.h = icon->header.h;
    ref.data_size = argb.size();
    ref.data = argb.data();

    struct Case { uint16_t zoom; lv_opa_t recolor; bool aa; };
    for (Case c : {Case{256, 0, true}, Case{256, LV_OPA_COVER, true}, Case{256, LV_OPA_30, true},
                   Case{200, 0, false}, Case{200, LV_OPA_COVER, false}, Case{200, 0, true}, Case{200, LV_OPA_COVER, true}}) {
      CAPTURE(c.zoom);
      CAPTURE((int)c.recolor);
      CAPTURE(c.aa);
      auto a = render(icon, c.zoom, c.recolor, c.aa);
      auto b = render(&ref, c.zoom, c.recolor, c.aa);
      int diff = 0, max_d = 0, drawn = 0;
      double sum = 0;
      for (size_t i = 0; i < a.size(); i++) {
        int d = std::max({std::abs(a[i].ch.red - b[i].ch.red), std::abs(a[i].ch.green - b[i].ch.green),
                          std::abs(a[i].ch.blue - b[i].ch.blue)});
        diff += d != 0;
        sum += d;
        max_d = std::max(max_d, d);
        drawn += a[i].full != lv_color_make(40, 43, 48).full;
      }
      CHECK(drawn > 200);  // the icon is drawn at all
      double mean = sum / drawn;
      MESSAGE("zoom " << c.zoom << " recolor " << (int)c.recolor << std::string(c.aa ? " aa" : "") << ": " << diff << " of "
                      << drawn << " pixels differ, max " << max_d << ", mean " << mean);
      if (c.zoom == 256 || !c.aa) {
        CHECK(diff == 0);
      } else {
        // Zoomed with antialiasing, LVGL's ARGB sampler and patch 0006's
        // bilinear indexed sampler blend the edges differently.
        CHECK(mean < 8.0);
      }
    }
  }
}
