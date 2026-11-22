// The extruder screen shows the nozzles' tiles, like Home's, instead of the
// extruder line. With several nozzles (C5 Pro) they replace the T0-T3 buttons:
// a tap selects the nozzle, the active one has a blue background. With one
// nozzle (AD5M, AD5X) there is nothing to select; the AD5X keeps its T0-T3
// buttons (four tools on one nozzle).
// Цветные полоски под соплами такие же, как на Home и PID.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"
#include "nav.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <utility>
#include <vector>

using ui::tile;
using ui::tile_and_icon;

namespace {

// Start with `config_patch` (e.g. app::zmod_sensors(...)) and open Extrude.
void open_extruder_screen(FakeMoonraker &moonraker, const json &config_patch = json::object()) {
  app::start(moonraker, config_patch);
  app::wait_ready(moonraker);
  ui::tap_label("Extrude");
  // The screen sets itself up on the first status update; the printer sends
  // them all the time.
  moonraker.push_status({{"extruder", {{"temperature", 30.0}}}});
  app::settle(moonraker);
}

bool has_stripe(lv_obj_t *obj) {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  return lv_obj_get_style_border_width(obj, 0) > 0 && lv_obj_get_style_border_opa(obj, 0) > LV_OPA_TRANSP;
}

bool is_blue(lv_obj_t *obj) { return ui::has_bg(obj, LV_PALETTE_BLUE); }

// Whether T0-T3 buttons are shown (on top: other screens behind have some too).
bool tools_buttons_visible() { return ui::any_shown(ui::btnmatrices("T0")); }

}  // namespace

TEST_CASE("the C5 Pro extruder screen shows a tile per nozzle with its temperatures") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  open_extruder_screen(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  moonraker.push_status({{"extruder2", {{"temperature", 187.0}, {"target", 210.0}}}});
  app::settle(moonraker);

  auto t2 = ui::row_texts("T2");
  INFO("T2 tile: " << ui::join(t2));
  CHECK(std::find(t2.begin(), t2.end(), "187") != t2.end());
  CHECK(std::find(t2.begin(), t2.end(), "210") != t2.end());
  for (const char *t : {"T0", "T1", "T3"}) CHECK(ui::find_label(t) != nullptr);
  for (const char *t : {"T0", "T1", "T2", "T3"}) CHECK(has_stripe(tile(t)));
  // No single extruder line and no T0-T3 buttons.
  CHECK(ui::find_label("Extruder") == nullptr);
  CHECK_FALSE(tools_buttons_visible());
  auto last_tool = ui::coords(tile("T3"));
  auto remove = ui::coords(tile("Remove"));
  CHECK_FALSE(has_stripe(tile("Remove")));
  CHECK(remove.x1 > last_tool.x2);
  CHECK(remove.y1 == last_tool.y1);
  CHECK(remove.y2 == last_tool.y2);
}

// Нажатие на плитку сразу отправляет команду переключения инструмента.
TEST_CASE("a tap on a C5 Pro nozzle tile selects and switches the tool immediately") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  open_extruder_screen(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));

  CHECK(is_blue(tile("T0")));  // active_tool_id 0 in the fixture
  CHECK_FALSE(is_blue(tile("T2")));
  ui::tap(tile("T0"));
  app::settle(moonraker);
  CHECK(moonraker.gcodes().empty());

  ui::tap(tile("T2"));
  app::settle(moonraker);
  CHECK(is_blue(tile("T2")));
  CHECK_FALSE(is_blue(tile("T0")));
  REQUIRE(moonraker.gcodes().size() == 1);
  CHECK(moonraker.gcodes().back() == "_T_USE T=2");

  // A tool change on the printer shows.
  moonraker.push_status({{"zmod_color", {{"active_tool_id", 3}}}});
  CHECK(app::wait_until([&] { return is_blue(tile("T3")); }));
  CHECK_FALSE(is_blue(tile("T2")));
}

TEST_CASE("Remove extruder parks the selected C5 Pro tool") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  open_extruder_screen(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));

  REQUIRE(ui::find_label("Remove") != nullptr);
  ui::tap(tile("T2"));
  ui::tap_label("Remove");
  REQUIRE(app::wait_until([&] { return moonraker.gcodes().size() == 2; }));
  CHECK(moonraker.gcodes()[0] == "_T_USE T=2");
  CHECK(moonraker.gcodes()[1] == "_T_OUT");

  moonraker.push_status({{"zmod_color", {{"active_tool_id", -1}}}});
  app::settle(moonraker);
  CHECK_FALSE(is_blue(tile("T2")));
  ui::tap_label("Remove");
  app::settle(moonraker);
  CHECK(moonraker.gcodes().size() == 2);
}

TEST_CASE("Remove extruder does nothing if the C5 Pro carriage is empty") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  open_extruder_screen(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  moonraker.push_status({{"zmod_color", {{"active_tool_id", -1}}}});
  app::settle(moonraker);

  REQUIRE(ui::find_label("Remove") != nullptr);
  ui::tap_label("Remove");
  app::settle(moonraker);
  CHECK(moonraker.gcodes().empty());
}

TEST_CASE("Remove extruder is blocked while printing and allowed while paused on C5 Pro") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  open_extruder_screen(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));

  for (const char *status : {"printing", "paused"}) {
    moonraker.push_status({{"print_stats", {{"filename", "cube.gcode"}, {"state", status}}}});
    app::settle(moonraker);
    ui::tap_tab(Nav::HOME);
    ui::tap_label("Extrude");
    app::settle(moonraker);

    ui::tap_label("Remove");
    app::settle(moonraker);
    if (std::string(status) == "printing") {
      CHECK(moonraker.gcodes().empty());
    } else {
      REQUIRE(moonraker.gcodes().size() == 1);
      CHECK(moonraker.gcodes().front() == "_T_OUT");
    }
  }
}

TEST_CASE("Load on a C5 Pro nozzle not in use changes the tool first") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  open_extruder_screen(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));

  ui::tap(tile("T2"));
  ui::tap_label("Load");
  REQUIRE(app::wait_until([&] { return !moonraker.gcodes().empty(); }));
  std::string g = moonraker.gcodes().back();
  INFO(g);
  REQUIRE(moonraker.gcodes().size() == 2);
  CHECK(moonraker.gcodes().front() == "_T_USE T=2");
  CHECK(g.rfind("LOAD_FILAMENT T=2 ", 0) == 0);
}

// Unload: M109 and G1 E on the active tool.
TEST_CASE("Unload on a C5 Pro nozzle not in use changes the tool first") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  open_extruder_screen(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));

  ui::tap(tile("T1"));
  ui::tap_label("Unload");
  REQUIRE(app::wait_until([&] { return !moonraker.gcodes().empty(); }));
  std::string g = moonraker.gcodes().back();
  INFO(g);
  REQUIRE(moonraker.gcodes().size() == 2);
  CHECK(moonraker.gcodes().front() == "_T_USE T=1");
  CHECK(g.rfind("M109 S", 0) == 0);
  CHECK(g.find(" T1\n") != std::string::npos);
}

TEST_CASE("Load on the C5 Pro nozzle in use doesn't change the tool") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  open_extruder_screen(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));

  ui::tap_label("Load");
  REQUIRE(app::wait_until([&] { return !moonraker.gcodes().empty(); }));
  std::string g = moonraker.gcodes().back();
  INFO(g);
  CHECK(g.rfind("LOAD_FILAMENT T=0 ", 0) == 0);
}

TEST_CASE("a C5 Pro nozzle can't be picked during a print") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  open_extruder_screen(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  moonraker.push_status({{"print_stats", {{"filename", "cube.gcode"}, {"state", "printing"}}}});
  app::settle(moonraker);
  ui::tap_tab(Nav::HOME);
  ui::tap_label("Extrude");
  app::settle(moonraker);

  ui::tap(tile("T2"));
  app::settle(moonraker);
  CHECK(is_blue(tile("T0")));
  CHECK_FALSE(is_blue(tile("T2")));
  CHECK(moonraker.gcodes().empty());
}

TEST_CASE("the AD5X extruder screen shows its nozzle's tile and keeps the T0-T3 buttons") {
  FakeMoonraker moonraker(app::fixture("ad5x_basic.json"));
  open_extruder_screen(moonraker, app::zmod_sensors("guppyconfig_ad5x.json", "AD5X"));
  moonraker.push_status({{"extruder", {{"temperature", 187.0}, {"target", 210.0}}}});
  app::settle(moonraker);

  auto texts = ui::row_texts("Extruder");
  INFO("Extruder tile: " << ui::join(texts));
  CHECK(std::find(texts.begin(), texts.end(), "187") != texts.end());
  CHECK(std::find(texts.begin(), texts.end(), "210") != texts.end());
  CHECK(std::find(texts.begin(), texts.end(), "/") == texts.end());  // a tile, not the line
  CHECK(tools_buttons_visible());
  CHECK(ui::find_label("Remove") == nullptr);
}

TEST_CASE("the AD5M nozzle tile on the extruder screen is not selectable") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  open_extruder_screen(moonraker);

  auto texts = ui::row_texts("Extruder");
  CHECK(std::find(texts.begin(), texts.end(), "/") == texts.end());
  CHECK_FALSE(is_blue(tile("Extruder")));
  CHECK(has_stripe(tile("Extruder")));
  CHECK(ui::find_label("Remove") == nullptr);
  ui::tap(tile("Extruder"));
  app::settle(moonraker);
  CHECK_FALSE(is_blue(tile("Extruder")));
  CHECK(moonraker.gcodes().empty());
}

TEST_CASE("the extruder screen's nozzle tile sits where Home's extruder tile does") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  auto home = tile_and_icon("Extruder");
  ui::tap_label("Extrude");
  moonraker.push_status({{"extruder", {{"temperature", 30.0}}}});
  app::settle(moonraker);
  auto ext = tile_and_icon("Extruder");

  INFO("home tile " << home.first.x1 << "," << home.first.y1 << " icon " << home.second.x1 << "," << home.second.y1
       << "; extrude tile " << ext.first.x1 << "," << ext.first.y1 << " icon " << ext.second.x1 << "," << ext.second.y1);
  CHECK(ext.first.x1 == home.first.x1);
  CHECK(ext.first.y1 == home.first.y1);
  CHECK(ext.second.x1 == home.second.x1);
  CHECK(ext.second.y1 == home.second.y1);
}

// Rectangular, as on the PID screen: the corners of Home's tiles, not of
// the temperature buttons.
TEST_CASE("the selected nozzle's background has the corners of Home's tiles") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  app::start(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  app::wait_ready(moonraker);
  lv_obj_t *home_bed = tile("Bed");
  lv_coord_t want;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    want = lv_obj_get_style_radius(home_bed, 0);
  }
  ui::tap_label("Extrude");
  moonraker.push_status({{"extruder", {{"temperature", 30.0}}}});
  app::settle(moonraker);
  auto temps = ui::btnmatrices("200");
  REQUIRE(!temps.empty());
  lv_obj_t *t0 = tile("T0");
  REQUIRE(is_blue(t0));
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  INFO("tile radius " << lv_obj_get_style_radius(t0, 0) << ", Home's " << want
       << ", buttons' " << lv_obj_get_style_radius(temps.front(), LV_PART_ITEMS));
  CHECK(lv_obj_get_style_radius(t0, 0) == want);
}

// A tile's tool is the number in its extruder's name, not its place in the row.
TEST_CASE("a C5 Pro nozzle left out of the config keeps the other tiles' tools") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  open_extruder_screen(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro", "extruder1"));

  ui::tap(tile("T2"));
  ui::tap_label("Load");
  REQUIRE(app::wait_until([&] { return !moonraker.gcodes().empty(); }));
  REQUIRE(moonraker.gcodes().size() == 2);
  CHECK(moonraker.gcodes().front() == "_T_USE T=2");
  CHECK(moonraker.gcodes().back().rfind("LOAD_FILAMENT T=2 ", 0) == 0);

  moonraker.push_status({{"zmod_color", {{"active_tool_id", 3}}}});
  CHECK(app::wait_until([&] { return is_blue(tile("T3")); }));
  CHECK_FALSE(is_blue(tile("T2")));
}

// No tool in the head: nothing selected and Load off until a nozzle is picked.
TEST_CASE("a C5 Pro nozzle picked with no tool in the head enables Load") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  open_extruder_screen(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  moonraker.push_status({{"zmod_color", {{"active_tool_id", -1}}}});
  app::settle(moonraker);
  CHECK_FALSE(is_blue(tile("T0")));

  ui::tap_label("Load");
  app::settle(moonraker);
  CHECK(moonraker.gcodes().empty());

  ui::tap(tile("T1"));
  app::settle(moonraker);
  REQUIRE(moonraker.gcodes().size() == 1);
  CHECK(moonraker.gcodes().front() == "_T_USE T=1");
  ui::tap_label("Load");
  REQUIRE(app::wait_until([&] { return moonraker.gcodes().size() == 2; }));
  CHECK(moonraker.gcodes().back().rfind("LOAD_FILAMENT T=1 ", 0) == 0);
}

// One nozzle: a tap picks nothing, the buttons follow the printer's tool.
TEST_CASE("a tap on the AD5X nozzle with no channel active keeps Load off") {
  FakeMoonraker moonraker(app::fixture("ad5x_basic.json"));
  open_extruder_screen(moonraker, app::zmod_sensors("guppyconfig_ad5x.json", "AD5X"));
  moonraker.push_status({{"zmod_color", {{"active_tool_id", -1}}}});
  app::settle(moonraker);

  ui::tap(tile("Extruder"));
  ui::tap_label("Load");
  app::settle(moonraker);
  CHECK(moonraker.gcodes().empty());
}

// COLOR доступен без активного сопла и не вызывает переключение инструмента.
TEST_CASE("COLOR works without an active C5 Pro tool") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  open_extruder_screen(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  moonraker.push_status({{"zmod_color", {{"active_tool_id", -1}}}});
  app::settle(moonraker);

  ui::tap_label("COLOR");
  REQUIRE(app::wait_until([&] { return moonraker.gcodes().size() == 1; }));
  CHECK(moonraker.gcodes().front().rfind("_GUPPY_MACRO T=-1 ", 0) == 0);

  ui::tap(tile("T3"));
  app::settle(moonraker);
  REQUIRE(moonraker.gcodes().size() == 2);
  CHECK(moonraker.gcodes().back() == "_T_USE T=3");

  ui::tap_label("COLOR");
  REQUIRE(app::wait_until([&] { return moonraker.gcodes().size() == 3; }));
  CHECK(moonraker.gcodes().back().rfind("_GUPPY_MACRO T=3 ", 0) == 0);
}

TEST_CASE("ColdPull does not switch a C5 Pro tool again") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  open_extruder_screen(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));

  ui::tap(tile("T2"));
  app::settle(moonraker);
  REQUIRE(moonraker.gcodes().size() == 1);
  CHECK(moonraker.gcodes().front() == "_T_USE T=2");

  ui::tap_label("ColdPull");
  REQUIRE(app::wait_until([&] { return moonraker.gcodes().size() == 2; }));
  CHECK(moonraker.gcodes().back() == "COLDPULL T=2");
}
