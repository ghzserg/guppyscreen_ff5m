#include "doctest/doctest.h"
#include "app.h"
#include "fake_moonraker.h"
#include "fake_printer.h"
#include "screenshot.h"
#include "ui.h"

#include "guppyscreen.h"

#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>

TEST_CASE("Screenshot: scripted") {
  const char *script_path = std::getenv("GUPPY_SCREENSHOT_SCRIPT");
  if (!script_path || !*script_path) return;
  const char *dir = std::getenv("GUPPY_SCREENSHOT_DIR");
  const bool has_dir = dir != nullptr && *dir != 0;
  REQUIRE_MESSAGE(has_dir, "GUPPY_SCREENSHOT_DIR is not set");
  std::ifstream script(script_path);
  REQUIRE_MESSAGE(script.good(), "Cannot open screenshot scenario: " << script_path);

  json settings = json::object();
  if (const char *config = std::getenv("GUPPY_SCREENSHOT_CONFIG"))
    settings = json::parse(config);
  const char *profile = std::getenv("GUPPY_SCREENSHOT_PROFILE");
  const bool c5pro = profile && std::string(profile) == "c5pro";
  if (profile && !c5pro && std::string(profile) != "ad5m")
    FAIL("Unknown screenshot profile: " << profile);
  if (c5pro) {
    json config = json::parse(std::ifstream(app::fixture("guppyconfig_c5pro.json")));
    const json &printer = config["printers"]["C5Pro"];
    settings.merge_patch({{"printers", {{"FF5M", {
      {"monitored_sensors", printer["monitored_sensors"]},
      {"fans", printer["fans"]}
    }}}}});
  }
  FakeMoonraker moonraker(app::fixture(c5pro ? "c5pro_basic.json" : "ad5m_basic.json"));
  PrintFile sample;
  sample.path = "cube.gcode";
  FakePrinter printer(moonraker, {sample}, FakePrinter::Options{});
  app::start(moonraker, settings);
  app::wait_ready(moonraker);

  std::string line;
  int line_no = 0;
  while (std::getline(script, line)) {
    ++line_no;
    std::istringstream input(line);
    std::string command;
    input >> command;
    if (command.empty() || command[0] == '#') continue;
    std::string arg;
    std::getline(input, arg);
    const auto first = arg.find_first_not_of(" \t");
    arg = first == std::string::npos ? "" : arg.substr(first);
    INFO("Scenario line " << line_no << ": " << line);
    if (command == "tab") {
      if (arg == "home") ui::tap_tab(Nav::HOME);
      else if (arg == "print") ui::tap_tab(Nav::PRINT);
      else if (arg == "tune") ui::tap_tab(Nav::TUNE);
      else if (arg == "settings") ui::tap_tab(Nav::SETTINGS);
      else FAIL("Unknown tab: " << arg);
    } else if (command == "tap" || command == "tap_optional") {
      if (command == "tap_optional" && !ui::find_label(arg)) continue;
      ui::tap_label(arg);
    } else if (command == "tap_parent") {
      lv_obj_t *label = ui::find_label(arg);
      REQUIRE_MESSAGE(label != nullptr, "Missing label: " << arg);
      {
        std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
        lv_event_send(lv_obj_get_parent(label), LV_EVENT_SHORT_CLICKED, nullptr);
      }
      app::pump(20);
    } else if (command == "switch" || command == "switch_optional") {
      lv_obj_t *label = ui::find_label(arg);
      if (!label && command == "switch_optional") continue;
      REQUIRE_MESSAGE(label != nullptr, "Missing switch label: " << arg);
      lv_obj_t *toggle = nullptr;
      {
        std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
        toggle = ui::find_in(lv_obj_get_parent(label), &lv_switch_class);
      }
      REQUIRE_MESSAGE(toggle != nullptr, "Missing switch: " << arg);
      ui::tap(toggle);
    } else if (command == "tap_at") {
      int x = 0, y = 0;
      std::istringstream xy(arg);
      REQUIRE_MESSAGE(bool(xy >> x >> y), "Expected x y");
      ui::tap_at(x, y);
    } else if (command == "table_row") {
      REQUIRE_MESSAGE(app::wait_until([&] { return ui::table_has_row(arg); }), "File not found: " << arg);
      ui::tap_table_row(arg);
      app::settle(moonraker);
    } else if (command == "status") {
      moonraker.push_status(json::parse(arg));
      app::settle(moonraker);
    } else if (command == "sleep") {
      app::pump(std::stoi(arg));
    } else if (command == "save") {
      app::redraw();
      screenshot::save(std::string(dir) + "/" + arg + ".ppm");
    } else if (command == "memory") {
      screenshot::memory(std::string(dir) + "/" + arg + ".json");
    } else {
      FAIL("Unknown command: " << command);
    }
  }
}
