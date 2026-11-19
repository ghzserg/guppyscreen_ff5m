// Home's print status line (thumbnail, ETA, progress bar) lines up with the
// Print tab button on its left when the sensor tiles leave room for it
// (AD5M: one row of tiles): the same top and bottom. With more rows of
// tiles (C5 Pro) it stays under them.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"

#include <mutex>

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
