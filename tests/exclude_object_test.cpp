// Exclude-object panel during a print: the object map is drawn on a canvas,
// and tapping an object sends EXCLUDE_OBJECT.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <mutex>

namespace {

bool contains(const std::vector<std::string> &v, const std::string &s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

json square(float x0, float y0, float x1, float y1) {
  return json::array({{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}});
}

// ad5m_basic.json, but printing a file with two objects. The bed is
// -3..220 (toolhead axis limits), so CENTER_CUBE covers the middle of the map.
void start_printing(FakeMoonraker &moonraker) {
  std::ifstream f(app::fixture("ad5m_basic.json"));
  json fixture = json::parse(f);
  json sub = fixture["printer.objects.subscribe"];
  sub["status"]["print_stats"]["state"] = "printing";
  sub["status"]["print_stats"]["filename"] = "cubes.gcode";
  sub["status"]["exclude_object"] = {
    {"objects", json::array({
      {{"name", "CENTER_CUBE"}, {"center", {108.5, 108.5}}, {"polygon", square(88, 88, 128, 128)}},
      {{"name", "CORNER_CUBE"}, {"center", {30, 30}}, {"polygon", square(20, 20, 40, 40)}},
    })},
    {"excluded_objects", json::array()},
    {"current_object", "CENTER_CUBE"},
  };
  moonraker.set_result("printer.objects.subscribe", sub);
  app::start(moonraker);
  app::wait_ready(moonraker);
}

struct Pixels {
  int green = 0;
  int red = 0;
};

// Count the object outline colours drawn on the canvas.
Pixels count_outline_pixels(lv_obj_t *canvas) {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  Pixels n;
  lv_img_dsc_t *img = lv_canvas_get_img(canvas);
  lv_color_t green = lv_palette_main(LV_PALETTE_GREEN);
  lv_color_t red = lv_palette_main(LV_PALETTE_RED);
  auto same = [](lv_color_t a, lv_color_t b) {
    return a.ch.red == b.ch.red && a.ch.green == b.ch.green && a.ch.blue == b.ch.blue;
  };
  for (lv_coord_t y = 0; y < (lv_coord_t)img->header.h; y++) {
    for (lv_coord_t x = 0; x < (lv_coord_t)img->header.w; x++) {
      lv_color_t c = lv_canvas_get_px(canvas, x, y);
      if (same(c, green)) n.green++;
      else if (same(c, red)) n.red++;
    }
  }
  return n;
}

// The print status screen opens the exclude panel when the thumbnail's
// column (the file name over the thumbnail) is tapped.
void open_exclude_panel() {
  lv_obj_t *name = ui::find_label("cubes.gcode");
  REQUIRE_MESSAGE(name != nullptr, "print status not shown; screen: " << ui::dump_screen());
  lv_point_t p;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_t *thumbnail_cont = lv_obj_get_parent(name);
    lv_obj_update_layout(lv_scr_act());
    lv_area_t a;
    lv_obj_get_coords(thumbnail_cont, &a);
    p = {(lv_coord_t)((a.x1 + a.x2) / 2), (lv_coord_t)((a.y1 + a.y2) / 2)};
  }
  ui::tap_at(p.x, p.y);
}

}  // namespace

TEST_CASE("exclude object map excludes the tapped object") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_printing(moonraker);

  // Open, close and reopen: the map must still be drawn after the panel
  // has been closed once.
  for (int round = 0; round < 2; round++) {
    CAPTURE(round);
    open_exclude_panel();
    lv_obj_t *canvas = nullptr;
    REQUIRE_MESSAGE(app::wait_until([&] { return (canvas = ui::find_shown(&lv_canvas_class)) != nullptr; }),
                    "object map not shown; screen: " << ui::dump_screen());

    Pixels px = count_outline_pixels(canvas);
    CHECK_MESSAGE(px.green > 100, "green outline pixels: " << px.green);

    if (round == 0) {
      ui::tap(canvas);  // the centre of the map is inside CENTER_CUBE
      bool sent = app::wait_until([&] { return contains(moonraker.gcodes(), "EXCLUDE_OBJECT NAME=CENTER_CUBE"); });
      CHECK_MESSAGE(sent, "G-codes sent: " << ui::join(moonraker.gcodes()));

      // The outline turns red when Klipper's exclude_object update arrives.
      app::wait_until([&] { return count_outline_pixels(canvas).red > 100; });
      px = count_outline_pixels(canvas);
      CHECK_MESSAGE(px.red > 100, "red outline pixels after exclude: " << px.red);
    }

    ui::tap_tab(Nav::PRINT);  // back to the print screen
    REQUIRE(app::wait_until([] { return ui::find_shown(&lv_canvas_class) == nullptr; }));
  }
}

TEST_CASE("leaving the exclude map through the bar frees it") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_printing(moonraker);

  open_exclude_panel();
  lv_obj_t *canvas = nullptr;
  REQUIRE(app::wait_until([&] { return (canvas = ui::find_shown(&lv_canvas_class)) != nullptr; }));
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    REQUIRE((int)lv_canvas_get_img(canvas)->header.w > 1);  // drawn
  }
  ui::tap_tab(Nav::HOME);
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  CHECK((int)lv_canvas_get_img(canvas)->header.w == 1);  // the 1x1 idle buffer
}

TEST_CASE("exclude map buttons do not cover the map") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_printing(moonraker);

  open_exclude_panel();
  lv_obj_t *canvas = nullptr;
  REQUIRE(app::wait_until([&] { return (canvas = ui::find_shown(&lv_canvas_class)) != nullptr; }));
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_update_layout(lv_scr_act());
  lv_area_t map;
  lv_obj_get_coords(canvas, &map);
  CHECK(map.x2 <= 799);
  lv_obj_t *cont = lv_obj_get_parent(canvas);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(cont); i++) {
    lv_obj_t *btn = lv_obj_get_child(cont, i);
    if (!lv_obj_check_type(btn, &lv_btn_class) || lv_obj_has_flag(btn, LV_OBJ_FLAG_HIDDEN)) continue;
    lv_area_t b;
    lv_obj_get_coords(btn, &b);
    bool overlaps = b.x1 <= map.x2 && b.x2 >= map.x1 && b.y1 <= map.y2 && b.y2 >= map.y1;
    CHECK_MESSAGE(!overlaps, "button " << b.x1 << "," << b.y1 << " over the map " << map.x1 << ".." << map.x2);
  }
}
