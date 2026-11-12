// Macros tab: the macro list comes from Klipper's configfile (gcode_macro
// sections), with parameters and their defaults parsed from the G-code.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>

namespace {

bool contains(const std::vector<std::string> &v, const std::string &s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

// All labels with `text`, shown or not (the macro list of a hidden tab).
std::vector<lv_obj_t *> labels_named(const char *text) {
  std::vector<lv_obj_t *> found;
  std::function<void(lv_obj_t *)> walk = [&](lv_obj_t *o) {
    if (lv_obj_check_type(o, &lv_label_class) && strcmp(lv_label_get_text(o), text) == 0) found.push_back(o);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(o); i++) walk(lv_obj_get_child(o, i));
  };
  walk(lv_scr_act());
  return found;
}

lv_obj_t *find_label_anywhere(const char *text) {
  auto found = labels_named(text);
  return found.empty() ? nullptr : found.front();
}

size_t count_labels(const char *text) {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  return labels_named(text).size();
}

// Tap tab `idx` on the main tabview's tab bar (the tabview that holds the
// macros panel's "Show Hidden" label), as a finger would.
void tap_tab(uint32_t idx) {
  lv_point_t p;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_t *tv = find_label_anywhere("Show Hidden");
    while (tv && !lv_obj_check_type(tv, &lv_tabview_class)) tv = lv_obj_get_parent(tv);
    REQUIRE(tv != nullptr);
    uint32_t n = lv_obj_get_child_cnt(lv_tabview_get_content(tv));
    REQUIRE(idx < n);
    lv_obj_update_layout(tv);
    lv_area_t a;
    lv_obj_get_coords(lv_tabview_get_tab_btns(tv), &a);  // a vertical bar on the left
    p = {(lv_coord_t)((a.x1 + a.x2) / 2), (lv_coord_t)(a.y1 + (2 * idx + 1) * lv_area_get_height(&a) / (2 * n))};
  }
  ui::tap_at(p.x, p.y);
  app::pump(100);
}

constexpr uint32_t HOME_TAB = 0, MACROS_TAB = 3;
void open_macros_tab() { tap_tab(MACROS_TAB); }
void open_home_tab() { tap_tab(HOME_TAB); }

}  // namespace

TEST_CASE("macros tab lists macros and runs one with its default parameter") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  std::ifstream f(app::fixture("ad5m_basic.json"));
  json sub = json::parse(f)["printer.objects.subscribe"];
  sub["status"]["configfile"]["config"]["gcode_macro _INTERNAL_HELPER"] = {{"gcode", "\nM400"}};
  moonraker.set_result("printer.objects.subscribe", sub);

  app::start(moonraker);
  app::wait_ready(moonraker);

  open_macros_tab();
  REQUIRE_MESSAGE(app::wait_until([] { return ui::find_label("START_PRINT") != nullptr; }),
                  "macros tab not shown; screen: " << ui::dump_screen());

  auto screen = ui::dump_screen();
  CHECK_MESSAGE(ui::find_label("END_PRINT") != nullptr, screen);
  CHECK_MESSAGE(ui::find_label("LOAD_FILAMENT") != nullptr, screen);
  CHECK_MESSAGE(ui::find_label("BED_TEMP") != nullptr, screen);
  CHECK_MESSAGE(ui::find_label("_INTERNAL_HELPER") == nullptr, screen);

  // The run button sits next to the macro name.
  lv_obj_t *run_btn = nullptr;
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_obj_t *name = find_label_anywhere("START_PRINT");
    lv_obj_t *row = lv_obj_get_parent(name);
    for (uint32_t i = 0; i < lv_obj_get_child_cnt(row); i++) {
      lv_obj_t *c = lv_obj_get_child(row, i);
      if (lv_obj_check_type(c, &lv_btn_class)) run_btn = c;
    }
  }
  REQUIRE(run_btn != nullptr);
  ui::tap(run_btn);
  bool sent = app::wait_until([&] { return contains(moonraker.gcodes(), "START_PRINT BED_TEMP=60"); });
  CHECK_MESSAGE(sent, "G-codes sent: " << ui::join(moonraker.gcodes()));
}

void reconnect(FakeMoonraker &moonraker) {
  size_t before = moonraker.request_count("printer.objects.subscribe");
  moonraker.drop_client();
  REQUIRE(app::wait_until([&] { return moonraker.request_count("printer.objects.subscribe") > before; }, 10000));
  app::pump(500);
}

// docs/ram-optimization.md item 6: ~12 LVGL objects per macro (zMod: ~1,300
// objects, ~330 kB on the printer) are created when the Macros tab is first
// opened, not on every connect. A reconnect rebuilds an open list and frees
// a closed one.
TEST_CASE("macro list is built when the macros tab is first opened") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  app::pump(200);
  CHECK(count_labels("START_PRINT") == 0);

  open_macros_tab();
  REQUIRE(app::wait_until([] { return ui::find_label("START_PRINT") != nullptr; }));
  CHECK(count_labels("START_PRINT") == 1);

  open_home_tab();
  open_macros_tab();
  CHECK(count_labels("START_PRINT") == 1);  // kept, not built twice

  reconnect(moonraker);  // with the tab open
  CHECK(count_labels("START_PRINT") == 1);

  open_home_tab();
  reconnect(moonraker);  // with the tab closed
  CHECK(count_labels("START_PRINT") == 0);

  open_macros_tab();
  CHECK(app::wait_until([] { return ui::find_label("START_PRINT") != nullptr; }));
  CHECK(count_labels("START_PRINT") == 1);
}

#include "state.h"

// Every configfile path the app reads is in State after startup and after a
// configfile notification, and the rest is dropped (docs/ram-optimization.md
// item 5).
TEST_CASE("configfile values used by panels are kept in State") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  std::ifstream f(app::fixture("ad5m_basic.json"));
  json sub = json::parse(f)["printer.objects.subscribe"];
  json &cf = sub["status"]["configfile"];
  cf["config"]["input_shaper"] = {{"shaper_type_x", "mzv"}, {"shaper_freq_x", "50.0"}};
  cf["settings"]["input_shaper"] = {{"shaper_type_x", "mzv"}, {"shaper_freq_x", 50.0}};
  moonraker.set_result("printer.objects.subscribe", sub);

  app::start(moonraker);
  app::wait_ready(moonraker);

  auto get = [](const char *path) {
    return State::get_instance()->get_data(json::json_pointer(std::string("/printer_state/configfile") + path));
  };
  auto check_all = [&](const char *when) {
    CAPTURE(when);
    CHECK(get("/config/extruder/filament_diameter") == "1.750");
    CHECK(get("/config/input_shaper/shaper_type_x") == "mzv");
    CHECK(get("/settings/extruder/pressure_advance") == 0.025);
    CHECK(get("/settings/firmware_retraction/retract_length") == 0.8);
    CHECK(get("/settings/printer/max_velocity") == 600.0);
    // ...and nothing else: the macro text is the bulk of configfile.
    CHECK_FALSE(get("/config").contains("gcode_macro START_PRINT"));
    CHECK_FALSE(get("/config").contains("firmware_retraction"));
  };
  check_all("after startup");
  CHECK(get("/save_config_pending") == false);

  moonraker.push_status({{"configfile", {{"save_config_pending", true},
                                         {"config", cf["config"]}, {"settings", cf["settings"]}}}});
  CHECK(app::wait_until([&] { return get("/save_config_pending") == true; }));
  check_all("after a configfile notification");
}

// The macro text is not kept from the subscribe reply (it is dropped while
// parsing, see configfile_filter_test.cpp). The Macros tab asks for it with
// printer.objects.query when it builds the list, so it gets the current
// macros after a config change and a reconnect.
TEST_CASE("macros tab fetches the macro text when it builds the list") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  std::ifstream f(app::fixture("ad5m_basic.json"));
  json sub = json::parse(f)["printer.objects.subscribe"];
  app::start(moonraker);
  app::wait_ready(moonraker);
  app::pump(200);
  CHECK(moonraker.request_count("printer.objects.query") == 0);
  CHECK_FALSE(State::get_instance()->get_data_copy("/printer_state/configfile/config"_json_pointer)
                .contains("gcode_macro START_PRINT"));

  // The config changes before the tab is first opened.
  sub["status"]["configfile"]["config"]["gcode_macro NEW_ONE"] = {{"gcode", "\nM400"}};
  moonraker.set_result("printer.objects.subscribe", sub);

  open_macros_tab();
  REQUIRE_MESSAGE(app::wait_until([] { return ui::find_label("NEW_ONE") != nullptr; }),
                  "screen: " << ui::dump_screen());
  CHECK(ui::find_label("START_PRINT") != nullptr);
  auto queries = moonraker.requests("printer.objects.query");
  REQUIRE(queries.size() == 1);
  CHECK(queries[0] == json({{"objects", {{"configfile", {"config"}}}}}));

  open_home_tab();
  open_macros_tab();
  CHECK(moonraker.request_count("printer.objects.query") == 1);  // kept, not fetched again

  // Klipper restarts with another macro set while the tab is open.
  sub["status"]["configfile"]["config"].erase("gcode_macro NEW_ONE");
  sub["status"]["configfile"]["config"]["gcode_macro NEWER_ONE"] = {{"gcode", "\nM400"}};
  moonraker.set_result("printer.objects.subscribe", sub);
  reconnect(moonraker);
  CHECK(app::wait_until([] { return ui::find_label("NEWER_ONE") != nullptr; }));
  CHECK(count_labels("NEW_ONE") == 0);
  CHECK(count_labels("START_PRINT") == 1);
}
