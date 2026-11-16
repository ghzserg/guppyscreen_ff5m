// Klipper's configfile holds the whole printer config twice (~460 kB of
// text on zMod, mostly gcode_macro sections). KWebSocketClient::parse_message
// skips the sections no panel reads while parsing, so they never become JSON
// nodes. The messages are written by hand: Moonraker's key order (id after
// result) differs from the fake's sorted one.

#include "doctest/doctest.h"

#include "websocket_client.h"

#include <string>

namespace {

// A subscribe/query reply: configfile is not the first object, a macro
// section is the last key of `config`, and `id` comes last.
const std::string SUBSCRIBE_REPLY = R"({"jsonrpc": "2.0", "result": {"eventtime": 1.0, "status": {
  "extruder": {"temperature": 20.0, "config": {"gcode_macro X": {"gcode": "G28"}}},
  "configfile": {
    "settings": {"gcode_macro start_print": {"gcode": "G28"}, "printer": {"max_velocity": 600.0},
                 "bed_mesh": {"probe_count": [5, 5]}, "extruder": {"pressure_advance": 0.025},
                 "firmware_retraction": {"retract_length": 0.8}},
    "save_config_pending": false,
    "config": {"printer": {"kinematics": "cartesian"}, "extruder": {"filament_diameter": "1.750"},
               "input_shaper": {"shaper_type_x": "mzv"},
               "gcode_macro START_PRINT": {"gcode": "G28\nM104 S{params.T|default(200)}"}},
    "warnings": [{"type": "deprecated_option"}],
    "save_config_pending_items": {}},
  "heater_bed": {"temperature": 27.8}}}, "id": 42})";

const std::string NOTIFICATION = R"({"jsonrpc": "2.0", "method": "notify_status_update", "params": [
  {"configfile": {"config": {"gcode_macro A": {"gcode": "M400"}, "extruder": {"filament_diameter": "2.85"},
                             "gcode_macro B": {"gcode": "M400"}},
                  "save_config_pending": true},
   "heater_bed": {"temperature": 55.0}}, 1000.0]})";

}  // namespace

TEST_CASE("configfile sections no panel reads are dropped while parsing") {
  SUBCASE("subscribe reply") {
    json j = KWebSocketClient::parse_message(SUBSCRIBE_REPLY);
    CHECK(j["id"] == 42);
    const json &status = j["result"]["status"];
    const json &cf = status["configfile"];
    INFO("configfile: " << cf.dump());

    CHECK(cf["config"]["extruder"]["filament_diameter"] == "1.750");
    CHECK(cf["config"]["input_shaper"]["shaper_type_x"] == "mzv");
    CHECK_FALSE(cf["config"].contains("gcode_macro START_PRINT"));
    CHECK_FALSE(cf["config"].contains("printer"));

    CHECK(cf["settings"]["printer"]["max_velocity"] == 600.0);
    CHECK(cf["settings"]["extruder"]["pressure_advance"] == 0.025);
    CHECK(cf["settings"]["firmware_retraction"]["retract_length"] == 0.8);
    CHECK_FALSE(cf["settings"].contains("gcode_macro start_print"));
    CHECK_FALSE(cf["settings"].contains("bed_mesh"));

    // The small fields are kept.
    CHECK(cf["save_config_pending"] == false);
    CHECK(cf["warnings"].size() == 1);
    CHECK(cf["save_config_pending_items"].is_object());

    // Other objects are untouched, even with a key named "config".
    CHECK(status["extruder"]["config"].contains("gcode_macro X"));
    CHECK(status["heater_bed"]["temperature"] == 27.8);
  }

  SUBCASE("subscribe reply with the macros kept") {
    json j = KWebSocketClient::parse_message(SUBSCRIBE_REPLY, true);
    const json &cf = j["result"]["status"]["configfile"];
    INFO("configfile: " << cf.dump());
    CHECK(cf["config"]["gcode_macro START_PRINT"]["gcode"] == "G28\nM104 S{params.T|default(200)}");
    CHECK(cf["config"]["extruder"]["filament_diameter"] == "1.750");
    CHECK_FALSE(cf["config"].contains("printer"));
    CHECK_FALSE(cf["settings"].contains("gcode_macro start_print"));
    CHECK(j["id"] == 42);
  }

  SUBCASE("notification") {
    json j = KWebSocketClient::parse_message(NOTIFICATION);
    INFO("message: " << j.dump());
    const json &cf = j["params"][0]["configfile"];
    CHECK(cf["config"] == json({{"extruder", {{"filament_diameter", "2.85"}}}}));
    CHECK(cf["save_config_pending"] == true);
    CHECK(j["params"][0]["heater_bed"]["temperature"] == 55.0);
    CHECK(j["params"][1] == 1000.0);
    CHECK(j["method"] == "notify_status_update");
  }

  SUBCASE("message without configfile") {
    std::string msg = R"({"jsonrpc": "2.0", "method": "notify_status_update",
      "params": [{"extruder": {"temperature": 215.0}}, 1001.0]})";
    CHECK(KWebSocketClient::parse_message(msg) == json::parse(msg));
  }
}
