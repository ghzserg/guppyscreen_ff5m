#include "state.h"
#include "config.h"
#include "utils.h"
#include "spdlog/spdlog.h"
#include "lvgl/lvgl.h"

const uint32_t GUPPY_COLOR_SIZE = 19;
const lv_palette_t GUPPY_COLORS[GUPPY_COLOR_SIZE] = {
  LV_PALETTE_RED,
  LV_PALETTE_PINK,
  LV_PALETTE_PURPLE,
  LV_PALETTE_DEEP_PURPLE,
  LV_PALETTE_INDIGO,
  LV_PALETTE_BLUE,
  LV_PALETTE_LIGHT_BLUE,
  LV_PALETTE_CYAN,
  LV_PALETTE_TEAL,
  LV_PALETTE_GREEN,
  LV_PALETTE_LIGHT_GREEN,
  LV_PALETTE_LIME,
  LV_PALETTE_YELLOW,
  LV_PALETTE_AMBER,
  LV_PALETTE_ORANGE,
  LV_PALETTE_DEEP_ORANGE,
  LV_PALETTE_BROWN,
  LV_PALETTE_BLUE_GREY,
  LV_PALETTE_GREY
};

std::mutex State::lock;
State *State::instance{NULL};

State::State(std::mutex &state_lock)
  : NotifyConsumer(state_lock)
{
}

State *State::get_instance() {
  if (instance == NULL) {
    instance = new State(State::lock);
  }
  return instance;
}

void State::reset() {
  std::lock_guard<std::mutex> guard(lock);
  data.clear();
  macros.clear();
}

// configfile holds the whole printer config twice, as `config` and as parsed
// `settings`: ~480 KB of JSON on zMod, mostly gcode_macro sections. Keep only
// the sections the panels read, and the macro list parsed once.
// Readers: print_status_panel (config/extruder), inputshaper_panel
// (config/input_shaper), finetune_panel (settings/extruder), retract_panel
// (settings/firmware_retraction), limits_panel (settings/printer),
// macros_panel (get_macros()). Top-level keys such as save_config_pending
// are kept as they are.
json State::slim_configfile(const json &configfile) {
  static const char *const config_keep[] = {"extruder", "input_shaper"};
  static const char *const settings_keep[] = {"extruder", "firmware_retraction", "printer"};

  auto pick = [](const json &from, const auto &keys) {
    json out = json::object();
    if (!from.is_object()) return out;
    for (const char *k : keys) {
      if (from.contains(k)) out[k] = from.at(k);
    }
    return out;
  };

  json out = json::object();
  for (auto it = configfile.begin(); it != configfile.end(); ++it) {
    if (it.key() == "config") {
      macros = KUtils::parse_macros(it.value());
      out["config"] = pick(it.value(), config_keep);
    } else if (it.key() == "settings") {
      out["settings"] = pick(it.value(), settings_keep);
    } else {
      out[it.key()] = it.value();
    }
  }
  return out;
}

void State::set_data(const std::string &key, json &j, const std::string &json_path) {
  std::lock_guard<std::mutex> guard(lock);
  // A reference: merge_patch copies what it keeps, so a deep copy of the
  // incoming message (the whole printer state at subscribe) is not needed.
  const json &patch = j[json::json_pointer(json_path)];
  if (patch.is_null()) {
    return;
  }

  if (key == "printer_state" && patch.is_object() && patch.contains("configfile")) {
    // Same as data[key].merge_patch(patch), with configfile slimmed first.
    json &target = data[key];
    if (!target.is_object()) {
      target = json::object();
    }
    for (auto it = patch.begin(); it != patch.end(); ++it) {
      if (it.value().is_null()) {
        target.erase(it.key());
      } else if (it.key() == "configfile" && it.value().is_object()) {
        target["configfile"].merge_patch(slim_configfile(it.value()));
      } else {
        target[it.key()].merge_patch(it.value());
      }
    }
    return;
  }

  data[key].merge_patch(patch);
}

json &State::get_data() {
  std::lock_guard<std::mutex> guard(lock);
  return data;
}

json &State::get_data(const json::json_pointer& ptr) {
  std::lock_guard<std::mutex> guard(lock);
  return data[ptr];
}

std::map<std::string, std::map<std::string, std::string>> State::get_macros() {
  std::lock_guard<std::mutex> guard(lock);
  return macros;
}

void State::consume(json &j) {
  if (j.contains("params") && !j["params"].empty()) {
    set_data("printer_state", j, "/params/0");
  }
}

std::vector<std::string> State::get_extruders() {
  std::lock_guard<std::mutex> guard(lock);
  auto &objects = data["/printer_objs/objects"_json_pointer];
  std::vector<std::string> extruders;
  if (!objects.is_null()) {
    for (auto &o : objects) {
      const std::string &obj_name = o.template get<std::string>();
      if (obj_name.rfind("extruder", 0) == 0
          && obj_name.rfind("extruder_stepper", 0) != 0) {
        extruders.push_back(obj_name);
      }
    }
  }

  return extruders;
}

std::vector<std::string> State::get_heaters() {
  std::lock_guard<std::mutex> guard(lock);
  auto &objects = data["/printer_objs/objects"_json_pointer];
  std::vector<std::string> heaters;
  if (!objects.is_null()) {
    for (auto &o : objects) {
      const std::string &obj_name = o.template get<std::string>();
      if (obj_name == "heater_bed"
          || obj_name.rfind("heater_generic ", 0) == 0) {
        heaters.push_back(obj_name);
      }
    }
  }

  return heaters;
}

std::vector<std::string> State::get_sensors() {
  std::lock_guard<std::mutex> guard(lock);
  auto &objects = data["/printer_objs/objects"_json_pointer];
  std::vector<std::string> sensors;
  if (!objects.is_null()) {
    for (auto &o : objects) {
      const std::string &obj_name = o.template get<std::string>();
      if (obj_name.rfind("temperature_sensor ", 0) == 0
          || obj_name.rfind("temperature_fan ", 0) == 0) {
        sensors.push_back(obj_name);
      }
    }
  }

  return sensors;
}

std::vector<std::string> State::get_fans() {
  std::lock_guard<std::mutex> guard(lock);
  auto &objects = data["/printer_objs/objects"_json_pointer];
  std::vector<std::string> fans;
  if (!objects.is_null()) {
    for (auto &o : objects) {
      const std::string &obj_name = o.template get<std::string>();
      if (obj_name == "fan"
          || obj_name.rfind("heater_fan ", 0) == 0
          || obj_name.rfind("fan_generic ", 0) == 0
          || obj_name.rfind("controller_fan ", 0) == 0) {
        fans.push_back(obj_name);
      }
    }
  }

  return fans;
}

std::vector<std::string> State::get_leds() {
  std::lock_guard<std::mutex> guard(lock);
  auto &objects = data["/printer_objs/objects"_json_pointer];
  std::vector<std::string> leds;
  if (!objects.is_null()) {
    for (auto &o : objects) {
      const std::string &obj_name = o.template get<std::string>();
      if (obj_name.rfind("led ", 0) == 0) {
        leds.push_back(obj_name);
      }
    }
  }

  return leds;
}

std::vector<std::string> State::get_output_pins() {
  std::lock_guard<std::mutex> guard(lock);
  auto &objects = data["/printer_objs/objects"_json_pointer];
  std::vector<std::string> output_pins;
  if (!objects.is_null()) {
    for (auto &o : objects) {
      const std::string &obj_name = o.template get<std::string>();
      if (obj_name.rfind("output_pin ", 0) == 0) {
        output_pins.push_back(obj_name);
      }
    }
  }

  return output_pins;
}

json State::get_display_sensors() {
  Config *conf = Config::get_instance();
  json &user_sensors = conf->get_json(conf->df() + "monitored_sensors");
  json sensors_by_id;
  if (!user_sensors.is_null()) {
    for (auto &s : user_sensors) {
      sensors_by_id[s["id"].template get<std::string>()] = s;
    }
  }

  json display_sensors;
  auto extruders = get_extruders();
  for (auto &e : extruders) {
    if (sensors_by_id.contains(e)) {
      spdlog::debug("found user configured extruder {}", e);
      display_sensors[e] = sensors_by_id[e];
    }
  }

  auto heaters = get_heaters();
  for (auto &e : heaters) {
    if (sensors_by_id.contains(e)) {
      spdlog::debug("found user configured heater {}", e);
      display_sensors[e] = sensors_by_id[e];
    }
  }

  auto sensors = get_sensors();
  for (auto &e : sensors) {
    if (sensors_by_id.contains(e)) {
      spdlog::debug("found user configured sensor {}", e);
      display_sensors[e] = sensors_by_id[e];
    }
  }

  if (display_sensors.empty()) {
    // default to first extruders/heaters from printer objects
    uint32_t color_idx = 0;
    lv_palette_t color = GUPPY_COLORS[color_idx];
    for (auto &e: extruders) {
      spdlog::debug("default extruder {}", e);
      color = GUPPY_COLORS[color_idx % GUPPY_COLOR_SIZE];
      display_sensors[e] = {
        { "id", e },
        { "display_name", KUtils::to_title(e) },
        { "controllable", true },
        { "color", color }
      };

      color_idx++;
    }

    for (auto &e: heaters) {
      spdlog::debug("default heaters {}", e);
      color = GUPPY_COLORS[color_idx % GUPPY_COLOR_SIZE];
      display_sensors[e] = {
        { "id", e },
        { "display_name", KUtils::to_title(e) },
        { "controllable", true },
        { "color", color }
      };

      color_idx++;
    }

    for (auto &e: sensors) {
      spdlog::debug("default sensors {}", e);
      color = GUPPY_COLORS[color_idx % GUPPY_COLOR_SIZE];
      display_sensors[e] = {
        { "id", e },
        { "display_name", KUtils::to_title(e) },
        { "controllable", false },
        { "color", color }
      };

      color_idx++;
    }

  }

  return display_sensors;
}

json State::get_display_fans() {
  Config *conf = Config::get_instance();
  json &user_fans = conf->get_json(conf->df() + "fans");
  json fans_by_id;
  if (!user_fans.is_null()) {
    for (auto &s : user_fans) {
      fans_by_id[s["id"].template get<std::string>()] = s;
    }
  }

  json display_fans;
  auto fans = get_fans();
  for (auto &e : fans) {
    if (fans_by_id.contains(e)) {
      spdlog::debug("found user configured fan {}", e);
      display_fans[e] = fans_by_id[e];
    }
  }

  // hack to allow output_pin defined fans
  auto output_pins = get_output_pins();
  for (auto &e : output_pins) {
    if (fans_by_id.contains(e)) {
      spdlog::debug("found user configured output_pin fan {}", e);
      display_fans[e] = fans_by_id[e];
    }
  }

  // default to top standard fans
  if (display_fans.empty()) {
    for (auto &e: fans) {
      size_t pos = e.find_last_of(' ');
      std::string display_name = KUtils::to_title(pos != std::string::npos
                                                  ? e.substr(pos + 1)
                                                  : "Part Fan");
      display_fans[e] = {
        {"id", e},
        {"display_name", display_name}
      };
    }
  }

  return display_fans;
}


json State::get_display_leds() {
  Config *conf = Config::get_instance();
  json &user_leds = conf->get_json(conf->df() + "leds");
  json leds_by_id;
  if (!user_leds.is_null()) {
    for (auto &s : user_leds) {
      leds_by_id[s["id"].template get<std::string>()] = s;
    }
  }

  json display_leds;
  auto leds = get_leds();
  for (auto &e : leds) {
    if (leds_by_id.contains(e)) {
      spdlog::debug("found user configured led {}", e);
      display_leds[e] = leds_by_id[e];
    }
  }

  // hack to allow output_pin defined leds
  auto output_pins = get_output_pins();
  for (auto &e : output_pins) {
    if (leds_by_id.contains(e)) {
      spdlog::debug("found user configured output_pin leds {}", e);
      display_leds[e] = leds_by_id[e];
    }
  }

  // default to top standard leds
  if (display_leds.empty()) {
    for (auto &e : output_pins) {
      // hack to support k1 defined output_pin led
      if (e == "output_pin LED") {
        display_leds["output_pin LED"] = {
          {"id", "output_pin LED"},
          {"display_name", "LED"}
        };
      }
    }

    for (auto &e: leds) {
      size_t pos = e.find_last_of(' ');
      std::string display_name = KUtils::to_title(pos != std::string::npos
                                                  ? e.substr(pos + 1)
                                                  : "LED");
      display_leds[e] = {
        {"id", e},
        {"display_name", display_name}
      };
    }
  }

  return display_leds;
}
