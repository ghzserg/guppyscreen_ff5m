// Fixes of the screens as they were: the AD5X's nozzle temperature on the
// print screen, Fine Tune's percents, Pro's Back button, Cancel, and the
// numpad of a sensor deleted by a reconnect. The thumbnail fixes are in
// thumbnail_test.cpp.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"
#include "lv_tc_screen.h"
#include "state.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>

namespace {

// fn on obj and every object under it. Hold the LVGL lock.
void walk(lv_obj_t *obj, const std::function<void(lv_obj_t *)> &fn) {
  fn(obj);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); i++) walk(lv_obj_get_child(obj, i), fn);
}

// Tap tab `idx` (0 Home, 1 Tune, ...) on the main tabview's bar on the left.
void tap_tab(uint32_t idx) {
  lv_point_t p;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_t *tv = nullptr;
    walk(lv_scr_act(), [&](lv_obj_t *o) {
      if (!tv && lv_obj_check_type(o, &lv_tabview_class)) tv = o;
    });
    REQUIRE(tv != nullptr);
    uint32_t n = lv_obj_get_child_cnt(lv_tabview_get_content(tv));
    REQUIRE(idx < n);
    lv_obj_update_layout(tv);
    lv_area_t a;
    lv_obj_get_coords(lv_tabview_get_tab_btns(tv), &a);
    p = {(lv_coord_t)((a.x1 + a.x2) / 2), (lv_coord_t)(a.y1 + (2 * idx + 1) * lv_area_get_height(&a) / (2 * n))};
  }
  ui::tap_at(p.x, p.y);
  app::pump(100);
}

constexpr uint32_t TUNE_TAB = 1;

// The print screen's nozzle label ("T<n>: ..."), shown or not.
std::string nozzle_label() {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  std::string text;
  walk(lv_scr_act(), [&](lv_obj_t *o) {
    if (!text.empty() || !lv_obj_check_type(o, &lv_label_class)) return;
    const char *t = lv_label_get_text(o);
    if (t && t[0] == 'T' && t[1] >= '0' && t[1] <= '9' && t[2] == ':') text = t;
  });
  return text;
}

bool sent(FakeMoonraker &moonraker, const std::string &gcode) {
  auto g = moonraker.gcodes();
  return std::find(g.begin(), g.end(), gcode) != g.end();
}

}  // namespace

TEST_CASE("the AD5X print screen shows the active channel with its one nozzle's temperature") {
  FakeMoonraker moonraker(app::fixture("ad5x_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  // T2 is a filament channel: there is no extruder2, the nozzle is extruder.
  moonraker.push_status({{"zmod_color", {{"active_tool_id", 2}}}});
  app::settle(moonraker);
  moonraker.push_status({{"extruder", {{"temperature", 230.0}, {"target", 230.0}}}});
  CHECK_MESSAGE(app::wait_until([] { return nozzle_label() == "T2: 230 / 230"; }), "label: " << nozzle_label());
}

TEST_CASE("the C5 Pro print screen shows the active nozzle's temperature") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  moonraker.push_status({{"zmod_color", {{"active_tool_id", 2}}}});
  app::settle(moonraker);
  moonraker.push_status({{"extruder2", {{"temperature", 230.0}, {"target", 230.0}}}});
  REQUIRE_MESSAGE(app::wait_until([] { return nozzle_label() == "T2: 230 / 230"; }), "label: " << nozzle_label());

  moonraker.push_status({{"extruder", {{"temperature", 180.0}, {"target", 180.0}}}});  // T0's: not shown
  app::settle(moonraker);
  CHECK(nozzle_label() == "T2: 230 / 230");
}

TEST_CASE("Fine Tune shows Klipper's 1.15 as 115% and steps from it") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  moonraker.push_status({{"gcode_move", {{"speed_factor", 1.15}, {"extrude_factor", 1.15}}}});
  app::settle(moonraker);

  tap_tab(TUNE_TAB);
  ui::tap_label("Fine Tune");
  REQUIRE_MESSAGE(app::wait_until([] { return ui::find_label("115%") != nullptr; }), ui::dump_screen());

  ui::tap_label("Speed+");  // the step is 1 (the multiplier's first)
  CHECK_MESSAGE(app::wait_until([&] { return sent(moonraker, "M220 S116"); }),
                "gcodes: " << ui::join(moonraker.gcodes()));
  ui::tap_label("Flow-");
  CHECK_MESSAGE(app::wait_until([&] { return sent(moonraker, "M221 S114"); }),
                "gcodes: " << ui::join(moonraker.gcodes()));
}

// Pro's grid has rows 0..2; a cell in row 3 made LVGL read past the track
// array and place the button wherever that memory said.
TEST_CASE("Pro's Back button is in a row of its grid") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_t *pro = nullptr;  // the grid holding "Calibrate VFA"
  walk(lv_scr_act(), [&](lv_obj_t *o) {
    if (!pro && lv_obj_check_type(o, &lv_label_class) && strcmp(lv_label_get_text(o), "Calibrate VFA") == 0) {
      pro = lv_obj_get_parent(lv_obj_get_parent(o));
    }
  });
  REQUIRE(pro != nullptr);
  lv_obj_t *back = nullptr;
  walk(pro, [&](lv_obj_t *o) {
    if (!back && lv_obj_check_type(o, &lv_label_class) && strcmp(lv_label_get_text(o), "Back") == 0) {
      back = lv_obj_get_parent(o);
    }
  });
  REQUIRE(back != nullptr);

  const lv_coord_t *rows = lv_obj_get_style_grid_row_dsc_array(pro, LV_PART_MAIN);
  REQUIRE(rows != nullptr);
  int n_rows = 0;
  while (rows[n_rows] != LV_GRID_TEMPLATE_LAST) n_rows++;
  CHECK(lv_obj_get_style_grid_cell_row_pos(back, LV_PART_MAIN) < n_rows);
}

// With prompt_emergency_stop off, Cancel runs at once. The print status
// stays until Klipper reports the print cancelled.
TEST_CASE("Cancel keeps the print status until Klipper ends the print") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker, {{"prompt_emergency_stop", false}});
  app::wait_ready(moonraker);

  moonraker.push_status({{"print_stats", {{"filename", "cubes.gcode"}, {"state", "printing"}}}});
  REQUIRE_MESSAGE(app::wait_until([] { return ui::find_label("cubes.gcode") != nullptr; }), ui::dump_screen());

  ui::tap_label("Cancel");
  REQUIRE(app::wait_until([&] { return moonraker.request_count("printer.print.cancel") > 0; }));
  app::pump(200);
  CHECK_MESSAGE(ui::find_label("cubes.gcode") != nullptr, "print status hidden before Klipper reported it");

  moonraker.push_status({{"print_stats", {{"state", "cancelled"}}}});
  CHECK(app::wait_until([] { return ui::find_label("cubes.gcode") == nullptr; }));
}

// The sensors are rebuilt on a reconnect; the numpad's OK called into the
// deleted one.
TEST_CASE("the numpad closes when its sensor is rebuilt by a reconnect") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_label("Extruder");
  REQUIRE_MESSAGE(app::wait_until([] { return ui::find_shown(&lv_textarea_class) != nullptr; }), ui::dump_screen());

  moonraker.forget_requests();
  moonraker.drop_client();
  REQUIRE(app::wait_until([&] { return moonraker.request_count("printer.objects.subscribe") > 0; }, 10000));
  app::settle(moonraker);
  CHECK(ui::find_shown(&lv_textarea_class) == nullptr);
}

TEST_CASE("print metadata leaves Klipper's print duration intact") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  moonraker.set_result("server.files.metadata", {{"estimated_time", 600}});
  app::start(moonraker);
  app::wait_ready(moonraker);

  moonraker.push_status({{"print_stats", {
    {"filename", "part.gcode"}, {"state", "printing"}, {"print_duration", 30.0}, {"filament_used", 80.0},
  }}});
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.metadata") > 0; }));
  app::settle(moonraker);
  const auto duration = State::get_instance()->get_data_copy("/printer_state/print_stats/print_duration"_json_pointer);
  CHECK(duration == 30.0);
}

TEST_CASE("touch calibration shows the first point after the startup delay") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  auto &lv_lock = GuppyScreen::get()->get_lock();
  lv_tc_screen_t *screen;
  {
    std::lock_guard<std::mutex> lock(lv_lock);
    screen = reinterpret_cast<lv_tc_screen_t *>(lv_tc_screen_create());
    lv_tc_screen_start(&screen->screenObj);
    CHECK(lv_obj_has_flag(screen->indicatorObj, LV_OBJ_FLAG_HIDDEN));
  }
  REQUIRE(app::wait_until([&] {
    std::lock_guard<std::mutex> lock(lv_lock);
    return !lv_obj_has_flag(screen->indicatorObj, LV_OBJ_FLAG_HIDDEN);
  }));
  {
    std::lock_guard<std::mutex> lock(lv_lock);
    CHECK(screen->currentStep == 1);
    lv_obj_del(&screen->screenObj);
  }
}
