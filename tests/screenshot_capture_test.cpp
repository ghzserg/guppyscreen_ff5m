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
#include <sys/stat.h>
#include <string>

TEST_CASE("Screenshot: scripted") {
  const char *script_path = std::getenv("GUPPY_SCREENSHOT_SCRIPT");
  if (!script_path || !*script_path) return;
  const char *dir = std::getenv("GUPPY_SCREENSHOT_DIR");
  const bool has_dir = dir != nullptr && *dir != 0;
  REQUIRE_MESSAGE(has_dir, "GUPPY_SCREENSHOT_DIR is not set");
  std::ifstream script(script_path);
  REQUIRE_MESSAGE(script.good(), "Cannot open screenshot scenario: " << script_path);
  bool shaper_graphs = false;
  bool screw_adjustment = false;
  for (std::string line; std::getline(script, line);) {
    if (line == "tap Input Shaper") shaper_graphs = true;
    if (line == "tap Screw adjustment") screw_adjustment = true;
  }
  script.clear();
  script.seekg(0);

  json settings = json::object();
  if (const char *config = std::getenv("GUPPY_SCREENSHOT_CONFIG"))
    settings = json::parse(config);
  const char *profile = std::getenv("GUPPY_SCREENSHOT_PROFILE");
  const bool c5pro = profile && std::string(profile) == "c5pro";
  const bool ad5x = profile && std::string(profile) == "ad5x";
  if (profile && !c5pro && !ad5x && std::string(profile) != "ad5m")
    FAIL("Unknown screenshot profile: " << profile);
  if (c5pro || ad5x) {
    json config = json::parse(std::ifstream(app::fixture(c5pro ? "guppyconfig_c5pro.json" : "guppyconfig_ad5x.json")));
    const json &printer = config["printers"][c5pro ? "C5Pro" : "AD5X"];
    settings.merge_patch({{"printers", {{"FF5M", {
      {"monitored_sensors", printer["monitored_sensors"]},
      {"fans", printer["fans"]}
    }}}}});
  }
  FakeMoonraker moonraker(app::fixture(c5pro ? "c5pro_basic.json" : ad5x ? "ad5x_basic.json" : "ad5m_basic.json"));
  if (c5pro || ad5x) {
    json initial = moonraker.result("printer.objects.subscribe");
    initial["status"]["zmod_color"]["slots"] = json::array({
      {{"ID", "1"}, {"Material", "PLA"}, {"Color", "Blue"}, {"HEX", "3399FF"}, {"hasFilament", true}},
      {{"ID", "2"}, {"Material", "PETG"}, {"Color", "Pink"}, {"HEX", "E040A0"}, {"hasFilament", true}},
      {{"ID", "3"}, {"Material", ""}, {"Color", ""}, {"HEX", ""}, {"hasFilament", false}},
      {{"ID", "4"}, {"Material", "PLA"}, {"Color", "White"}, {"HEX", "FFFFFF"}, {"hasFilament", true}}
    });
    moonraker.set_result("printer.objects.subscribe", initial);
  }
  if (screw_adjustment) {
    auto initial = moonraker.result("printer.objects.subscribe");
    auto &status = initial["status"];
    status["configfile"]["settings"]["screws_tilt_adjust"] = {
      {"screw1", {12.5, 12.5}}, {"screw1_name", "Left Near"},
      {"screw2", {202, 12.5}}, {"screw2_name", "Right Near"},
      {"screw3", {202, 202}}, {"screw3_name", "Right Far"},
      {"screw4", {12.5, 202}}, {"screw4_name", "Left Far"}
    };
    status["screws_tilt_adjust"]["results"] = {
      {"screw1", {{"z", -1.8675}, {"is_base", true}, {"sign", "CW"}, {"adjust", "00:00"}}},
      {"screw2", {{"z", -1.8692}, {"is_base", false}, {"sign", "CW"}, {"adjust", "00:00"}}},
      {"screw3", {{"z", -1.9458}, {"is_base", false}, {"sign", "CW"}, {"adjust", "00:09"}}},
      {"screw4", {{"z", -2.0133}, {"is_base", false}, {"sign", "CCW"}, {"adjust", "00:18"}}}
    };
    moonraker.set_result("printer.objects.subscribe", initial);
  }
  PrintFile sample;
  sample.path = "cube.gcode";
  FakePrinter::Options options;
  std::string root;
  if (c5pro || shaper_graphs) {
    root = "/tmp/guppy-gallery-XXXXXX";
    REQUIRE(mkdtemp(root.data()) != nullptr);
    REQUIRE(mkdir((root + "/mod_data").c_str(), 0755) == 0);
    if (c5pro) {
      REQUIRE(mkdir((root + "/gcodes").c_str(), 0755) == 0);
      options.gcodes_dir = root + "/gcodes";
    }
    for (const char *axis : {"X", "Y"}) {
      const std::string filename = std::string("resonances_") + axis + ".png";
      std::ifstream in(app::fixture(filename), std::ios::binary);
      REQUIRE_MESSAGE(in.good(), "Missing Input Shaper sample: " << filename);
      std::ofstream out(root + "/mod_data/" + filename, std::ios::binary);
      out << in.rdbuf();
      REQUIRE(out.good());
    }
  }
  FakePrinter printer(moonraker, {sample}, options);
  if (c5pro || shaper_graphs) {
    moonraker.set_result("server.files.roots", json::array({
      {{"name", "config"}, {"path", root}, {"permissions", "rw"}},
      {{"name", "gcodes"}, {"path", c5pro ? options.gcodes_dir : "/data"}, {"permissions", "rw"}},
    }));
    if (c5pro)
      REQUIRE(std::ifstream(options.gcodes_dir + "/.thumbs/cube-300x300.png").good());
  }
  app::start(moonraker, settings);
  app::wait_ready(moonraker);
  if (c5pro) {
    ui::tap_tab(Nav::TUNE);
    REQUIRE_MESSAGE(ui::find_label("Calibrate extruders"), "C5 Pro lacks extruder calibration");
    REQUIRE_MESSAGE(ui::find_label("Calibrate VFA"), "C5 Pro lacks VFA calibration");
    ui::tap_tab(Nav::HOME);
    ui::tap_label("LED");
    REQUIRE_MESSAGE(ui::find_label("Chamber Led"), "C5 Pro LED panel is empty");
  }

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
      if (arg == "Screw adjustment" &&
          (ui::find_label("Measuring bed screws…") || ui::find_label("Measure again"))) {
        REQUIRE_MESSAGE(app::wait_until([] { return ui::table_has_row("Left Far"); }), ui::dump_screen());
      }
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
