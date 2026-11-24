// Exclude-object panel during a print: the object map is drawn on a canvas,
// and tapping an object sends EXCLUDE_OBJECT.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "fake_hw.h"
#include "ui.h"

#include "guppyscreen.h"
#include "config.h"

#include <algorithm>
#include <cstring>
#include <cmath>
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
void start_printing(FakeMoonraker &moonraker, const json &config_patch = json::object()) {
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
  app::start(moonraker, config_patch);
  app::wait_ready(moonraker);
}

void touch_frame(int x, int y, bool pressed) {
  fake_hw::touch_set(x, y, pressed);
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  for (lv_indev_t *i = lv_indev_get_next(NULL); i != NULL; i = lv_indev_get_next(i))
    lv_timer_ready(i->driver->read_timer);
  lv_timer_handler();
}

int bed_left(lv_obj_t *canvas) {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  const lv_img_dsc_t *img = lv_canvas_get_img(canvas);
  uint32_t border = lv_color_to32(lv_color_hex(0x71889B));
  for (int x = 0; x < (int)img->header.w; ++x)
    for (int y = 0; y < (int)img->header.h; ++y)
      if (lv_color_to32(lv_canvas_get_px(canvas, x, y)) == border) return x;
  return -1;
}

void answer_confirmation(bool confirm) {
  lv_obj_t *btns = ui::find_shown(&lv_btnmatrix_class);
  REQUIRE_MESSAGE(btns != nullptr, ui::dump_screen());
  lv_area_t a = ui::coords(btns);
  ui::tap_at(a.x1 + lv_area_get_width(&a) * (confirm ? 3 : 1) / 4,
             (a.y1 + a.y2) / 2);
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

// Проверяем экран исключения объектов: имя файла теперь открывает каталог.
void open_exclude_panel() {
  lv_obj_t *name = ui::find_label("cubes.gcode");
  REQUIRE_MESSAGE(name != nullptr, "print status not shown; screen: " << ui::dump_screen());
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_t *thumbnail_cont = lv_obj_get_parent(name);
    lv_event_send(thumbnail_cont, LV_EVENT_SHORT_CLICKED, nullptr);
  }
  app::pump(20);
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

    {
      std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
      lv_obj_t *frame = lv_obj_get_parent(canvas);
      CHECK(lv_obj_get_style_border_width(frame, 0) == 2);
      CHECK(lv_obj_get_width(frame) == lv_obj_get_width(canvas) + 8);
    }
    Pixels px = count_outline_pixels(canvas);
    CHECK_MESSAGE(px.green > 100, "green outline pixels: " << px.green);

    if (round == 0) {
      ui::tap(canvas);  // the centre of the map is inside CENTER_CUBE
      REQUIRE(app::wait_until([] { return ui::find_shown(&lv_btnmatrix_class) != nullptr; }));
      CHECK_FALSE(contains(moonraker.gcodes(), "EXCLUDE_OBJECT NAME=CENTER_CUBE"));
      CHECK(count_outline_pixels(canvas).red == 0);
      answer_confirmation(false);
      CHECK_FALSE(contains(moonraker.gcodes(), "EXCLUDE_OBJECT NAME=CENTER_CUBE"));
      ui::tap(canvas);
      answer_confirmation(true);
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

TEST_CASE("exclude map shows a square bed with vertical zoom controls") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_printing(moonraker);
  open_exclude_panel();
  lv_obj_t *canvas = ui::find_shown(&lv_canvas_class);
  REQUIRE(canvas != nullptr);

  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_color_t border = lv_color_hex(0x71889B);
    int left = 10000, top = 10000, right = -1, bottom = -1;
    const auto *img = lv_canvas_get_img(canvas);
    for (int y = 0; y < (int)img->header.h; ++y)
      for (int x = 0; x < (int)img->header.w; ++x)
        if (lv_color_to32(lv_canvas_get_px(canvas, x, y)) == lv_color_to32(border)) {
          left = std::min(left, x); right = std::max(right, x);
          top = std::min(top, y); bottom = std::max(bottom, y);
        }
    CHECK(right - left > 300);
    CHECK(std::abs((right - left) - (bottom - top)) <= 2);
  }

  lv_obj_t *plus = ui::find_label("+");
  lv_obj_t *minus = ui::find_label("-");
  REQUIRE(plus != nullptr);
  REQUIRE(minus != nullptr);
  CHECK(ui::find_label(LV_SYMBOL_LEFT) == nullptr);
  CHECK(ui::find_label(LV_SYMBOL_RIGHT) == nullptr);
  CHECK(ui::find_label(LV_SYMBOL_UP) == nullptr);
  CHECK(ui::find_label(LV_SYMBOL_DOWN) == nullptr);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_area_t map_area, plus_area, minus_area;
    lv_obj_get_coords(lv_obj_get_parent(canvas), &map_area);
    lv_obj_get_coords(lv_obj_get_parent(plus), &plus_area);
    lv_obj_get_coords(lv_obj_get_parent(minus), &minus_area);
    CHECK(lv_obj_get_width(canvas) > 600);
    CHECK(lv_obj_get_height(canvas) > 400);
    CHECK(map_area.x2 < plus_area.x1);
    CHECK(map_area.x2 < minus_area.x1);
    CHECK(plus_area.y2 < minus_area.y1);
    CHECK(lv_obj_get_style_text_font(plus, 0) == &lv_font_montserrat_48);
  }
  ui::tap_label("+");
  CHECK(count_outline_pixels(canvas).green > 100);
  ui::tap_label("-");
  CHECK(count_outline_pixels(canvas).green > 100);
}

TEST_CASE("dragging the zoomed exclusion map pans without excluding objects") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_printing(moonraker, {{"prompt_exclude_object", false}});
  open_exclude_panel();
  lv_obj_t *canvas = ui::find_shown(&lv_canvas_class);
  REQUIRE(canvas != nullptr);

  ui::tap_label("+");
  int before = bed_left(canvas);
  REQUIRE(before >= 0);
  lv_area_t a = ui::coords(canvas);
  int x = (a.x1 + a.x2) / 2, y = (a.y1 + a.y2) / 2;

  touch_frame(x, y, true);
  for (int dx : {3, 12, 30, 45}) touch_frame(x + dx, y, true);
  touch_frame(x + 45, y, false);
  app::redraw();

  CHECK(bed_left(canvas) > before + 15);
  CHECK_FALSE(contains(moonraker.gcodes(), "EXCLUDE_OBJECT NAME=CENTER_CUBE"));
  CHECK(ui::find_shown(&lv_btnmatrix_class) == nullptr);

  ui::tap_at(x + 45, y);
  CHECK(app::wait_until([&] {
    return contains(moonraker.gcodes(), "EXCLUDE_OBJECT NAME=CENTER_CUBE");
  }));
}

TEST_CASE("object updates preserve exclusions until Klipper clears them") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_printing(moonraker);
  open_exclude_panel();
  auto *canvas = ui::find_shown(&lv_canvas_class);
  REQUIRE(canvas != nullptr);

  moonraker.push_status({{"exclude_object", {{"excluded_objects", {"CENTER_CUBE"}}}}});
  REQUIRE(app::wait_until([&] { return count_outline_pixels(canvas).red > 100; }));
  const auto initial = count_outline_pixels(canvas);

  const auto objects = json::array({
    {{"name", "CENTER_CUBE"}, {"polygon", square(65, 65, 155, 155)}},
    {{"name", "CORNER_CUBE"}, {"polygon", square(20, 20, 40, 40)}},
  });
  moonraker.push_status({{"exclude_object", {{"objects", objects}}}});
  REQUIRE(app::wait_until([&] {
    const auto refreshed = count_outline_pixels(canvas);
    return refreshed.green + refreshed.red > initial.green + initial.red + 100;
  }));
  const auto refreshed = count_outline_pixels(canvas);
  CHECK(refreshed.red > initial.red);
  CHECK(refreshed.green == initial.green);

  moonraker.push_status({{"exclude_object", {{"objects", objects}, {"excluded_objects", json::array()}}}});
  REQUIRE(app::wait_until([&] {
    const auto cleared = count_outline_pixels(canvas);
    return cleared.red == 0 && cleared.green == refreshed.green + refreshed.red;
  }));
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
  lv_obj_t *cont = lv_obj_get_parent(lv_obj_get_parent(canvas));
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(cont); i++) {
    lv_obj_t *btn = lv_obj_get_child(cont, i);
    if (!lv_obj_check_type(btn, &lv_btn_class) || lv_obj_has_flag(btn, LV_OBJ_FLAG_HIDDEN)) continue;
    lv_area_t b;
    lv_obj_get_coords(btn, &b);
    bool overlaps = b.x1 <= map.x2 && b.x2 >= map.x1 && b.y1 <= map.y2 && b.y2 >= map.y1;
    CHECK_MESSAGE(!overlaps, "button " << b.x1 << "," << b.y1 << " over the map " << map.x1 << ".." << map.x2);
  }
}

// Синяя рамка обозначает наличие объектов, доступных для исключения.
TEST_CASE("print thumbnail is outlined when objects can be excluded") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_printing(moonraker);

  lv_obj_t *name = ui::find_label("cubes.gcode");
  REQUIRE(name != nullptr);
  lv_obj_t *thumbnail;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    thumbnail = ui::find_in(lv_obj_get_parent(name), &lv_img_class);
    REQUIRE(thumbnail != nullptr);
    CHECK(lv_obj_get_style_outline_width(thumbnail, 0) == 1);
  }

  moonraker.push_status({{"exclude_object", {{"objects", json::array()}}}});
  app::settle(moonraker);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK(lv_obj_get_style_outline_width(thumbnail, 0) == 0);
  }

  moonraker.push_status({{"exclude_object", {{"objects", json::array({{{"name", "CENTER_CUBE"}}})}}}});
  app::settle(moonraker);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK(lv_obj_get_style_outline_width(thumbnail, 0) == 1);
  }

  moonraker.push_status({{"exclude_object", {{"excluded_objects", {{"CENTER_CUBE"}}}}}});
  app::settle(moonraker);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK(lv_obj_get_style_outline_width(thumbnail, 0) == 1);
  }

  moonraker.push_status({{"print_stats", {{"state", "complete"}}}});
  app::settle(moonraker);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK(lv_obj_get_style_outline_width(thumbnail, 0) == 0);
  }
}

TEST_CASE("the exclude prompt of an object without a polygon is in English without a Russian translation") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  std::ifstream f(app::fixture("ad5m_basic.json"));
  json sub = json::parse(f)["printer.objects.subscribe"];
  sub["status"]["print_stats"]["state"] = "printing";
  sub["status"]["print_stats"]["filename"] = "cubes.gcode";
  sub["status"]["exclude_object"] = {
    {"objects", json::array({{{"name", "CENTER_CUBE"}}})},  // без polygon: список переключателей
    {"excluded_objects", json::array()},
    {"current_object", "CENTER_CUBE"},
  };
  moonraker.set_result("printer.objects.subscribe", sub);
  app::start(moonraker);
  app::wait_ready(moonraker);
  open_exclude_panel();

  lv_obj_t *name = nullptr;
  REQUIRE_MESSAGE(app::wait_until([&] { return (name = ui::find_label("CENTER_CUBE")) != nullptr; }), ui::dump_screen());
  app::settle(moonraker);
  ui::tap(name);
  bool shown = app::wait_until([] { return ui::find_label("Exclude Object?") != nullptr; });
  std::string screen = ui::dump_screen();
  CHECK_MESSAGE(shown, screen);
  lv_obj_t *btns = ui::find_shown(&lv_btnmatrix_class);
  REQUIRE_MESSAGE(btns != nullptr, screen);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK(std::string(lv_btnmatrix_get_btn_text(btns, 0)) == "Cancel");
    CHECK(std::string(lv_btnmatrix_get_btn_text(btns, 1)) == "Exclude");
  }
  CHECK_MESSAGE(screen.find("Исключить") == std::string::npos, screen);
}

TEST_CASE("graphic exclusion skips confirmation when setting is off") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start_printing(moonraker, {{"prompt_exclude_object", false}});
  open_exclude_panel();
  lv_obj_t *canvas = ui::find_shown(&lv_canvas_class);
  REQUIRE(canvas != nullptr);
  ui::tap(canvas);
  CHECK(ui::find_shown(&lv_btnmatrix_class) == nullptr);
  CHECK(app::wait_until([&] { return contains(moonraker.gcodes(), "EXCLUDE_OBJECT NAME=CENTER_CUBE"); }));
}

TEST_CASE("text exclusion cancel does not send gcode") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  std::ifstream f(app::fixture("ad5m_basic.json"));
  json sub = json::parse(f)["printer.objects.subscribe"];
  sub["status"]["print_stats"]["state"] = "printing";
  sub["status"]["print_stats"]["filename"] = "cubes.gcode";
  sub["status"]["exclude_object"] = {
    {"objects", json::array({{{"name", "CENTER_CUBE"}}})},
    {"excluded_objects", json::array()}, {"current_object", "CENTER_CUBE"}
  };
  moonraker.set_result("printer.objects.subscribe", sub);
  app::start(moonraker);
  app::wait_ready(moonraker);
  open_exclude_panel();
  ui::tap_label("CENTER_CUBE");
  REQUIRE(app::wait_until([] { return ui::find_shown(&lv_btnmatrix_class) != nullptr; }));
  CHECK_FALSE(contains(moonraker.gcodes(), "EXCLUDE_OBJECT NAME=CENTER_CUBE"));
  answer_confirmation(false);
  CHECK_FALSE(contains(moonraker.gcodes(), "EXCLUDE_OBJECT NAME=CENTER_CUBE"));
  ui::tap_label("CENTER_CUBE");
  REQUIRE(app::wait_until([] { return ui::find_shown(&lv_btnmatrix_class) != nullptr; }));
  answer_confirmation(true);
  CHECK(app::wait_until([&] { return contains(moonraker.gcodes(), "EXCLUDE_OBJECT NAME=CENTER_CUBE"); }));
}

TEST_CASE("text exclusion skips confirmation when setting is off") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  std::ifstream f(app::fixture("ad5m_basic.json"));
  json sub = json::parse(f)["printer.objects.subscribe"];
  sub["status"]["print_stats"]["state"] = "printing";
  sub["status"]["print_stats"]["filename"] = "cubes.gcode";
  sub["status"]["exclude_object"] = {
    {"objects", json::array({{{"name", "CENTER_CUBE"}}})},
    {"excluded_objects", json::array()}, {"current_object", "CENTER_CUBE"}
  };
  moonraker.set_result("printer.objects.subscribe", sub);
  app::start(moonraker, {{"prompt_exclude_object", false}});
  app::wait_ready(moonraker);
  open_exclude_panel();
  ui::tap_label("CENTER_CUBE");
  CHECK(ui::find_shown(&lv_btnmatrix_class) == nullptr);
  CHECK(app::wait_until([&] { return contains(moonraker.gcodes(), "EXCLUDE_OBJECT NAME=CENTER_CUBE"); }));
}

TEST_CASE("object exclusion confirmation can be changed in System settings") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  CHECK(Config::get_instance()->get<bool>("/prompt_exclude_object"));
  ui::tap_tab(Nav::SETTINGS);
  ui::tap_label("System");
  lv_obj_t *label = nullptr;
  REQUIRE(app::wait_until([&] { return (label = ui::find_label("Object exclusion confirmation")) != nullptr; }));
  lv_obj_t *toggle;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    toggle = lv_obj_get_child(lv_obj_get_parent(label), 0);
    CHECK(lv_obj_has_state(toggle, LV_STATE_CHECKED));
  }
  ui::tap(toggle);
  CHECK_FALSE(Config::get_instance()->get<bool>("/prompt_exclude_object"));
}
