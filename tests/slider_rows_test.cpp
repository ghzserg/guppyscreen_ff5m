// The Retract and Limits screens (Tune > Retract, Tune > Limits): four rows
// of sliders that fit on the screen without a scrollbar, a line between the
// rows and none under the last one. Limits' fourth row is Minimum Cruise
// Ratio (Klipper 0.12) or Acceleration to Deceleration (older Klipper).

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"

#include <fstream>
#include <mutex>
#include <string>

namespace {

struct Screen {
  const char *button, *first_row, *last_row;
};

const Screen RETRACT = {"Retract", "Retract Length", "Unretract Speed"};
// The fixture's Klipper has minimum_cruise_ratio: its row is last.
const Screen LIMITS = {"Limits", "Velocity (mm/s)", "Minimum Cruise Ratio"};

void open(FakeMoonraker &moonraker, const Screen &s) {
  app::start(moonraker);
  app::wait_ready(moonraker);
  ui::tap_tab(Nav::TUNE);
  ui::tap_label(s.button);
  app::settle(moonraker);
}

// The row (title, Reset button and slider) of the shown title `title`.
lv_obj_t *row(const char *title) {
  lv_obj_t *r = ui::label_container(title);
  REQUIRE_MESSAGE(r != nullptr, std::string(title) << " not shown: " << ui::dump_screen());
  return r;
}

void check_fits(const Screen &s) {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  open(moonraker, s);
  lv_obj_t *last = row(s.last_row);
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_update_layout(lv_scr_act());
  lv_area_t a;
  lv_obj_get_coords(last, &a);
  lv_coord_t screen = lv_obj_get_height(lv_scr_act());
  INFO("last row " << a.y1 << ".." << a.y2 << ", screen height " << screen);
  CHECK(a.y2 < screen);
  CHECK(lv_obj_get_scroll_bottom(lv_obj_get_parent(last)) <= 0);
  CHECK_FALSE(lv_obj_has_flag(lv_obj_get_parent(last), LV_OBJ_FLAG_SCROLLABLE));  // no scrollbar
}

// On the open screen `s`.
void check_no_line_under_last(const Screen &s) {
  auto line_below = [](lv_obj_t *r) {
    return lv_obj_get_style_border_width(r, 0) > 0 && lv_obj_get_style_border_opa(r, 0) > LV_OPA_TRANSP
        && (lv_obj_get_style_border_side(r, 0) & LV_BORDER_SIDE_BOTTOM);
  };
  lv_obj_t *first = row(s.first_row), *last = row(s.last_row);
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  CHECK(line_below(first));
  CHECK_FALSE(line_below(last));
  // Still in line with the other rows.
  lv_obj_update_layout(lv_scr_act());
  lv_area_t f, l;
  lv_obj_get_coords(first, &f);
  lv_obj_get_coords(last, &l);
  CHECK(l.x1 == f.x1);
}

}  // namespace

TEST_CASE("the Retract screen fits its four rows on the screen") { check_fits(RETRACT); }
TEST_CASE("the Limits screen fits its four rows on the screen") { check_fits(LIMITS); }
TEST_CASE("the Retract screen has no line under its last row") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  open(moonraker, RETRACT);
  check_no_line_under_last(RETRACT);
}

TEST_CASE("the Limits screen has no line under its last row") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  open(moonraker, LIMITS);
  check_no_line_under_last(LIMITS);
}

TEST_CASE("Limits shows Minimum Cruise Ratio when Klipper has it") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  open(moonraker, LIMITS);
  REQUIRE(ui::find_label("Minimum Cruise Ratio") != nullptr);
  CHECK(ui::find_label("Acceleration to Deceleration (mm/s2)") == nullptr);
}

TEST_CASE("Limits shows Acceleration to Deceleration when Klipper has it") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  json fixture = json::parse(std::ifstream(app::fixture("ad5m_basic.json")));
  json subscribe = fixture["printer.objects.subscribe"];
  // An older Klipper: max_accel_to_decel, no minimum_cruise_ratio.
  subscribe["status"]["toolhead"].erase("minimum_cruise_ratio");
  subscribe["status"]["configfile"]["settings"]["printer"].erase("minimum_cruise_ratio");
  subscribe["status"]["toolhead"]["max_accel_to_decel"] = 10000.0;
  moonraker.set_result("printer.objects.subscribe", subscribe);
  open(moonraker, LIMITS);
  CHECK(ui::find_label("Minimum Cruise Ratio") == nullptr);
  check_no_line_under_last({"Limits", "Velocity (mm/s)", "Acceleration to Deceleration (mm/s2)"});
}
