// Main screen layout from guppyconfig.json ("layout": {"main": ...}).
// Positions are compared through the centres of the buttons' containers,
// so the tests don't depend on pixel sizes.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "config.h"
#include "grid_layout.h"
#include "guppyscreen.h"
#include "sensor_container.h"

#include <mutex>

namespace {

struct Pos {
  lv_coord_t x, y;
};

Pos centre(lv_obj_t *obj) {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_update_layout(lv_scr_act());
  lv_area_t a;
  lv_obj_get_coords(obj, &a);
  return {(lv_coord_t)((a.x1 + a.x2) / 2), (lv_coord_t)((a.y1 + a.y2) / 2)};
}

// Centre of the button that carries the shown label `text`.
Pos button(const std::string &text) {
  lv_obj_t *label = ui::find_label(text);
  REQUIRE_MESSAGE(label != nullptr, "no shown label " << text << "; screen: " << ui::dump_screen());
  return centre(lv_obj_get_parent(label));
}

void check_no_layout_keys_added(const json &expected_layout) {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  json &root = Config::get_instance()->get_json("");
  if (expected_layout.is_null()) {
    CHECK_FALSE(root.contains("layout"));
  } else {
    CHECK(root["layout"] == expected_layout);
  }
}

void start(FakeMoonraker &moonraker, const json &layout_main) {
  json patch = json::object();
  if (!layout_main.is_null()) {
    patch["layout"] = {{"main", layout_main}};
  }
  app::start(moonraker, patch);
  app::wait_ready(moonraker);
}

// The hard-coded grid: two button columns, two rows used of three (Print and
// Stop are in the nav bar).
void check_default_buttons() {
  Pos homing = button("Homing"), extrude = button("Extrude");
  Pos fans = button("Fans"), led = button("LED");

  CHECK(homing.x == fans.x);
  CHECK(extrude.x == led.x);
  CHECK(homing.x < extrude.x);

  CHECK(homing.y == extrude.y);
  CHECK(fans.y == led.y);
  CHECK(homing.y < fans.y);

  // Buttons on the right half; temperatures top left, the chart below them.
  const lv_coord_t mid = 400;
  CHECK(homing.x > mid);
  lv_obj_t *extruder_label = ui::find_label("Extruder");
  REQUIRE_MESSAGE(extruder_label != nullptr, "screen: " << ui::dump_screen());
  Pos temps = centre(extruder_label);
  CHECK(temps.x < mid);
  CHECK(temps.y < fans.y);
  lv_obj_t *chart = ui::find_shown(&lv_chart_class);
  REQUIRE(chart != nullptr);
  Pos c = centre(chart);
  CHECK(c.x < mid);
  CHECK(c.y > fans.y);
}

}  // namespace

TEST_CASE("main layout without config keeps the hard-coded grid") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, nullptr);
  check_default_buttons();

  // Reading the layout must not add keys that Config::save() would write back.
  check_no_layout_keys_added(nullptr);
}

TEST_CASE("main layout hides a button") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, {{"items", {{"led", {{"hidden", true}}}}}});

  CHECK(ui::find_label("LED") == nullptr);
  Pos extrude = button("Extrude"), homing = button("Homing"), fans = button("Fans");
  CHECK(extrude.x > homing.x);
  CHECK(fans.x == homing.x);
  CHECK(fans.y > homing.y);
}

TEST_CASE("main layout moves a button to another cell") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, {{"items", {
    {"led", {{"hidden", true}}},
    {"bedmesh", {{"hidden", true}}},  // frees the cell
    {"fans", {{"col", 3}, {"row", 2}}},
  }}});

  Pos fans = button("Fans"), extrude = button("Extrude"), homing = button("Homing");
  CHECK(fans.x == extrude.x);
  CHECK(fans.x > homing.x);
  CHECK(fans.y > extrude.y);
}

TEST_CASE("main layout spans a button over two columns") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, {{"items", {
    {"led", {{"hidden", true}}},
    {"fans", {{"col", 2}, {"row", 1}, {"col_span", 2}}},
  }}});

  Pos fans = button("Fans"), homing = button("Homing"), extrude = button("Extrude");
  CHECK(fans.x > homing.x);
  CHECK(fans.x < extrude.x);
}

TEST_CASE("main layout columns and rows change the grid") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, {
    {"columns", {1, 1, 1, 1, 1}},
    {"rows", {1, 1, 1}},
    {"items", {{"homing", {{"col", 4}, {"row", 0}}}}},
  });

  Pos homing = button("Homing"), extrude = button("Extrude");
  CHECK(homing.x > extrude.x);
  CHECK(homing.y == extrude.y);
}

TEST_CASE("main layout with wrong types keeps defaults") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  json layout = {
    {"columns", "four"},
    {"rows", {"x", nullptr}},
    {"items", {
      {"fans", {{"col", "3"}, {"hidden", "yes"}}},
      {"print", 5},
    }},
  };
  start(moonraker, layout);
  check_default_buttons();
  check_no_layout_keys_added({{"main", layout}});
}

TEST_CASE("main layout clamps a cell outside the grid") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, {{"items", {
    {"bedmesh", {{"hidden", true}}},  // frees the last cell
    {"fans", {{"col", 9}, {"row", 9}, {"col_span", 5}}},
  }}});

  // Clamped to the last cell: under LED, in Extrude's column.
  Pos fans = button("Fans"), extrude = button("Extrude"), led = button("LED");
  CHECK(fans.x == extrude.x);
  CHECK(fans.y > led.y);
}

// Top edge of the tile that carries the shown label `text`.
lv_coord_t tile_top(const std::string &text) {
  lv_obj_t *label = ui::find_label(text);
  REQUIRE_MESSAGE(label != nullptr, "no shown label " << text);
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_update_layout(lv_scr_act());
  lv_area_t a;
  lv_obj_get_coords(lv_obj_get_parent(label), &a);
  return a.y1;
}

TEST_CASE("main layout padding of the temperatures moves the sensors only") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, {{"items", {{"temps", {{"padding", {{"top", 30}}}}}}}});

  CHECK(tile_top("Bed") == 30);  // the grid's own top padding is 0
  Pos homing = button("Homing");
  CHECK(homing.y < 100);  // the buttons stay where they were
}

TEST_CASE("main layout padding of the wrong type keeps the temperatures' own") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, {{"items", {{"temps", {{"padding", "wide"}}}}}});
  CHECK(tile_top("Bed") == SensorContainer::tile_gap());
}

// The layout written out (preview, default layout file) keeps an item's padding.
TEST_CASE("main layout item padding is written out with the layout") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  start(moonraker, {{"items", {{"temps", {{"padding", {{"top", 30}}}}}}}});
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  auto written = GridLayout::current_layout()["main"]["items"]["temps"];
  INFO(written.dump());
  CHECK(written["padding"] == json({{"top", 30}}));
}
