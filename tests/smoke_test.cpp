// Smoke tests: the app starts against a basic AD5M, shows live data and
// sends commands. Each TEST_CASE runs in its own process (tests/run_tests.sh).
// Test case names must not contain commas.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "guppyscreen.h"
#include "ui.h"

#include <algorithm>
#include <cstdlib>

static bool contains(const std::vector<std::string> &v, const std::string &s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

TEST_CASE("startup shows heater temperatures from Klipper") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  // Fixture: extruder 31.2 / 0, bed 27.8 / 0. The UI shows whole degrees.
  auto extruder = ui::row_texts("Extruder");
  INFO("Extruder row: " << ui::join(extruder) << "; screen: " << ui::dump_screen());
  CHECK(contains(extruder, "31"));

  auto bed = ui::row_texts("Bed");
  INFO("Bed row: " << ui::join(bed));
  CHECK(contains(bed, "27"));
}

TEST_CASE("status update changes the shown temperature and target") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  moonraker.push_status({{"extruder", {{"temperature", 215.4}, {"target", 220.0}}}});

  bool updated = app::wait_until([] {
    auto row = ui::row_texts("Extruder");
    return contains(row, "215") && contains(row, "220");
  });
  CHECK_MESSAGE(updated, "Extruder row: " << ui::join(ui::row_texts("Extruder")));
}

TEST_CASE("homing panel sends G28") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_label("Homing");
  REQUIRE_MESSAGE(app::wait_until([] { return ui::find_label("G28") != nullptr; }),
                  "homing panel did not open; screen: " << ui::dump_screen());

  ui::tap_label("G28");
  bool sent = app::wait_until([&] { return contains(moonraker.gcodes(), "G28"); });
  CHECK_MESSAGE(sent, "G-codes sent: " << ui::join(moonraker.gcodes()));
}

TEST_CASE("homing shows live coordinates and axis mode") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_label("Homing");
  lv_obj_t *labels[5];
  REQUIRE(app::wait_until([&] {
    labels[0] = ui::find_label("X: 0.00");
    labels[1] = ui::find_label("Y: 0.00");
    labels[2] = ui::find_label("Z: 0.00");
    labels[3] = ui::find_label("Z-offset: 0.000");
    labels[4] = ui::find_label("XYZ: G90");
    return std::all_of(std::begin(labels), std::end(labels), [](auto *label) { return label != nullptr; });
  }));
  lv_obj_t *z_up = ui::label_container("Z+");
  lv_obj_t *motor_off = ui::label_container("Motor off");
  REQUIRE(z_up != nullptr);
  REQUIRE(motor_off != nullptr);
  app::redraw();

  int draws[5] = {};
  const auto count_draw = [](lv_event_t *event) {
    ++*static_cast<int *>(lv_event_get_user_data(event));
  };
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_area_t x, y, z, offset, mode, top_button, bottom_button;
    lv_obj_get_coords(labels[0], &x);
    lv_obj_get_coords(labels[1], &y);
    lv_obj_get_coords(labels[2], &z);
    lv_obj_get_coords(labels[3], &offset);
    lv_obj_get_coords(labels[4], &mode);
    lv_obj_get_coords(z_up, &top_button);
    lv_obj_get_coords(motor_off, &bottom_button);
    CHECK(std::abs(x.y1 - top_button.y1) <= 1);
    CHECK(offset.y1 - z.y1 == 2 * (y.y1 - x.y1));
    CHECK(mode.y1 - offset.y1 == 2 * (y.y1 - x.y1));
    CHECK(mode.y2 < bottom_button.y1);
    for (size_t i = 0; i < 5; ++i) {
      lv_obj_add_event_cb(labels[i], count_draw, LV_EVENT_DRAW_MAIN, &draws[i]);
    }
  }

  moonraker.push_status({{"gcode_move", {{"gcode_position", {12.5, 0.0, 0.0, 0.0}}}}});
  REQUIRE(app::wait_until([] { return ui::find_label("X: 12.50") != nullptr; }));
  app::redraw();
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK(draws[0] > 0);
    for (size_t i = 0; i < 5; ++i) {
      if (i > 0) CHECK_MESSAGE(draws[i] == 0, "label " << i << " was redrawn");
      lv_obj_remove_event_cb(labels[i], count_draw);
    }
  }

  moonraker.push_status({{"gcode_move", {
    {"gcode_position", {12.5, -3.25, 9.75, 0.0}},
    {"homing_origin", {0.0, 0.0, -0.125, 0.0}},
    {"absolute_coordinates", false}
  }}});
  CHECK(app::wait_until([] {
    return ui::find_label("X: 12.50") && ui::find_label("Y: -3.25") && ui::find_label("Z: 9.75") &&
           ui::find_label("Z-offset: -0.125") && ui::find_label("XYZ: G91");
  }));

  moonraker.push_status({{"gcode_move", {{"absolute_coordinates", true}}}});
  CHECK(app::wait_until([] {
    return ui::find_label("Y: -3.25") && ui::find_label("Z-offset: -0.125") && ui::find_label("XYZ: G90");
  }));
}
