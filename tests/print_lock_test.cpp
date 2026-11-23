#include "doctest/doctest.h"
#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"
#include "guppyscreen.h"

#include <cstring>
#include <mutex>
#include <vector>

namespace {

const char *printing_only_buttons[] = {
    "X+", "X-", "Y+", "Y-", "Z+", "Z-",
    "Load", "Unload", "Cool down", "ColdPull"
};

const char *job_locked_buttons[] = {
    "G28", "G28 XY", "Motor off",
    "Save", "Calibrate", "Shake Belts", "Excitate", "PID Extruder", "PID Bed",
    "Clear nozzle", "Screw adjustment", "80 °C", "Calibrate extruders", "Calibrate VFA",
    "Native screen", "Restore Print", "Clean", "Cold"
};

void check_button(const char *name, bool disabled, bool send = false) {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  std::vector<lv_obj_t *> labels;
  ui::walk(lv_scr_act(), [&](lv_obj_t *obj) {
    if (!lv_obj_check_type(obj, &lv_label_class) || strcmp(lv_label_get_text(obj), name)) return;
    auto *parent = lv_obj_get_parent(obj);
    if (!parent) return;
    // Z+ и Z- есть также в настройках Z-offset, которые доступны при печати.
    if (!strcmp(name, "Z+") || !strcmp(name, "Z-")) {
      bool homing = false;
      ui::walk(lv_obj_get_parent(parent), [&](lv_obj_t *child) {
        if (lv_obj_check_type(child, &lv_label_class) &&
            !strcmp(lv_label_get_text(child), "G28")) homing = true;
      });
      if (!homing) return;
    }
    auto *img = lv_obj_get_child(parent, 0);
    bool screw_control = !strcmp(name, "Screw adjustment") || !strcmp(name, "245 °C") || !strcmp(name, "80 °C");
    if ((img && lv_obj_check_type(img, &lv_imgbtn_class)) ||
        (screw_control && lv_obj_check_type(parent, &lv_btn_class))) labels.push_back(obj);
  });
  INFO(std::string(name));
  REQUIRE(!labels.empty());
  if (!strcmp(name, "Save") || !strcmp(name, "Calibrate")) CHECK(labels.size() >= 2);
  for (auto *label : labels) {
    auto *parent = lv_obj_get_parent(label);
    bool expected_disabled = disabled;
    CHECK(lv_obj_has_state(parent, LV_STATE_DISABLED) == expected_disabled);
    if (lv_obj_check_type(parent, &lv_btn_class)) {
      CHECK(lv_obj_has_flag(parent, LV_OBJ_FLAG_CLICKABLE) != expected_disabled);
      if (send) lv_event_send(parent, LV_EVENT_SHORT_CLICKED, nullptr);
      continue;
    }
    CHECK(lv_obj_has_state(label, LV_STATE_DISABLED) == expected_disabled);
    CHECK(lv_obj_has_state(lv_obj_get_child(parent, 0), LV_STATE_DISABLED) == expected_disabled);
    if (send) lv_event_send(parent, LV_EVENT_SHORT_CLICKED, nullptr);
  }
}

bool disabled(lv_obj_t *obj) {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  return lv_obj_has_state(obj, LV_STATE_DISABLED);
}

} // namespace

TEST_CASE("print locks all unsafe buttons even on connecting during a print") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  auto subscription = moonraker.result("printer.objects.subscribe");
  subscription["status"]["print_stats"]["state"] = "printing";
  subscription["status"]["print_stats"]["filename"] = "cube.gcode";
  moonraker.set_result("printer.objects.subscribe", subscription);
  app::start(moonraker, app::zmod_sensors("guppyconfig_c5pro.json", "C5Pro"));
  app::wait_ready(moonraker);

  for (auto *name : printing_only_buttons) check_button(name, true, true);
  for (auto *name : job_locked_buttons) check_button(name, true, true);
  check_button("Load Cell", false);
  check_button("COLOR", false);
  check_button("Spoolman", false);
  app::settle(moonraker);
  CHECK(moonraker.gcodes().empty());

  // Обновления осей и инструмента не должны снимать блокировку печати.
  moonraker.push_status({{"toolhead", {{"homed_axes", "xy"}}},
                        {"zmod_color", {{"active_tool_id", 2}}}});
  app::settle(moonraker);
  for (auto *name : printing_only_buttons) check_button(name, true);
  for (auto *name : job_locked_buttons) check_button(name, true);
  ui::tap_tab(Nav::HOME);
  ui::tap_label("Extrude");
  app::settle(moonraker);
  for (auto *name : {"T0", "T1", "T2", "T3"}) CHECK(disabled(ui::tile(name)));
  CHECK(disabled(ui::tile("Remove")));
  ui::tap(ui::tile("T1"));
  app::settle(moonraker);
  CHECK(moonraker.gcodes().empty());

  moonraker.push_status({{"print_stats", {{"state", "paused"}}}});
  app::settle(moonraker);
  for (auto *name : printing_only_buttons) check_button(name, !strcmp(name, "Z+") || !strcmp(name, "Z-"));
  for (auto *name : job_locked_buttons) check_button(name, true, true);
  check_button("Load Cell", false);
  check_button("COLOR", false);
  check_button("Spoolman", false);
  app::settle(moonraker);
  CHECK(moonraker.gcodes().empty());
  ui::tap_tab(Nav::HOME);
  ui::tap_label("Extrude");
  app::settle(moonraker);
  for (auto *name : {"T0", "T1", "T2", "T3"}) CHECK_FALSE(disabled(ui::tile(name)));
  CHECK_FALSE(disabled(ui::tile("Remove")));
  ui::tap(ui::tile("T1"));
  REQUIRE(app::wait_until([&] { return !moonraker.gcodes().empty(); }));
  CHECK(moonraker.gcodes().back() == "_T_USE T=1");
  ui::tap(ui::tile("Remove"));
  REQUIRE(app::wait_until([&] { return moonraker.gcodes().size() == 2; }));
  CHECK(moonraker.gcodes().back() == "_T_OUT");

  moonraker.push_status({{"print_stats", {{"state", "printing"}}}});
  app::settle(moonraker);
  for (auto *name : printing_only_buttons) check_button(name, true);
  for (auto *name : job_locked_buttons) check_button(name, true);

  moonraker.push_status({{"print_stats", {{"state", "complete"}}}});
  app::settle(moonraker);
  for (auto *name : printing_only_buttons) check_button(name, !strcmp(name, "Z+") || !strcmp(name, "Z-"));
  for (auto *name : job_locked_buttons) check_button(name, false);
  ui::tap_tab(Nav::HOME);
  ui::tap_label("Extrude");
  app::settle(moonraker);
  CHECK_FALSE(disabled(ui::tile("T1")));
  CHECK_FALSE(disabled(ui::tile("Remove")));

  // При следующем старте печати блокировка должна включиться снова.
  moonraker.push_status({{"print_stats", {{"state", "printing"}}}});
  app::settle(moonraker);
  for (auto *name : printing_only_buttons) check_button(name, true);
  for (auto *name : job_locked_buttons) check_button(name, true);
}

TEST_CASE("AD5M screw nozzle temperature is locked while printing") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  auto subscription = moonraker.result("printer.objects.subscribe");
  subscription["status"]["print_stats"]["state"] = "printing";
  subscription["status"]["print_stats"]["filename"] = "cube.gcode";
  moonraker.set_result("printer.objects.subscribe", subscription);
  app::start(moonraker);
  app::wait_ready(moonraker);

  check_button("245 °C", true, true);
  app::settle(moonraker);
  CHECK(moonraker.gcodes().empty());
}

TEST_CASE("configuration change dialog waits until the print has ended") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  moonraker.push_status({{"print_stats", {{"filename", "cube.gcode"}, {"state", "printing"}}},
                        {"configfile", {{"save_config_pending", true}}}});
  app::settle(moonraker);
  CHECK(ui::find_label("Configuration change detected") == nullptr);
  moonraker.push_status({{"print_stats", {{"state", "paused"}}}});
  app::settle(moonraker);
  CHECK(ui::find_label("Configuration change detected") == nullptr);
  moonraker.push_status({{"print_stats", {{"state", "complete"}}}});
  CHECK(app::wait_until([&] { return ui::find_label("Configuration change detected") != nullptr; }));
}
