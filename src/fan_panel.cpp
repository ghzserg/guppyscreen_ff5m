#include "fan_panel.h"
#include "nav.h"
#include "state.h"
#include "utils.h"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cstdlib>

LV_IMG_DECLARE(cancel);
LV_IMG_DECLARE(fan_on);
#ifdef GUPPY_FF5M
LV_IMG_DECLARE(air_circulation_internal_img);
LV_IMG_DECLARE(air_circulation_external_img);
LV_IMG_DECLARE(air_circulation_stop_img);

// Поля Klipper, по которым виден режим циркуляции (FanPanel::air_now).
static const char *const AIR_FIELDS[5] = {
  "servo my_servo/value",
  "fan_generic chamber_heat_fan/speed", "fan_generic chamber_loop_fan/speed",
  "fan_generic chamber_fan/speed", "fan_generic chamber_cool_fan/speed"};

static double number(const json &v) { return v.is_number() ? v.get<double>() : NAN; }
#endif

FanPanel::FanPanel(KWebSocketClient &websocket_client, std::mutex &lock)
  : NotifyConsumer(lock)
  , ws(websocket_client)
  , fanpanel_cont(lv_obj_create(Nav::get().content()))
  , fans_cont(lv_obj_create(fanpanel_cont))
#ifdef GUPPY_FF5M
  , air_cont(lv_obj_create(fanpanel_cont))
  , air_internal_btn(air_cont, &air_circulation_internal_img, _("Int") /* "Внутр" */,
                     &FanPanel::_handle_air_circulation, this)
  , air_external_btn(air_cont, &air_circulation_external_img, _("Ext") /* "Внеш" */,
                     &FanPanel::_handle_air_circulation, this)
  , air_stop_btn(air_cont, &air_circulation_stop_img, _("Stop") /* "Стоп" */,
                 &FanPanel::_handle_air_circulation, this)
#endif
{
  lv_obj_set_style_pad_all(fanpanel_cont, 0, 0);

  lv_obj_clear_flag(fanpanel_cont, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(fanpanel_cont, LV_PCT(100), LV_PCT(100));

  lv_obj_center(fans_cont);
  lv_obj_set_size(fans_cont, lv_pct(80), lv_pct(100));
  lv_obj_set_flex_flow(fans_cont, LV_FLEX_FLOW_COLUMN);

#ifdef GUPPY_FF5M
  // Столбец у правого края, вентиляторы левее.
  lv_obj_remove_style_all(air_cont);
  lv_obj_set_size(air_cont, LV_SIZE_CONTENT, LV_PCT(100));
  lv_obj_align(air_cont, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_flex_flow(air_cont, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(air_cont, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  for (ButtonContainer *b : {&air_internal_btn, &air_external_btn, &air_stop_btn}) {
    lv_obj_set_width(b->get_container(), 100 * (double)lv_disp_get_physical_hor_res(NULL) / 800.0);  // короткие подписи
    lv_obj_set_style_radius(b->get_container(), 8, LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(b->get_container(), LV_OPA_COVER, LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(b->get_container(), lv_palette_main(LV_PALETTE_BLUE), LV_STATE_CHECKED);
  }
  show_air_circulation(false);
#endif

  ws.register_notify_update(this);
}

FanPanel::~FanPanel() {
  // Строки вентиляторов удаляют свои объекты сами: до удаления их родителя,
  // иначе ~SliderContainer удаляет уже удалённый объект.
  fans.clear();

  if (fanpanel_cont != NULL) {
    lv_obj_del(fanpanel_cont);
    fanpanel_cont = NULL;
  }

  ws.unregister_notify_update(this);
}

void FanPanel::consume(json &j) {
  std::lock_guard<std::mutex> lock(lv_lock);
  for (auto &f : fans) {
    // hack for output_pin fans
    auto fan_value = j[json::json_pointer(fmt::format("/params/0/{}/value", f.first))];
    if (!fan_value.is_null()) {
      int v = static_cast<int>(fan_value.template get<double>() * 100);
      f.second->update_value(v);
    }

    fan_value = j[json::json_pointer(fmt::format("/params/0/{}/speed", f.first))];
    if (!fan_value.is_null()) {
      int v = static_cast<int>(fan_value.template get<double>() * 100);
      f.second->update_value(v);
    }
  }
#ifdef GUPPY_FF5M
  static const std::vector<json::json_pointer> fields = [] {
    std::vector<json::json_pointer> v;
    for (const char *f : AIR_FIELDS) v.emplace_back(std::string("/params/0/") + f);
    return v;
  }();
  bool changed = false;
  for (size_t i = 0; i < fields.size(); i++) {
    if (j.contains(fields[i])) {
      air_now[i] = number(j[fields[i]]);
      changed = true;
    }
  }
  if (changed) show_air_mode();
#endif
}

void FanPanel::create_fans(json &f) {
  std::lock_guard<std::mutex> lock(lv_lock);
  fans.clear();

  std::vector<std::string> keys;
  for (auto &fan : f.items()) keys.push_back(fan.key());  // по id
#ifdef GUPPY_FF5M
  // Сначала M106, затем камера, остальные в порядке Klipper.
  json objs = State::get_instance()->get_data("/printer_objs/objects"_json_pointer);
  auto rank = [&objs](const std::string &k) -> long {
    if (k == "fan_generic fanM106") return -2;
    if (k == "fan_generic chamber_fan") return -1;
    auto at = objs.is_array() ? std::find(objs.begin(), objs.end(), k) : objs.end();
    return objs.is_array() && at != objs.end() ? at - objs.begin() : (long)objs.size();
  };
  std::stable_sort(keys.begin(), keys.end(), [&](const std::string &a, const std::string &b) {
    return rank(a) < rank(b);
  });
#endif

  for (const std::string &key : keys) {
    const json &fan_conf = f[key];
    spdlog::trace("create fan {}, {}", key, fan_conf.dump());
    std::string display_name = fan_conf["display_name"].template get<std::string>();

    lv_event_cb_t fan_cb = &FanPanel::_handle_fan_update;
    if (key == "fan") {
      fan_cb = &FanPanel::_handle_fan_update_part_fan;
    } else if (key.rfind("output_pin ", 0) != 0) {
      // generic_fan, controller_fan, etc.
      fan_cb = &FanPanel::_handle_fan_update_generic;
    }
    auto fptr = std::make_shared<SliderContainer>(fans_cont, display_name.c_str(), &cancel, _("Off") /* "Выкл" */,
                                                  &fan_on, _("Max") /* "Макс" */, fan_cb, this);
    fans.push_back({key, fptr});
    // lv_obj_set_grid_cell(fptr->get_container(), LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, rowidx++, 1);
  }

#ifdef GUPPY_FF5M
  SliderContainer::stack_rows(fans_cont);  // четыре помещаются без прокрутки (у AD5M Pro их четыре)
  if (fans.size() > 4) {
    // Больше прокручиваются друг за другом: равномерно распределённые, они бы налезали.
    lv_obj_set_flex_align(fans_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(fans_cont, LV_OBJ_FLAG_SCROLLABLE);
  }
#else
  if (fans.size() > 3) {
    lv_obj_add_flag(fans_cont, LV_OBJ_FLAG_SCROLLABLE);
  } else {
    lv_obj_clear_flag(fans_cont, LV_OBJ_FLAG_SCROLLABLE);
  }
#endif

}

void FanPanel::foreground() {
  for (auto &f : fans) {
    // hack for output_pin fans
    auto fan_value = State::get_instance()
      ->get_data(json::json_pointer(fmt::format("/printer_state/{}/value", f.first)));
    if (!fan_value.is_null()) {
      int v = static_cast<int>(fan_value.template get<double>() * 100);
      f.second->update_value(v);
    }

    fan_value = State::get_instance()
      ->get_data(json::json_pointer(fmt::format("/printer_state/{}/speed", f.first)));
    if (!fan_value.is_null()) {
      int v = static_cast<int>(fan_value.template get<double>() * 100);
      f.second->update_value(v);
    }
  }

  lv_obj_move_foreground(fanpanel_cont);
}

#ifdef GUPPY_FF5M
void FanPanel::show_air_circulation(bool show) {
  if (show) {
    lv_obj_clear_flag(air_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(air_cont);
    lv_obj_set_width(fans_cont, lv_obj_get_content_width(fanpanel_cont) - lv_obj_get_width(air_cont));
    lv_obj_align(fans_cont, LV_ALIGN_LEFT_MID, 0, 0);
    // Угол заслонки каждого режима берётся из его макроса: SET_SERVO SERVO=my_servo ANGLE=<угол>,
    // и переводится в value, которое Klipper сообщает: ширина импульса / период 20 мс.
    State *state = State::get_instance();
    json servo = state->get_data_copy("/printer_state/configfile/settings/servo my_servo"_json_pointer);
    double min = 0.001, max = 0.002, range = 180;  // умолчания Klipper
    if (servo.is_object()) {
      min = servo.value("minimum_pulse_width", min);
      max = servo.value("maximum_pulse_width", max);
      range = servo.value("maximum_servo_angle", range);
    }
    air_tol = (max - min) / range / 0.020;  // 1°
    const char *macros[2] = {"air_circulation_internal", "air_circulation_external"};
    for (int i = 0; i < 2; i++) {
      air_value[i] = NAN;
      json g = state->get_data_copy(json::json_pointer(
          std::string("/printer_state/configfile/settings/gcode_macro ") + macros[i] + "/gcode"));
      if (!servo.is_object() || !g.is_string()) continue;
      std::string gcode = g.get<std::string>();
      std::transform(gcode.begin(), gcode.end(), gcode.begin(), ::tolower);
      // Только число в той же строке SET_SERVO: шаблон {…} угла не даёт.
      size_t at = gcode.find("servo=my_servo");
      if (at == std::string::npos) continue;
      size_t eol = gcode.find('\n', at);
      at = gcode.find("angle=", at);
      if (at >= eol) continue;
      char *end;
      double angle = strtod(gcode.c_str() + at + 6, &end);
      if (end == gcode.c_str() + at + 6) continue;
      angle = std::min(std::max(angle, 0.0), range);  // Klipper ставит угол в этих пределах
      air_value[i] = (min + angle / range * (max - min)) / 0.020;
    }
    for (int i = 0; i < 5; i++) {
      air_now[i] = number(state->get_data_copy(json::json_pointer(std::string("/printer_state/") + AIR_FIELDS[i])));
    }
    show_air_mode();
  } else {
    lv_obj_add_flag(air_cont, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_width(fans_cont, LV_PCT(80));
    lv_obj_center(fans_cont);
  }
}

void FanPanel::show_air_mode() {
  // C5 Pro: внутренняя — chamber_heat_fan >= 0.5 и chamber_loop_fan >= 0.3,
  // внешняя — chamber_fan >= 0.5 и chamber_cool_fan >= 0.5.
  bool lit[2] = {
    std::fabs(air_now[0] - air_value[0]) < air_tol || (air_now[1] >= 0.5 && air_now[2] >= 0.3),
    std::fabs(air_now[0] - air_value[1]) < air_tol || (air_now[3] >= 0.5 && air_now[4] >= 0.5)};
  ButtonContainer *btns[2] = {&air_internal_btn, &air_external_btn};
  for (int i = 0; i < 2; i++) {
    if (lit[i]) {
      lv_obj_add_state(btns[i]->get_container(), LV_STATE_CHECKED);
    } else {
      lv_obj_clear_state(btns[i]->get_container(), LV_STATE_CHECKED);
    }
  }
}

void FanPanel::handle_air_circulation(lv_event_t *event) {
  lv_obj_t *btn = lv_event_get_current_target(event);
  if (btn == air_internal_btn.get_container()) {
    ws.gcode_script("AIR_CIRCULATION_INTERNAL");
  } else if (btn == air_external_btn.get_container()) {
    ws.gcode_script("AIR_CIRCULATION_EXTERNAL");
  } else if (btn == air_stop_btn.get_container()) {
    ws.gcode_script("AIR_CIRCULATION_STOP");
  }
}
#endif

void FanPanel::handle_fan_update(lv_event_t *event) {
  lv_obj_t *obj = lv_event_get_target(event);

  if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
    double pct = 255 * (double)lv_slider_get_value(obj) / 100.0;

    spdlog::trace("updating fan speed to {}", pct);
    for (auto &f : fans) {
      if (obj == f.second->get_slider()) {
        std::string fan_name = KUtils::get_obj_name(f.first);
              spdlog::trace("update fan {}", fan_name);
        ws.gcode_script(fmt::format("SET_PIN PIN={} VALUE={}", fan_name, pct));
        break;
      }
    }
  } else if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
    obj = lv_event_get_current_target(event);
    for (auto &f : fans) {
      if (obj == f.second->get_off()) {
        std::string fan_name = KUtils::get_obj_name(f.first);
              spdlog::trace("turning off fan {}", fan_name);
        ws.gcode_script(fmt::format("SET_PIN PIN={} VALUE=0", fan_name));
        f.second->update_value(0);
        break;
      } else if (obj == f.second->get_max()) {
        std::string fan_name = KUtils::get_obj_name(f.first);
        spdlog::trace("turning fan to max {}", fan_name);
        ws.gcode_script(fmt::format("SET_PIN PIN={} VALUE=255", fan_name));
        f.second->update_value(100);
        break;
      }
    }
  }
}

void FanPanel::handle_fan_update_part_fan(lv_event_t *event) {
  lv_obj_t *obj = lv_event_get_target(event);

  if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
    double pct = 255 * (double)lv_slider_get_value(obj) / 100.0;

    spdlog::trace("updating part fan speed to {}", pct);
    for (auto &f : fans) {
      if (obj == f.second->get_slider()) {
        ws.gcode_script(fmt::format("M106 S{}", pct));
        break;
      }
    }

  } else if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
    obj = lv_event_get_current_target(event);

    for (auto &f : fans) {
      if (obj == f.second->get_off()) {
        ws.gcode_script("M106 S0");
        f.second->update_value(0);
        break;
      } else if (obj == f.second->get_max()) {
        ws.gcode_script("M106 S255");
        f.second->update_value(100);
        break;
      }
    }
  }
}

void FanPanel::handle_fan_update_generic(lv_event_t *event) {
  lv_obj_t *obj = lv_event_get_target(event);

  if (lv_event_get_code(event) == LV_EVENT_RELEASED) {
    double pct = (double)lv_slider_get_value(obj) / 100.0;

    spdlog::trace("updating fan speed to {}", pct);
    for (auto &f : fans) {
      if (obj == f.second->get_slider()) {
        std::string fan_name = KUtils::get_obj_name(f.first);
              spdlog::trace("update fan {}", fan_name);
        ws.gcode_script(fmt::format("SET_FAN_SPEED FAN={} SPEED={}", fan_name, pct));
        break;
      }
    }
  } else if (lv_event_get_code(event) == LV_EVENT_SHORT_CLICKED) {
    obj = lv_event_get_current_target(event);

    for (auto &f : fans) {
      if (obj == f.second->get_off()) {
        std::string fan_name = KUtils::get_obj_name(f.first);
              spdlog::trace("turning off fan {}", fan_name);
        ws.gcode_script(fmt::format("SET_FAN_SPEED FAN={} SPEED=0", fan_name));
        f.second->update_value(0);
        break;
      } else if (obj == f.second->get_max()) {
        std::string fan_name = KUtils::get_obj_name(f.first);
        spdlog::trace("turning fan to max {}", fan_name);
        ws.gcode_script(fmt::format("SET_FAN_SPEED FAN={} SPEED=1", fan_name));
        f.second->update_value(100);
        break;
      }
    }
  }
}
