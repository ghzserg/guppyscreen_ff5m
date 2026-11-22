// Home's print status line (thumbnail, ETA, progress bar) lines up with the
// Print tab button on its left when the sensor tiles leave room for it
// (AD5M: one row of tiles): the same top and bottom. With more rows of
// tiles (C5 Pro) it stays under them.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"
#include "config.h"

#include <mutex>
#include <ctime>
#include <cstring>
#include <fstream>

namespace {

// The status line around its "ETA: ..." label.
lv_obj_t *status_line() {
  lv_obj_t *label = ui::find_label("ETA:", ui::Match::Contains);
  REQUIRE_MESSAGE(label != nullptr, ui::dump_screen());
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  return lv_obj_get_parent(label);
}

void print_on_home(FakeMoonraker &moonraker) {
  moonraker.push_status({{"print_stats", {{"filename", "cube.gcode"}, {"state", "printing"}}}});
  app::settle(moonraker);
  ui::tap_tab(Nav::HOME);
  app::settle(moonraker);
}

}  // namespace

TEST_CASE("the AD5M print status on Home lines up with the Print tab") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  print_on_home(moonraker);

  lv_area_t status = ui::coords(status_line());
  lv_area_t tab = ui::coords(Nav::get().tab_button(Nav::PRINT));
  INFO("status y " << status.y1 << ".." << status.y2 << "; Print tab y " << tab.y1 << ".." << tab.y2);
  CHECK(status.y1 == tab.y1);
  CHECK(status.y2 == tab.y2);
}

TEST_CASE("the C5 Pro print status on Home stays under its sensor tiles") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  app::start(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  app::wait_ready(moonraker);
  print_on_home(moonraker);

  lv_area_t status = ui::coords(status_line());
  lv_area_t tile = ui::coords(ui::tile("T3"));
  INFO("status y " << status.y1 << ".." << status.y2 << "; tile T3 y " << tile.y1 << ".." << tile.y2);
  CHECK(status.y1 > tile.y2);
  CHECK(status.y2 - status.y1 == ui::coords(Nav::get().tab_button(Nav::PRINT)).y2 -
                                     ui::coords(Nav::get().tab_button(Nav::PRINT)).y1);
}

TEST_CASE("a tap on the progress bar of Home's print status opens the print screen") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  print_on_home(moonraker);

  lv_obj_t *line = status_line();
  lv_obj_t *found;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    found = ui::find_in(line, &lv_bar_class);
  }
  REQUIRE(found != nullptr);
  lv_area_t bar = ui::coords(found);
  ui::tap_at((bar.x1 + bar.x2) / 2, (bar.y1 + bar.y2) / 2);
  CHECK_MESSAGE(app::wait_until([] { return ui::find_label("Pause") != nullptr; }), ui::dump_screen());
}

TEST_CASE("Home shows a large clock only when not printing or paused") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  lv_obj_t *hours = nullptr, *minutes = nullptr, *colon = nullptr;
  lv_obj_t *line = nullptr;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    ui::walk(Nav::get().root(Nav::HOME), [&](lv_obj_t *obj) {
      if (lv_obj_check_type(obj, &lv_label_class) &&
          lv_obj_get_style_text_font(obj, 0)->dsc == lv_font_montserrat_48.dsc) {
        if (!strcmp(lv_label_get_text(obj), ":")) colon = obj;
        else if (!hours) hours = obj;
        else minutes = obj;
      }
    });
    REQUIRE(hours != nullptr);
    REQUIRE(minutes != nullptr);
    REQUIRE(colon != nullptr);
    const auto *font = lv_obj_get_style_text_font(hours, 0);
    const auto width = lv_font_get_glyph_width(font, '0', 0);
    for (uint32_t digit = '0'; digit <= '9'; ++digit) {
      CHECK(lv_font_get_glyph_width(font, digit, 0) == width);
      CHECK(lv_font_get_glyph_width(font, digit, ':') == width);
      CHECK(lv_font_get_glyph_width(font, ':', digit) == lv_font_get_glyph_width(font, ':', 0));
      for (uint32_t next = '0'; next <= '9'; ++next) {
        CHECK(lv_font_get_glyph_width(font, digit, next) == width);
      }
    }
    CHECK(lv_font_get_glyph_width(font, '-', '-') == width);
    line = lv_obj_get_parent(hours);
    CHECK(lv_obj_get_parent(minutes) == line);
    CHECK(lv_obj_get_parent(colon) == line);
    CHECK_FALSE(lv_obj_has_flag(line, LV_OBJ_FLAG_HIDDEN));
    CHECK_FALSE(lv_obj_has_flag(line, LV_OBJ_FLAG_CLICKABLE));
    CHECK_FALSE(lv_obj_has_flag(hours, LV_OBJ_FLAG_HIDDEN));
    CHECK_FALSE(lv_obj_has_flag(minutes, LV_OBJ_FLAG_HIDDEN));
    CHECK(lv_obj_has_flag(colon, LV_OBJ_FLAG_FLOATING));
  }
  REQUIRE(app::wait_until([&] {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    const auto now = std::time(nullptr);
    for (auto stamp : {now, now - 1}) {
      std::tm local;
      char expected[6];
      if (localtime_r(&stamp, &local) && std::strftime(expected, sizeof(expected), "%H:%M", &local) == 5 &&
          !strncmp(lv_label_get_text(hours), expected, 2) && !strcmp(lv_label_get_text(minutes), expected + 3) &&
          strlen(lv_label_get_text(hours)) == 2) {
        return true;
      }
    }
    return false;
  }));
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK_FALSE(lv_obj_has_flag(colon, LV_OBJ_FLAG_HIDDEN));
  }
  lv_area_t idle = ui::coords(line);
  lv_area_t left = ui::coords(hours);
  lv_area_t right = ui::coords(minutes);
  lv_area_t separator = ui::coords(colon);
  for (int i = 0; i < 2; ++i) {
    app::pump(1100);
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK_FALSE(lv_obj_has_flag(colon, LV_OBJ_FLAG_HIDDEN));
  }
  for (auto *obj : {hours, minutes, colon}) {
    lv_area_t digits = ui::coords(obj);
    lv_area_t before = obj == hours ? left : obj == minutes ? right : separator;
    CHECK(digits.x1 == before.x1);
    CHECK(digits.x2 == before.x2);
    CHECK(digits.y1 == before.y1);
    CHECK(digits.y2 == before.y2);
    CHECK(digits.x1 >= idle.x1);
    CHECK(digits.x2 <= idle.x2);
    CHECK(digits.y1 >= idle.y1);
    CHECK(digits.y2 <= idle.y2);
  }
  CHECK(left.x2 < separator.x1);
  CHECK(separator.x2 < right.x1);
  ui::tap(line);
  CHECK(Nav::get().active() == Nav::HOME);

  for (const char *state : {"printing", "paused", "complete", "error", "cancelled"}) {
    moonraker.push_status({{"print_stats", {{"filename", "cube.gcode"}, {"state", state}}}});
    app::settle(moonraker);
    ui::tap_tab(Nav::HOME);
    const bool printing = !strcmp(state, "printing") || !strcmp(state, "paused");
    {
      std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
      CHECK(lv_obj_has_flag(hours, LV_OBJ_FLAG_HIDDEN) == printing);
      CHECK(lv_obj_has_flag(minutes, LV_OBJ_FLAG_HIDDEN) == printing);
      CHECK(lv_obj_has_flag(colon, LV_OBJ_FLAG_HIDDEN) == printing);
      CHECK(lv_obj_has_flag(line, LV_OBJ_FLAG_CLICKABLE) == printing);
      CHECK_FALSE(lv_obj_has_flag(line, LV_OBJ_FLAG_HIDDEN));
    }
    if (printing) {
      CHECK(status_line() == line);
      ui::tap(line);
      CHECK(Nav::get().active() == Nav::PRINT);
    } else {
      ui::tap(line);
      CHECK(Nav::get().active() == Nav::HOME);
    }
  }
}

TEST_CASE("System Clock switch persists and hides both clocks but not print status") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  lv_obj_t *home_clock = nullptr;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    ui::walk(Nav::get().root(Nav::HOME), [&](lv_obj_t *obj) {
      if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), ":") &&
          lv_obj_get_style_text_font(obj, 0)->dsc == lv_font_montserrat_48.dsc)
        home_clock = lv_obj_get_parent(obj);
    });
  }
  REQUIRE(home_clock != nullptr);

  ui::tap_tab(Nav::SETTINGS);
  ui::tap_label("System");
  lv_obj_t *label = ui::find_label("Clock");
  REQUIRE_MESSAGE(label != nullptr, ui::dump_screen());
  lv_obj_t *toggle = nullptr;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    toggle = ui::find_in(lv_obj_get_parent(label), &lv_switch_class);
  }
  REQUIRE(toggle != nullptr);
  CHECK(lv_obj_has_state(toggle, LV_STATE_CHECKED));

  ui::tap(toggle);
  CHECK_FALSE(Config::get_instance()->get<bool>("/show_clock"));
  CHECK(json::parse(std::ifstream(app::dir() + "/guppyconfig.json"))["show_clock"] == false);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK(lv_obj_has_flag(home_clock, LV_OBJ_FLAG_HIDDEN));
  }

  moonraker.push_status({{"print_stats", {{"filename", "cube.gcode"}, {"state", "printing"}}}});
  app::settle(moonraker);
  ui::tap_tab(Nav::HOME);
  CHECK(status_line() == home_clock);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK_FALSE(lv_obj_has_flag(home_clock, LV_OBJ_FLAG_HIDDEN));
  }

  lv_obj_t *print_clock = nullptr;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    ui::walk(Nav::get().root(Nav::PRINT), [&](lv_obj_t *obj) {
      if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), "--:--:--"))
        print_clock = obj;
    });
  }
  REQUIRE(print_clock != nullptr);
  CHECK(lv_obj_has_flag(print_clock, LV_OBJ_FLAG_HIDDEN));

  ui::tap_tab(Nav::SETTINGS);
  ui::tap_label("System");
  ui::tap(toggle);
  CHECK(Config::get_instance()->get<bool>("/show_clock"));
  CHECK(json::parse(std::ifstream(app::dir() + "/guppyconfig.json"))["show_clock"] == true);
  ui::tap_tab(Nav::PRINT);
  CHECK_FALSE(lv_obj_has_flag(print_clock, LV_OBJ_FLAG_HIDDEN));
  CHECK(app::wait_until([&] {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    return strcmp(lv_label_get_text(print_clock), "--:--:--") != 0;
  }));
}

TEST_CASE("Clock disabled in configuration stays hidden on startup") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker, {{"show_clock", false}});
  app::wait_ready(moonraker);
  CHECK_FALSE(Config::get_instance()->get<bool>("/show_clock"));

  lv_obj_t *home_clock = nullptr;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    ui::walk(Nav::get().root(Nav::HOME), [&](lv_obj_t *obj) {
      if (lv_obj_check_type(obj, &lv_label_class) && !strcmp(lv_label_get_text(obj), ":") &&
          lv_obj_get_style_text_font(obj, 0)->dsc == lv_font_montserrat_48.dsc)
        home_clock = lv_obj_get_parent(obj);
    });
  }
  REQUIRE(home_clock != nullptr);
  CHECK(lv_obj_has_flag(home_clock, LV_OBJ_FLAG_HIDDEN));
  moonraker.push_status({{"print_stats", {{"filename", "cube.gcode"}, {"state", "printing"}}}});
  app::settle(moonraker);
  ui::tap_tab(Nav::HOME);
  CHECK_FALSE(lv_obj_has_flag(home_clock, LV_OBJ_FLAG_HIDDEN));
  bool status_visible = false;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK(lv_obj_has_flag(home_clock, LV_OBJ_FLAG_CLICKABLE));
    ui::walk(home_clock, [&](lv_obj_t *obj) {
      if (lv_obj_check_type(obj, &lv_label_class) &&
          strstr(lv_label_get_text(obj), "ETA:") &&
          !lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN))
        status_visible = true;
    });
  }
  CHECK(status_visible);
}
