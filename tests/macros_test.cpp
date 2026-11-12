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
#include <mutex>

namespace {

bool contains(const std::vector<std::string> &v, const std::string &s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

lv_obj_t *find_label_anywhere(lv_obj_t *obj, const char *text) {
  if (lv_obj_check_type(obj, &lv_label_class) && strcmp(lv_label_get_text(obj), text) == 0) return obj;
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); i++) {
    if (lv_obj_t *found = find_label_anywhere(lv_obj_get_child(obj, i), text)) return found;
  }
  return nullptr;
}

// The tab bar buttons are a button matrix with symbols, not labels, so
// switch to the tab that holds `label_text` through the tabview.
void open_tab_with(const char *label_text) {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  lv_obj_t *label = find_label_anywhere(lv_scr_act(), label_text);
  REQUIRE_MESSAGE(label != nullptr, "no label " << label_text << " anywhere");
  for (lv_obj_t *o = label; o != NULL; o = lv_obj_get_parent(o)) {
    lv_obj_t *parent = lv_obj_get_parent(o);
    lv_obj_t *grand = parent ? lv_obj_get_parent(parent) : NULL;
    if (grand && lv_obj_check_type(grand, &lv_tabview_class)) {
      lv_tabview_set_act(grand, lv_obj_get_index(o), LV_ANIM_OFF);
      return;
    }
  }
  FAIL("label " << label_text << " is not in a tabview");
}

}  // namespace

TEST_CASE("macros tab lists macros and runs one with its default parameter") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  std::ifstream f(app::fixture("ad5m_basic.json"));
  json sub = json::parse(f)["printer.objects.subscribe"];
  sub["status"]["configfile"]["config"]["gcode_macro _INTERNAL_HELPER"] = {{"gcode", "\nM400"}};
  moonraker.set_result("printer.objects.subscribe", sub);

  app::start(moonraker);
  app::wait_ready(moonraker);

  open_tab_with("START_PRINT");
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
    lv_obj_t *name = find_label_anywhere(lv_scr_act(), "START_PRINT");
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
