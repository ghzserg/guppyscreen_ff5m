// The PID screen (Tune > PID) shows its sensors as Home does: tiles with the
// icon and the name under it, the target above the temperature, at the place
// of Home's tiles. The extruder's target is the calibration temperature. With
// several nozzles (C5 Pro) a tap on a nozzle selects it for the calibration
// (blue background) instead of T0-T3 buttons; every tap on a nozzle opens the
// numpad for the temperature. One nozzle (AD5M, AD5X): calibrate T0.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"

#include <algorithm>
#include <mutex>
#include <utility>
#include <vector>

using ui::tile;
using ui::tile_and_icon;

namespace {

void open_pid(FakeMoonraker &moonraker) {
  ui::tap_tab(Nav::TUNE);
  ui::tap_label("PID");
  moonraker.push_status({{"extruder", {{"temperature", 31.0}}}});
  app::settle(moonraker);
}

}  // namespace

TEST_CASE("the PID screen shows its sensors as tiles") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  open_pid(moonraker);

  auto texts = ui::row_texts("Extruder");
  INFO("Extruder: " << ui::join(texts));
  CHECK(std::find(texts.begin(), texts.end(), "245") != texts.end());  // the PID target
  CHECK(std::find(texts.begin(), texts.end(), "31") != texts.end());
  CHECK(std::find(texts.begin(), texts.end(), "/") == texts.end());  // a tile, not the line
}

TEST_CASE("the PID screen's tiles sit where Home's do") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  auto home = tile_and_icon("Extruder");
  auto home_bed = tile_and_icon("Bed");
  open_pid(moonraker);
  auto pid = tile_and_icon("Extruder");
  auto pid_bed = tile_and_icon("Bed");

  INFO("home tile " << home.first.x1 << "," << home.first.y1 << ".." << home.first.x2 << "," << home.first.y2
       << "; PID tile " << pid.first.x1 << "," << pid.first.y1 << ".." << pid.first.x2 << "," << pid.first.y2);
  CHECK(pid.first.x1 == home.first.x1);
  CHECK(pid.first.y1 == home.first.y1);
  CHECK(pid.first.x2 == home.first.x2);
  CHECK(pid.first.y2 == home.first.y2);
  CHECK(pid.second.x1 == home.second.x1);
  CHECK(pid.second.y1 == home.second.y1);
  CHECK(pid_bed.first.x1 == home_bed.first.x1);  // the same gap
}

TEST_CASE("the C5 Pro PID screen has a tile per nozzle instead of T0-T3 buttons") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  app::start(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  app::wait_ready(moonraker);
  open_pid(moonraker);

  for (const char *t : {"T0", "T1", "T2", "T3"}) {
    auto texts = ui::row_texts(t);
    INFO(std::string(t) << ": " << ui::join(texts));
    CHECK(std::find(texts.begin(), texts.end(), "245") != texts.end());  // the calibration temperature
  }
  CHECK_FALSE(ui::any_shown(ui::btnmatrices("T0")));
  CHECK(ui::has_bg(tile("T0"), LV_PALETTE_BLUE));  // active_tool_id 0 in the fixture
}

TEST_CASE("a tap on a C5 Pro nozzle on the PID screen selects it and asks for the temperature") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  app::start(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  app::wait_ready(moonraker);
  open_pid(moonraker);

  ui::tap(tile("T2"));
  ui::enter_on_numpad("230");
  app::settle(moonraker);
  CHECK(ui::has_bg(tile("T2"), LV_PALETTE_BLUE));
  CHECK_FALSE(ui::has_bg(tile("T0"), LV_PALETTE_BLUE));
  auto texts = ui::row_texts("T2");
  INFO("T2: " << ui::join(texts));
  CHECK(std::find(texts.begin(), texts.end(), "230") != texts.end());
  // Selecting a nozzle to calibrate doesn't change the printer's tool.
  CHECK(moonraker.gcodes().empty());

  ui::tap_label("PID Extruder");
  REQUIRE(app::wait_until([&] { return !moonraker.gcodes().empty(); }));
  CHECK(moonraker.gcodes().back() == "PID_TUNE_EXTRUDER T=2 TEMPERATURE=230");
}

TEST_CASE("the AD5X PID screen calibrates its one nozzle without T0-T3 buttons") {
  FakeMoonraker moonraker(app::fixture("ad5x_basic.json"));
  app::start(moonraker, app::zmod_sensors("guppyconfig_ad5x.json", "AD5X"));
  app::wait_ready(moonraker);
  open_pid(moonraker);

  CHECK_FALSE(ui::any_shown(ui::btnmatrices("T0")));
  ui::tap(tile("Extruder"));
  ui::enter_on_numpad("250");
  app::settle(moonraker);
  CHECK_FALSE(ui::has_bg(tile("Extruder"), LV_PALETTE_BLUE));  // nothing to select
  ui::tap_label("PID Extruder");
  REQUIRE(app::wait_until([&] { return !moonraker.gcodes().empty(); }));
  CHECK(moonraker.gcodes().back() == "PID_TUNE_EXTRUDER T=0 TEMPERATURE=250");
}

// The macro heats `extruder` for T=0, else `extruderT`: the AD5X's four
// filament channels share one nozzle, T=0 whatever channel is loaded.
TEST_CASE("the AD5X PID screen calibrates its nozzle with any filament channel active") {
  FakeMoonraker moonraker(app::fixture("ad5x_basic.json"));
  app::start(moonraker, app::zmod_sensors("guppyconfig_ad5x.json", "AD5X"));
  app::wait_ready(moonraker);
  moonraker.push_status({{"zmod_color", {{"active_tool_id", 2}}}});
  open_pid(moonraker);
  moonraker.push_status({{"zmod_color", {{"active_tool_id", -1}}}});
  app::settle(moonraker);

  ui::tap_label("PID Extruder");
  REQUIRE(app::wait_until([&] { return !moonraker.gcodes().empty(); }));
  CHECK(moonraker.gcodes().back() == "PID_TUNE_EXTRUDER T=0 TEMPERATURE=245");
}

TEST_CASE("a C5 Pro nozzle picked on the PID screen stays picked through status updates") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  app::start(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  app::wait_ready(moonraker);
  open_pid(moonraker);

  ui::tap(tile("T2"));
  ui::enter_on_numpad("230");
  moonraker.push_status({{"zmod_color", {{"active_tool_id", 0}}}, {"extruder", {{"temperature", 32.0}}}});
  app::settle(moonraker);
  CHECK(ui::has_bg(tile("T2"), LV_PALETTE_BLUE));
  CHECK_FALSE(ui::has_bg(tile("T0"), LV_PALETTE_BLUE));
  ui::tap_label("PID Extruder");
  REQUIRE(app::wait_until([&] { return !moonraker.gcodes().empty(); }));
  CHECK(moonraker.gcodes().back() == "PID_TUNE_EXTRUDER T=2 TEMPERATURE=230");
}

TEST_CASE("a C5 Pro nozzle left out of the config keeps the PID screen's tools") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  app::start(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro", "extruder1"));
  app::wait_ready(moonraker);
  open_pid(moonraker);

  ui::tap(tile("T2"));
  ui::enter_on_numpad("230");
  app::settle(moonraker);
  ui::tap_label("PID Extruder");
  REQUIRE(app::wait_until([&] { return !moonraker.gcodes().empty(); }));
  CHECK(moonraker.gcodes().back() == "PID_TUNE_EXTRUDER T=2 TEMPERATURE=230");
}

// The chart's extruder line: the selected nozzle's, in its tile's colour.
TEST_CASE("the PID chart draws the selected nozzle in its tile's colour") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  app::start(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  app::wait_ready(moonraker);
  open_pid(moonraker);

  for (const char *name : {"T0", "T2"}) {
    lv_obj_t *t = tile(name);
    if (std::string(name) != "T0") {
      ui::tap(t);
      ui::enter_on_numpad("230");
      app::settle(moonraker);
    }
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    // PID's: on the screen holding the tiles.
    lv_obj_t *chart = ui::find_in(lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(t))), &lv_chart_class);
    REQUIRE(chart != nullptr);
    // Series are listed newest first: the extruder's, added first, is last.
    lv_chart_series_t *extruder_series = nullptr;
    for (lv_chart_series_t *s = lv_chart_get_series_next(chart, NULL); s != NULL; s = lv_chart_get_series_next(chart, s)) {
      extruder_series = s;
    }
    REQUIRE(extruder_series != nullptr);
    INFO(name << ": tile " << std::hex << lv_color_to32(lv_obj_get_style_border_color(t, 0)) << " line "
              << lv_color_to32(extruder_series->color));
    CHECK(lv_color_to32(extruder_series->color) == lv_color_to32(lv_obj_get_style_border_color(t, 0)));
  }
}

// The line is one nozzle's history: another nozzle starts it afresh.
TEST_CASE("the PID chart starts afresh for another nozzle") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  app::start(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  app::wait_ready(moonraker);
  open_pid(moonraker);
  for (int i = 0; i < 3; i++) {  // T0's history (the chart takes a point a second)
    moonraker.push_status({{"extruder", {{"temperature", 240.0 + i}}}});
    app::settle(moonraker);
    app::pump(1100);
  }

  lv_obj_t *t2 = tile("T2");
  auto points = [&] {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_t *chart = ui::find_in(lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(t2))), &lv_chart_class);
    lv_chart_series_t *extruder_series = nullptr;  // listed newest first: the extruder's is last
    for (lv_chart_series_t *s = lv_chart_get_series_next(chart, NULL); s != NULL; s = lv_chart_get_series_next(chart, s)) {
      extruder_series = s;
    }
    lv_coord_t *y = lv_chart_get_y_array(chart, extruder_series);
    int n = 0;
    for (uint16_t i = 0; i < lv_chart_get_point_count(chart); i++) n += y[i] != LV_CHART_POINT_NONE;
    return n;
  };
  REQUIRE(points() > 0);
  ui::tap(t2);
  ui::enter_on_numpad("230");
  CHECK(points() == 0);
}

namespace {

uint32_t stripe_of(lv_obj_t *t) {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  return lv_color_to32(lv_obj_get_style_border_color(t, 0));
}

// The colours of the PID chart's lines, oldest first (extruder, bed, weight); `nozzle` is a PID nozzle tile.
std::vector<uint32_t> pid_chart_colours(lv_obj_t *nozzle) {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_t *chart = ui::find_in(lv_obj_get_parent(lv_obj_get_parent(lv_obj_get_parent(nozzle))), &lv_chart_class);
  REQUIRE(chart != nullptr);
  std::vector<uint32_t> colours;
  for (lv_chart_series_t *s = lv_chart_get_series_next(chart, NULL); s != NULL; s = lv_chart_get_series_next(chart, s)) {
    colours.insert(colours.begin(), lv_color_to32(s->color));  // listed newest first
  }
  return colours;
}

}  // namespace

// При наличии weightValue доступны все три управления независимо от имени профиля.
TEST_CASE("the C5 Pro PID screen keeps weight controls and colours when weightValue exists") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  json objects = moonraker.result("printer.objects.list");
  objects["objects"].push_back("temperature_sensor weightValue");
  moonraker.set_result("printer.objects.list", objects);
  app::start(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  app::wait_ready(moonraker);
  std::vector<uint32_t> home;
  for (const char *name : {"T0", "T1", "T2", "T3", "Bed", "Chamber"}) home.push_back(stripe_of(tile(name)));
  uint32_t home_bed = stripe_of(tile("Bed"));

  open_pid(moonraker);
  lv_obj_t *bed = tile("Bed");
  lv_obj_t *weight = tile("Weight");
  CHECK(ui::find_label("Clear nozzle") != nullptr);
  CHECK(ui::find_label("Load Cell") != nullptr);
  CHECK(stripe_of(bed) == home_bed);
  INFO("weight " << std::hex << stripe_of(weight));
  CHECK(std::find(home.begin(), home.end(), stripe_of(weight)) == home.end());

  std::vector<uint32_t> lines = pid_chart_colours(tile("T0"));  // T0 is a level deeper than Bed
  REQUIRE(lines.size() == 3);
  CHECK(lines[1] == stripe_of(bed));
  CHECK(lines[2] == stripe_of(weight));
}

// The AD5M's Home shows no weight: PID keeps its usual orange, free there.
TEST_CASE("the AD5M PID screen keeps the weight orange and the bed as on Home") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  uint32_t home_bed = stripe_of(tile("Bed"));

  open_pid(moonraker);
  CHECK(stripe_of(tile("Bed")) == home_bed);
  CHECK(stripe_of(tile("Weight")) == lv_color_to32(lv_palette_main(LV_PALETTE_ORANGE)));
}

TEST_CASE("the C5 Pro PID screen hides unsupported weight controls") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  app::start(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  app::wait_ready(moonraker);
  open_pid(moonraker);
  CHECK(ui::find_label("Weight") == nullptr);
  CHECK(ui::find_label("Clear nozzle") == nullptr);
  CHECK(ui::find_label("Load Cell") == nullptr);
  CHECK(ui::find_label("PID Bed") != nullptr);
}

TEST_CASE("the AD5M PID screen keeps weight controls") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  open_pid(moonraker);
  CHECK(ui::find_label("Weight") != nullptr);
  CHECK(ui::find_label("Clear nozzle") != nullptr);
  CHECK(ui::find_label("Load Cell") != nullptr);
}

TEST_CASE("the C5 Pro PID bed tile opens temperature input after nozzle selection") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  app::start(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  app::wait_ready(moonraker);
  open_pid(moonraker);

  ui::tap(ui::tile("T2"));
  ui::enter_on_numpad("230");
  ui::tap(ui::tile("Bed"));
  REQUIRE_MESSAGE(ui::find_shown(&lv_textarea_class) != nullptr, "Bed did not open PID temperature input");
  CHECK(ui::has_bg(ui::tile("Bed"), LV_PALETTE_BLUE));
  ui::enter_on_numpad("90");
  CHECK_FALSE(ui::has_bg(ui::tile("Bed"), LV_PALETTE_BLUE));
  ui::tap_label("PID Bed");
  REQUIRE(app::wait_until([&] { return !moonraker.gcodes().empty(); }));
  CHECK(moonraker.gcodes().back() == "PID_TUNE_BED TEMPERATURE=90");
  ui::tap_label("PID Extruder");
  REQUIRE(app::wait_until([&] { return moonraker.gcodes().size() >= 2; }));
  CHECK(moonraker.gcodes().back() == "PID_TUNE_EXTRUDER T=2 TEMPERATURE=230");
}

TEST_CASE("the AD5M PID bed tile accepts its calibration temperature") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  open_pid(moonraker);

  ui::tap(ui::tile("Bed"));
  REQUIRE(ui::find_shown(&lv_textarea_class) != nullptr);
  CHECK(ui::has_bg(ui::tile("Bed"), LV_PALETTE_BLUE));
  ui::enter_on_numpad("95");
  CHECK_FALSE(ui::has_bg(ui::tile("Bed"), LV_PALETTE_BLUE));
  ui::tap_label("PID Bed");
  REQUIRE(app::wait_until([&] { return !moonraker.gcodes().empty(); }));
  CHECK(moonraker.gcodes().back() == "PID_TUNE_BED TEMPERATURE=95");
}
