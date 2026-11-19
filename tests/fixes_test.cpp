// Fixes of the screens as they were: the AD5X's nozzle temperature on the
// print screen, Fine Tune's percents, Cancel, and the
// numpad of a sensor deleted by a reconnect. The thumbnail fixes are in
// thumbnail_test.cpp.

#include "doctest/doctest.h"

#include "app.h"
#include "fake_moonraker.h"
#include "ui.h"

#include "guppyscreen.h"
#include "lv_tc_screen.h"
#include "state.h"
#include "utils.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>

namespace {

// fn on obj and every object under it. Hold the LVGL lock.
void walk(lv_obj_t *obj, const std::function<void(lv_obj_t *)> &fn) {
  fn(obj);
  for (uint32_t i = 0; i < lv_obj_get_child_cnt(obj); i++) walk(lv_obj_get_child(obj, i), fn);
}

// Console is a button in Settings.
void open_console() {
  ui::tap_tab(Nav::SETTINGS);
  ui::tap_label("Console");
  app::pump(100);
}

// The print screen's nozzle label ("T<n>: ..."), shown or not.
std::string nozzle_label() {
  std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
  std::string text;
  walk(lv_scr_act(), [&](lv_obj_t *o) {
    if (!text.empty() || !lv_obj_check_type(o, &lv_label_class)) return;
    const char *t = lv_label_get_text(o);
    if (t && t[0] == 'T' && t[1] >= '0' && t[1] <= '9' && t[2] == ':') text = t;
  });
  return text;
}

bool sent(FakeMoonraker &moonraker, const std::string &gcode) {
  auto g = moonraker.gcodes();
  return std::find(g.begin(), g.end(), gcode) != g.end();
}

}  // namespace

TEST_CASE("the AD5X print screen shows the active channel with its one nozzle's temperature") {
  FakeMoonraker moonraker(app::fixture("ad5x_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  // T2 is a filament channel: there is no extruder2, the nozzle is extruder.
  moonraker.push_status({{"zmod_color", {{"active_tool_id", 2}}}});
  app::settle(moonraker);
  moonraker.push_status({{"extruder", {{"temperature", 230.0}, {"target", 230.0}}}});
  CHECK_MESSAGE(app::wait_until([] { return nozzle_label() == "T2: 230 / 230"; }), "label: " << nozzle_label());
}

TEST_CASE("the C5 Pro print screen shows the active nozzle's temperature") {
  FakeMoonraker moonraker(app::fixture("c5pro_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  moonraker.push_status({{"zmod_color", {{"active_tool_id", 2}}}});
  app::settle(moonraker);
  moonraker.push_status({{"extruder2", {{"temperature", 230.0}, {"target", 230.0}}}});
  REQUIRE_MESSAGE(app::wait_until([] { return nozzle_label() == "T2: 230 / 230"; }), "label: " << nozzle_label());

  moonraker.push_status({{"extruder", {{"temperature", 180.0}, {"target", 180.0}}}});  // T0's: not shown
  app::settle(moonraker);
  CHECK(nozzle_label() == "T2: 230 / 230");
}

TEST_CASE("Fine Tune shows Klipper's 1.15 as 115% and steps from it") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);
  moonraker.push_status({{"gcode_move", {{"speed_factor", 1.15}, {"extrude_factor", 1.15}}}});
  app::settle(moonraker);

  ui::tap_label("Fine Tune");
  REQUIRE_MESSAGE(app::wait_until([] { return ui::find_label("115%") != nullptr; }), ui::dump_screen());

  ui::tap_label("Speed+");  // the step is 1 (the multiplier's first)
  CHECK_MESSAGE(app::wait_until([&] { return sent(moonraker, "M220 S116"); }),
                "gcodes: " << ui::join(moonraker.gcodes()));
  ui::tap_label("Flow-");
  CHECK_MESSAGE(app::wait_until([&] { return sent(moonraker, "M221 S114"); }),
                "gcodes: " << ui::join(moonraker.gcodes()));
}

// With prompt_emergency_stop off, Cancel runs at once. The print status
// stays until Klipper reports the print cancelled.
TEST_CASE("Cancel keeps the print status until Klipper ends the print") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker, {{"prompt_emergency_stop", false}});
  app::wait_ready(moonraker);

  moonraker.push_status({{"print_stats", {{"filename", "cubes.gcode"}, {"state", "printing"}}}});
  REQUIRE_MESSAGE(app::wait_until([] { return ui::find_label("cubes.gcode") != nullptr; }), ui::dump_screen());

  ui::tap_label("Cancel");
  REQUIRE(app::wait_until([&] { return moonraker.request_count("printer.print.cancel") > 0; }));
  app::pump(200);
  CHECK_MESSAGE(ui::find_label("cubes.gcode") != nullptr, "print status hidden before Klipper reported it");

  moonraker.push_status({{"print_stats", {{"state", "cancelled"}}}});
  CHECK(app::wait_until([] { return ui::find_label("cubes.gcode") == nullptr; }));
}

// The sensors are rebuilt on a reconnect; the numpad's OK called into the
// deleted one.
TEST_CASE("the numpad closes when its sensor is rebuilt by a reconnect") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  ui::tap_label("Extruder");
  REQUIRE_MESSAGE(app::wait_until([] { return ui::find_shown(&lv_textarea_class) != nullptr; }), ui::dump_screen());

  moonraker.forget_requests();
  moonraker.drop_client();
  REQUIRE(app::wait_until([&] { return moonraker.request_count("printer.objects.subscribe") > 0; }, 10000));
  app::settle(moonraker);
  CHECK(ui::find_shown(&lv_textarea_class) == nullptr);
}

TEST_CASE("print metadata leaves Klipper's print duration intact") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  moonraker.set_result("server.files.metadata", {{"estimated_time", 600}});
  app::start(moonraker);
  app::wait_ready(moonraker);

  moonraker.push_status({{"print_stats", {
    {"filename", "part.gcode"}, {"state", "printing"}, {"print_duration", 30.0}, {"filament_used", 80.0},
  }}});
  REQUIRE(app::wait_until([&] { return moonraker.request_count("server.files.metadata") > 0; }));
  app::settle(moonraker);
  const auto duration = State::get_instance()->get_data_copy("/printer_state/print_stats/print_duration"_json_pointer);
  CHECK(duration == 30.0);
}

TEST_CASE("an empty console history saves the first command across reconnect") {
  json stored_history = json::array();
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  moonraker.set_handler("server.database.get_item", [&](const json &params) {
    return json{{"value", params.value("namespace", "") == "fluidd"
                           ? json{{"commandHistory", stored_history}} : json::object()}};
  });
  moonraker.set_handler("server.database.post_item", [&](const json &params) {
    if (params.value("namespace", "") == "fluidd"
        && params.value("key", "") == "console.commandHistory") {
      stored_history = params.at("value");
    }
    return params;
  });
  app::start(moonraker);
  app::wait_ready(moonraker);
  open_console();

  const char *command = "M105";
  const auto expected = json::array({command});
  auto *input = ui::find_shown(&lv_textarea_class);
  REQUIRE(input != nullptr);
  for (size_t n = 1; n <= 2; ++n) {
    {
      std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
      lv_textarea_set_text(input, command);
    }
    ui::tap_label(LV_SYMBOL_NEW_LINE);
    REQUIRE(app::wait_until([&] { return moonraker.gcodes().size() == n; }));
  }
  app::settle(moonraker);
  const auto writes = moonraker.requests("server.database.post_item");
  CHECK(writes.size() == 1);
  if (!writes.empty()) CHECK(writes.front().at("value") == expected);

  moonraker.forget_requests();
  moonraker.drop_client();
  REQUIRE(app::wait_until([&] { return moonraker.request_count("printer.objects.subscribe") > 0; }, 10000));
  app::wait_ready(moonraker);
  open_console();
  const auto restored = State::get_instance()->get_data_copy("/console/commandHistory"_json_pointer);
  CHECK(restored == expected);
  auto *table = ui::find_shown(&lv_table_class);
  REQUIRE(table != nullptr);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    CHECK(std::string(lv_table_get_cell_value(table, 0, 0)) == command);
  }
}

TEST_CASE("an expired print estimate clears all displayed ETAs") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  moonraker.set_result("server.files.metadata", {{"estimated_time", 600}});
  app::start(moonraker);
  app::wait_ready(moonraker);

  moonraker.push_status({
    {"print_stats", {{"filename", "part.gcode"}, {"state", "printing"}, {"print_duration", 60}}},
    {"virtual_sdcard", {{"progress", 0.55}}},
  });
  const auto previous_eta = KUtils::eta_string(540);
  REQUIRE(app::wait_until([&] { return ui::find_label("ETA: " + previous_eta) != nullptr; }));
  auto *bar = ui::find_shown(&lv_bar_class);
  REQUIRE(bar != nullptr);
  auto end_time = [&] {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    auto *label = lv_obj_get_child(lv_obj_get_parent(bar), -1);
    REQUIRE(lv_obj_check_type(label, &lv_label_class));
    return std::string(lv_label_get_text(label));
  };
  CHECK(end_time() != "...");

  ui::tap_tab(Nav::HOME);
  auto *mini = ui::find_label(previous_eta + " / ", ui::Match::Contains);
  REQUIRE(mini != nullptr);
  ui::tap(mini);
  REQUIRE(app::wait_until([] { return ui::find_label("Pause") != nullptr; }));

  moonraker.push_status({{"print_stats", {{"print_duration", 660}}}});
  const auto elapsed = KUtils::eta_string(660);
  REQUIRE(app::wait_until([&] { return ui::find_label(elapsed) != nullptr; }));
  CHECK(ui::find_label("ETA: " + previous_eta) == nullptr);
  CHECK(end_time() == "...");

  ui::tap_tab(Nav::HOME);
  CHECK_MESSAGE(ui::find_label("ETA: ...", ui::Match::Contains) != nullptr, ui::dump_screen());
}

TEST_CASE("touch calibration shows the first point after the startup delay") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  app::start(moonraker);
  app::wait_ready(moonraker);

  auto &lv_lock = GuppyScreen::get()->get_lock();
  lv_tc_screen_t *screen;
  {
    std::lock_guard<std::mutex> lock(lv_lock);
    screen = reinterpret_cast<lv_tc_screen_t *>(lv_tc_screen_create());
    lv_tc_screen_start(&screen->screenObj);
    CHECK(lv_obj_has_flag(screen->indicatorObj, LV_OBJ_FLAG_HIDDEN));
  }
  REQUIRE(app::wait_until([&] {
    std::lock_guard<std::mutex> lock(lv_lock);
    return !lv_obj_has_flag(screen->indicatorObj, LV_OBJ_FLAG_HIDDEN);
  }));
  {
    std::lock_guard<std::mutex> lock(lv_lock);
    CHECK(screen->currentStep == 1);
    lv_obj_del(&screen->screenObj);
  }
}

TEST_CASE("a failed resume stays retryable until the print state changes") {
  FakeMoonraker moonraker(app::fixture("ad5m_basic.json"));
  moonraker.set_handler("printer.print.resume", [](const json &) {
    return FakeMoonraker::error(400, "Filament runout");
  });
  app::start(moonraker);
  app::wait_ready(moonraker);

  moonraker.push_status({{"print_stats", {{"filename", "part.gcode"}, {"state", "paused"}}},
                         {"pause_resume", {{"is_paused", true}}}});
  REQUIRE(app::wait_until([] { return ui::find_label("Resume") != nullptr; }));
  ui::tap_label("Resume");
  REQUIRE(app::wait_until([&] { return moonraker.request_count("printer.print.resume") == 1; }));
  moonraker.push_status({{"pause_resume", {{"is_paused", false}}}});
  app::settle(moonraker);
  CHECK(ui::find_label("Pause") == nullptr);

  moonraker.set_handler("printer.print.resume", [](const json &) { return "ok"; });
  ui::tap_label("Resume");
  REQUIRE(app::wait_until([&] { return moonraker.request_count("printer.print.resume") == 2; }));
  moonraker.push_status({{"print_stats", {{"state", "printing"}}}});
  REQUIRE(app::wait_until([] { return ui::find_label("Pause") != nullptr; }));
  CHECK(ui::find_label("Resume") == nullptr);

  ui::tap_label("Pause");
  REQUIRE(app::wait_until([&] { return moonraker.request_count("printer.print.pause") == 1; }));
  moonraker.push_status({{"print_stats", {{"state", "paused"}}}});
  CHECK(app::wait_until([] { return ui::find_label("Resume") != nullptr; }));
}

TEST_CASE("late Fluidd history preserves saved and newly entered commands") {
  FakeMoonraker server(app::fixture("ad5m_basic.json"));
  server.set_handler("server.database.get_item", [](const json &params) {
    if (params.value("namespace", "") == "fluidd") {
      return json{{"value", {{"commandHistory", {"M114"}}}}};
    }
    return json{{"value", json::object()}};
  });
  server.hold_next_reply("server.database.get_item");
  app::start(server);
  app::wait_ready(server);
  open_console();
  auto *input = ui::find_shown(&lv_textarea_class);
  REQUIRE(input != nullptr);
  {
    std::lock_guard<std::mutex> lock(GuppyScreen::get()->get_lock());
    lv_textarea_set_text(input, "M105");
  }
  ui::tap_label(LV_SYMBOL_NEW_LINE);
  REQUIRE(app::wait_until([&] { return server.gcodes() == std::vector<std::string>{"M105"}; }));
  app::settle(server);
  CHECK(server.request_count("server.database.post_item") == 0);
  server.release_reply();
  app::settle(server);
  const auto writes = server.requests("server.database.post_item");
  REQUIRE(writes.size() == 1);
  CHECK(writes.front().at("value") == json::array({"M105", "M114"}));
}

TEST_CASE("starting the same file again reloads its name and estimate once") {
  FakeMoonraker server(app::fixture("ad5m_basic.json"));
  server.set_result("server.files.metadata", {{"estimated_time", 600}});
  app::start(server);
  app::wait_ready(server);
  server.push_status({{"print_stats", {{"filename", "part.gcode"}, {"state", "printing"}, {"print_duration", 60}}}});
  REQUIRE(app::wait_until([] { return ui::find_label("ETA: " + KUtils::eta_string(540)) != nullptr; }));
  const auto first = server.request_count("server.files.metadata");
  server.push_status({{"print_stats", {{"state", "complete"}}}});
  REQUIRE(app::wait_until([] { return ui::find_label("Pause") == nullptr; }));

  server.push_status({{"print_stats", {{"state", "printing"}, {"print_duration", 1}}}});
  REQUIRE(app::wait_until([] { return ui::find_label("Pause") != nullptr; }));
  app::settle(server);
  CHECK(ui::find_label("part.gcode") != nullptr);
  CHECK(ui::find_label("ETA: " + KUtils::eta_string(599)) != nullptr);
  CHECK(server.request_count("server.files.metadata") == first + 1);

  server.push_status({{"print_stats", {{"state", "paused"}}}});
  REQUIRE(app::wait_until([] { return ui::find_label("Resume") != nullptr; }));
  server.push_status({{"print_stats", {{"state", "printing"}}}});
  REQUIRE(app::wait_until([] { return ui::find_label("Pause") != nullptr; }));
  app::settle(server);
  CHECK(server.request_count("server.files.metadata") == first + 1);
}

TEST_CASE("print screen refreshes the bed target while its temperature stays constant") {
  FakeMoonraker server(app::fixture("ad5m_basic.json"));
  app::start(server);
  app::wait_ready(server);
  server.push_status({{"print_stats", {{"filename", "test.gcode"}, {"state", "printing"}}}});
  REQUIRE(app::wait_until([] { return ui::find_label("Pause") != nullptr; }));
  server.push_status({{"heater_bed", {{"temperature", 55.0}, {"target", 60.0}}}});
  REQUIRE(app::wait_until([] { return ui::find_label("55 / 60") != nullptr; }));
  server.push_status({{"heater_bed", {{"target", 70.0}}}});
  app::settle(server);
  CHECK_MESSAGE(ui::find_label("55 / 70") != nullptr, ui::dump_screen());
  server.push_status({{"heater_bed", {{"temperature", 56.0}}}});
  CHECK(app::wait_until([] { return ui::find_label("56 / 70") != nullptr; }));
  server.push_status({{"heater_bed", {{"target", 0.0}}}});
  CHECK(app::wait_until([] { return ui::find_label("56") != nullptr; }));
}

TEST_CASE("a print started with a preheated bed retains the unchanged bed target") {
  FakeMoonraker server(app::fixture("ad5m_basic.json"));
  app::start(server);
  app::wait_ready(server);
  server.push_status({{"heater_bed", {{"temperature", 55.0}, {"target", 60.0}}}});
  app::settle(server);
  server.push_status({{"print_stats", {{"filename", "preheated.gcode"}, {"state", "printing"}}}});
  REQUIRE(app::wait_until([] { return ui::find_label("Pause") != nullptr; }));
  CHECK_MESSAGE(ui::find_label("55 / 60") != nullptr, ui::dump_screen());
  server.push_status({{"heater_bed", {{"temperature", 56.0}}}});
  app::settle(server);
  CHECK_MESSAGE(ui::find_label("56 / 60") != nullptr, ui::dump_screen());
}
